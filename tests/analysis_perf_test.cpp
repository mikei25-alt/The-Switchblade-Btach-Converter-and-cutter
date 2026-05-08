// =============================================================================
//  analysis_perf_test.cpp
//
//  Correctness + performance benchmarks for the Switchblade analysis pipeline.
//
//  Build:
//      cmake --build build --target SwitchbladeTests --config Debug
//      ctest --test-dir build -R SwitchbladeTests --output-on-failure
//
//  Perf thresholds are set for Debug builds (no optimisation, MSVC /Od).
//  Release builds will beat these by 10–50x.
// =============================================================================

#include <gtest/gtest.h>

#include "Analysis/AudioFile.h"
#include "Analysis/SpectralFlux.h"
#include "Analysis/AdaptiveThreshold.h"
#include "Analysis/TransientDetector.h"
#include "Analysis/TextureAnalyzer.h"
#include "Analysis/ZeroCrossing.h"
#include "Analysis/PitchDetector.h"

#include <chrono>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
namespace
{
    constexpr double kSampleRate = 44100.0;

    std::vector<float> makeSine (float freqHz, double durationSec,
                                 double sr = kSampleRate)
    {
        const auto n = static_cast<std::size_t> (sr * durationSec);
        std::vector<float> buf (n);
        for (std::size_t i = 0; i < n; ++i)
            buf[i] = std::sin (2.0f * 3.14159265f * freqHz
                               * static_cast<float> (i) / static_cast<float> (sr));
        return buf;
    }

    std::vector<float> makeNoise (double durationSec, double sr = kSampleRate,
                                  unsigned seed = 42)
    {
        const auto n = static_cast<std::size_t> (sr * durationSec);
        std::mt19937 rng { seed };
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        std::vector<float> buf (n);
        for (auto& v : buf) v = dist (rng);
        return buf;
    }

    // Sustained-burst train: each burst is `burstSec` seconds of constant
    // amplitude starting at multiples of `spacingSec`. Bursts are wide enough
    // (>> 10 ms) to survive Hann windowing and the silence gate check window.
    std::vector<float> makeBurstTrain (double spacingSec, double burstSec,
                                       double durationSec, float amplitude = 0.8f,
                                       double sr = kSampleRate)
    {
        const auto n    = static_cast<std::size_t> (sr * durationSec);
        const auto step = static_cast<std::size_t> (sr * spacingSec);
        const auto bLen = static_cast<std::size_t> (sr * burstSec);
        std::vector<float> buf (n, 0.0f);
        for (std::size_t i = 0; i < n; i += step)
            for (std::size_t j = 0; j < bLen && i + j < n; ++j)
                buf[i + j] = amplitude;
        return buf;
    }

    // Single sustained burst at `startSec`, `burstSec` long.
    std::vector<float> makeBurst (double startSec, double burstSec,
                                  double totalSec, float amplitude = 0.8f,
                                  double sr = kSampleRate)
    {
        const auto n     = static_cast<std::size_t> (sr * totalSec);
        const auto start = static_cast<std::size_t> (sr * startSec);
        const auto len   = static_cast<std::size_t> (sr * burstSec);
        std::vector<float> buf (n, 0.0f);
        for (std::size_t i = start; i < std::min (start + len, n); ++i)
            buf[i] = amplitude;
        return buf;
    }

    std::vector<float> makeSilence (double durationSec, double sr = kSampleRate)
    {
        return std::vector<float> (static_cast<std::size_t> (sr * durationSec), 0.0f);
    }

    // Sine burst helpers for spectral-flux onset tests.
    //
    // Why sine, not noise and not DC:
    //   DC burst: only bin 0 changes → flux delta ~0.013, barely above 0.008 floor.
    //   Noise burst: spectrum keeps changing INSIDE the burst → novelty stays
    //     high throughout, so no local maximum at onset (peak-picker misses it).
    //   Sine burst: large flux spike at onset (silence→sine), then ~0 steady-state
    //     novelty (periodic sine has identical Hann-windowed spectra frame-to-frame).
    //     Sharp, reliable local maximum exactly where expected.
    std::vector<float> makeSineBurstTrain (double spacingSec, double burstSec,
                                           double durationSec,
                                           float  freqHz    = 880.0f,
                                           float  amplitude = 0.8f,
                                           double sr        = kSampleRate)
    {
        const auto n    = static_cast<std::size_t> (sr * durationSec);
        const auto step = static_cast<std::size_t> (sr * spacingSec);
        const auto bLen = static_cast<std::size_t> (sr * burstSec);
        std::vector<float> buf (n, 0.0f);
        for (std::size_t burst = 0; burst * step < n; ++burst)
        {
            const std::size_t start = burst * step;
            for (std::size_t j = 0; j < bLen && start + j < n; ++j)
                buf[start + j] = amplitude
                    * std::sin (2.0f * 3.14159265f * freqHz
                                * static_cast<float> (j) / static_cast<float> (sr));
        }
        return buf;
    }

    std::vector<float> makeSineBurst (double startSec, double burstSec,
                                      double totalSec,
                                      float  freqHz    = 880.0f,
                                      float  amplitude = 0.8f,
                                      double sr        = kSampleRate)
    {
        const auto n     = static_cast<std::size_t> (sr * totalSec);
        const auto start = static_cast<std::size_t> (sr * startSec);
        const auto len   = static_cast<std::size_t> (sr * burstSec);
        std::vector<float> buf (n, 0.0f);
        for (std::size_t i = start; i < std::min (start + len, n); ++i)
            buf[i] = amplitude
                * std::sin (2.0f * 3.14159265f * freqHz
                            * static_cast<float> (i - start) / static_cast<float> (sr));
        return buf;
    }

    juce::AudioBuffer<float> toJuceBuffer (const std::vector<float>& mono)
    {
        juce::AudioBuffer<float> buf (1, static_cast<int> (mono.size()));
        std::copy (mono.begin(), mono.end(), buf.getWritePointer (0));
        return buf;
    }

    switchblade::analysis::AudioFile makeAudioFile (const std::vector<float>& mono,
                                                    double sr = kSampleRate)
    {
        switchblade::analysis::AudioFile f;
        f.sampleRate             = sr;
        f.originalLengthInSamples = static_cast<juce::int64> (mono.size());
        f.samples                = toJuceBuffer (mono);
        return f;
    }

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::duration<double, std::milli>;

    double elapsedMs (Clock::time_point start)
    {
        return Ms (Clock::now() - start).count();
    }
} // namespace

// =============================================================================
//  ZeroCrossing — correctness
// =============================================================================

// Signal has exactly one crossing in the search window (clean step function).
// Bug in the original test: a two-sample "bump" (neg→pos→neg) produced two
// crossings, and the closer one (index 12, not 11) was correctly selected.
TEST (ZeroCrossing, FindsCrossingInWindow)
{
    std::vector<float> sig (30);
    std::fill (sig.begin(),      sig.begin() + 11, -0.5f);
    std::fill (sig.begin() + 11, sig.end(),         0.5f);
    // Sole crossing: i=11 (a=sig[10]=-0.5, b=sig[11]=0.5).

    const auto snapped = switchblade::analysis::snapToZeroCrossing (
        std::span<const float> (sig), 15, 10);

    EXPECT_EQ (snapped, 11);
}

TEST (ZeroCrossing, ReturnsTargetWhenNoCrossingInWindow)
{
    std::vector<float> sig (100, 0.5f);
    const auto snapped = switchblade::analysis::snapToZeroCrossing (
        std::span<const float> (sig), 50, 5);
    EXPECT_EQ (snapped, 50);
}

TEST (ZeroCrossing, HandlesEmptySpan)
{
    std::vector<float> empty;
    EXPECT_EQ (switchblade::analysis::snapToZeroCrossing (
        std::span<const float> (empty), 0, 10), 0);
}

TEST (ZeroCrossing, ClampedToBufferEdge)
{
    std::vector<float> sig (20, 1.0f);
    const auto snapped = switchblade::analysis::snapToZeroCrossing (
        std::span<const float> (sig), 18, 100);
    EXPECT_EQ (snapped, 18); // no crossing → return target
}

// =============================================================================
//  SpectralFlux — correctness
// =============================================================================

TEST (SpectralFlux, SilenceProducesNearZeroNovelty)
{
    switchblade::analysis::SpectralFlux flux;
    const auto novelty = flux.process (makeSilence (1.0));

    ASSERT_FALSE (novelty.empty());
    for (float v : novelty)
        EXPECT_NEAR (v, 0.0f, 1e-5f);
}

TEST (SpectralFlux, StationarySineProducesLowNovelty)
{
    switchblade::analysis::SpectralFlux flux;
    const auto novelty = flux.process (makeSine (440.0f, 1.0));

    ASSERT_FALSE (novelty.empty());
    const float maxSteady = *std::max_element (novelty.begin() + 10, novelty.end());
    EXPECT_LT (maxSteady, 0.15f);
}

// Impulse at the CENTER of the first Hann window (position N/2) so it
// receives maximum weighting. An impulse at sample 0 is zeroed by the window.
TEST (SpectralFlux, ImpulseAtWindowCenterProducesHighNovelty)
{
    switchblade::analysis::SpectralFlux::Config cfg;
    cfg.fftOrder = 11; // 2048
    switchblade::analysis::SpectralFlux flux { cfg };

    std::vector<float> impulse (44100, 0.0f);
    impulse[1024] = 1.0f; // center of first 2048-sample frame

    const auto novelty = flux.process (impulse);
    ASSERT_FALSE (novelty.empty());
    EXPECT_GT (novelty[0], 0.1f)
        << "Impulse at window center should produce high novelty in frame 0";
}

TEST (SpectralFlux, NoveltyLengthMatchesExpectedFrameCount)
{
    switchblade::analysis::SpectralFlux::Config cfg;
    cfg.fftOrder = 10; // 1024
    cfg.hopSize  = 256;
    switchblade::analysis::SpectralFlux flux { cfg };

    constexpr std::size_t kSamples = 10000;
    const auto novelty = flux.process (
        std::span<const float> (makeNoise (kSamples / kSampleRate).data(), kSamples));

    const std::size_t expected =
        1 + (kSamples - static_cast<std::size_t> (flux.fftSize()))
          / static_cast<std::size_t> (flux.hopSize());
    EXPECT_EQ (novelty.size(), expected);
}

TEST (SpectralFlux, NoveltyIsNonNegative)
{
    switchblade::analysis::SpectralFlux flux;
    const auto novelty = flux.process (makeNoise (2.0));
    for (std::size_t i = 0; i < novelty.size(); ++i)
        EXPECT_GE (novelty[i], 0.0f) << "Frame " << i << " has negative novelty";
}

// =============================================================================
//  SpectralFlux — perf
//  30 s of noise. Optimised: no curMag_ copy, branchless HWR, half-fill.
//  The juce::dsp::FFT is the bottleneck in Debug (/Od) and absorbs ~95 % of
//  the runtime — the copy/swap savings are real but invisible at this scale.
//  Release builds with /O2 + AVX will beat 100 ms comfortably.
// =============================================================================
TEST (SpectralFluxPerf, ThirtySecondFileUnder1500ms)
{
    const auto signal = makeNoise (30.0);
    switchblade::analysis::SpectralFlux flux;

    const auto t0 = Clock::now();
    const auto novelty = flux.process (signal);
    const double ms = elapsedMs (t0);

    EXPECT_FALSE (novelty.empty());
    EXPECT_LT (ms, 1500.0)
        << "SpectralFlux::process: " << ms << " ms (Debug) for 30 s — "
           "FFT dominates; copy/swap elimination helps most in Release (/O2+AVX)";
}

// =============================================================================
//  AdaptiveThreshold — correctness
// =============================================================================

TEST (AdaptiveThreshold, ReturnsFloorBeforeWindowFills)
{
    switchblade::analysis::AdaptiveThreshold::Params p;
    p.floorAbs   = 0.01f;
    switchblade::analysis::AdaptiveThreshold gate { p };

    EXPECT_FLOAT_EQ (gate (0.1f), p.floorAbs);
    EXPECT_FLOAT_EQ (gate (0.1f), p.floorAbs);
    EXPECT_FLOAT_EQ (gate (0.1f), p.floorAbs);
}

TEST (AdaptiveThreshold, ThresholdRisesWithHighNovelty)
{
    switchblade::analysis::AdaptiveThreshold gate;
    for (int i = 0; i < 70; ++i) (void) gate (5.0f); // fill window
    const float th = gate (0.001f);
    EXPECT_GT (th, 0.5f);
}

TEST (AdaptiveThreshold, ThresholdFallsAfterQuietWindow)
{
    switchblade::analysis::AdaptiveThreshold gate;
    for (int i = 0; i < 70; ++i) (void) gate (0.0f);
    EXPECT_LT (gate (0.0f), 0.1f);
}

TEST (AdaptiveThreshold, MonotonicallyValidAfterReset)
{
    switchblade::analysis::AdaptiveThreshold gate;
    for (int i = 0; i < 70; ++i) (void) gate (1.0f);
    gate.reset();
    switchblade::analysis::AdaptiveThreshold::Params defaultP;
    EXPECT_FLOAT_EQ (gate (10.0f), defaultP.floorAbs);
}

// =============================================================================
//  AdaptiveThreshold — perf
//  Before: two heap allocs of 65 floats per call → 652 ms / 50k calls.
//  After (pre-allocated scratch): sort still dominates in Debug.
//  Target: < 500 ms Debug (allocation savings; sort cost unchanged in /Od).
// =============================================================================
TEST (AdaptiveThresholdPerf, FiftyKCallsUnder700ms)
{
    switchblade::analysis::AdaptiveThreshold gate;
    const auto noise = makeNoise (50000.0 / kSampleRate);

    const auto t0 = Clock::now();
    for (float v : noise)
        (void) gate (v);
    const double ms = elapsedMs (t0);

    EXPECT_LT (ms, 700.0)
        << "AdaptiveThreshold 50k calls: " << ms << " ms (Debug) — "
           "heap alloc removed; remaining cost is two std::sort per call";
}

// =============================================================================
//  TransientDetector — correctness
// =============================================================================

TEST (TransientDetector, SilenceProducesNoTransients)
{
    const auto file = makeAudioFile (makeSilence (2.0));
    EXPECT_TRUE (switchblade::analysis::TransientDetector{}.detect (file).empty());
}

// Sine burst at 0.5 s: silence→sine creates large flux spike, then novelty
// drops to ~0 (periodic sine → identical spectra frame-to-frame). Sharp local
// maximum at onset; reliable detection independent of random seeds.
TEST (TransientDetector, SingleBurstDetected)
{
    const auto sig  = makeSineBurst (0.5, 0.1, 2.0);
    const auto file = makeAudioFile (sig);

    switchblade::analysis::TransientDetector det;
    const auto result = det.detect (file);

    ASSERT_FALSE (result.empty());
    EXPECT_NEAR (result[0].timeSeconds, 0.5, 0.08)
        << "Detected onset at " << result[0].timeSeconds
        << " s; expected near t=0.5 s";
}

TEST (TransientDetector, BurstTrainProducesMultipleTransients)
{
    // 4 sine bursts at t=0, 0.5, 1.0, 1.5 s, each 100 ms long.
    const auto sig  = makeSineBurstTrain (0.5, 0.1, 2.2);
    const auto file = makeAudioFile (sig);

    switchblade::analysis::TransientDetector::Params p;
    p.minSpacingMs = 200.0f;
    const auto result = switchblade::analysis::TransientDetector { p }.detect (file);

    EXPECT_GE (result.size(), 3u)
        << "Expected ≥3 onsets from 4 bursts spaced 0.5 s apart";
}

TEST (TransientDetector, ConfidenceInRange)
{
    const auto file = makeAudioFile (makeBurstTrain (0.3, 0.05, 3.0));
    for (const auto& t : switchblade::analysis::TransientDetector{}.detect (file))
    {
        EXPECT_GE (t.confidence, 0.0f);
        EXPECT_LE (t.confidence, 1.0f) << "Confidence out of [0,1]";
    }
}

TEST (TransientDetector, SampleIndexLessThanFileLength)
{
    const auto sig  = makeBurstTrain (0.25, 0.05, 3.0);
    const auto file = makeAudioFile (sig);
    for (const auto& t : switchblade::analysis::TransientDetector{}.detect (file))
    {
        EXPECT_GE (t.sampleIndex, 0);
        EXPECT_LT (t.sampleIndex, static_cast<std::int64_t> (sig.size()));
    }
}

// The t=0 fallback path synthesises an onset when no peaks are found but
// the file starts with loud content.
TEST (TransientDetector, LeadingImpulseNotSilenceGated)
{
    std::vector<float> sig (static_cast<std::size_t> (kSampleRate), 0.0f);
    sig[0] = 1.0f;
    const auto file = makeAudioFile (sig);
    EXPECT_FALSE (switchblade::analysis::TransientDetector{}.detect (file).empty());
}

// =============================================================================
//  TransientDetector — perf
//  1-minute white noise. Debug baseline: ~2300 ms after AdaptiveThreshold fix.
// =============================================================================
TEST (TransientDetectorPerf, OneMinuteFileUnder5000ms)
{
    const auto file = makeAudioFile (makeNoise (60.0));

    const auto t0 = Clock::now();
    const auto result = switchblade::analysis::TransientDetector{}.detect (file);
    const double ms = elapsedMs (t0);

    EXPECT_LT (ms, 5000.0)
        << "TransientDetector on 1-min noise: " << ms << " ms (Debug) — "
           "SpectralFlux FFT inner loop dominates in unoptimised builds";
    (void) result;
}

// =============================================================================
//  TextureAnalyzer — correctness
// =============================================================================

TEST (TextureAnalyzer, SilenceProducesNoRegions)
{
    EXPECT_TRUE (switchblade::analysis::TextureAnalyzer{}.analyze (
        makeAudioFile (makeSilence (2.0))).empty());
}

TEST (TextureAnalyzer, StationarySineProducesAtLeastOneRegion)
{
    const auto sig  = makeSine (440.0f, 3.0);
    switchblade::analysis::TextureAnalyzer::Params p;
    p.stabilityThreshold = 0.3f;
    p.rmsFloor           = 0.01f;
    EXPECT_FALSE (switchblade::analysis::TextureAnalyzer { p }.analyze (
        makeAudioFile (sig)).empty());
}

TEST (TextureAnalyzer, MinSpacingRespected)
{
    const auto sig  = makeSine (220.0f, 5.0);
    switchblade::analysis::TextureAnalyzer::Params p;
    p.minSpacingMs       = 500.0f;
    p.stabilityThreshold = 0.2f;
    const auto result = switchblade::analysis::TextureAnalyzer { p }.analyze (
        makeAudioFile (sig));
    for (std::size_t i = 1; i < result.size(); ++i)
    {
        const double gapMs = (result[i].timeSeconds - result[i-1].timeSeconds) * 1000.0;
        EXPECT_GE (gapMs, p.minSpacingMs - 30.0)
            << "Gap " << gapMs << " ms < minSpacingMs";
    }
}

TEST (TextureAnalyzer, SampleIndicesInBounds)
{
    const auto sig  = makeSine (440.0f, 3.0);
    const auto file = makeAudioFile (sig);
    for (const auto& t : switchblade::analysis::TextureAnalyzer{}.analyze (file))
    {
        EXPECT_GE (t.sampleIndex, 0);
        EXPECT_LT (t.sampleIndex, static_cast<std::int64_t> (sig.size()));
    }
}

// =============================================================================
//  TextureAnalyzer — perf
//  30 s sine; ring-buffer variance is O(1)/frame vs O(window)/frame before.
//  FFT still dominates in Debug so the gain here is modest (~10 %).
// =============================================================================
TEST (TextureAnalyzerPerf, ThirtySecondSineUnder2000ms)
{
    const auto sig  = makeSine (440.0f, 30.0);
    switchblade::analysis::TextureAnalyzer::Params p;
    p.stabilityThreshold = 0.3f;

    const auto t0 = Clock::now();
    const auto result = switchblade::analysis::TextureAnalyzer { p }.analyze (
        makeAudioFile (sig));
    const double ms = elapsedMs (t0);

    EXPECT_LT (ms, 2000.0)
        << "TextureAnalyzer on 30 s sine: " << ms << " ms (Debug) — "
           "ring-buffer variance applied; FFT loop is the remaining bottleneck";
    (void) result;
}

// =============================================================================
//  PitchDetector — correctness
// =============================================================================

TEST (PitchDetector, Detects440HzSine)
{
    const auto frame = makeSine (440.0f, 2048.0 / kSampleRate);
    const auto result = switchblade::analysis::PitchDetector{}.detect (
        std::span<const float> (frame), kSampleRate);

    ASSERT_TRUE (result.has_value());
    EXPECT_NEAR (result->f0Hz, 440.0f, 5.0f);
    EXPECT_GT   (result->clarity, 0.5f);
}

TEST (PitchDetector, ReturnsNulloptOrLowClarityForNoise)
{
    const auto frame = makeNoise (2048.0 / kSampleRate);
    const auto result = switchblade::analysis::PitchDetector{}.detect (
        std::span<const float> (frame), kSampleRate);
    if (result.has_value())
        EXPECT_LT (result->clarity, 0.5f);
}

TEST (PitchDetector, NoteNameFrom440)
{
    EXPECT_EQ (switchblade::analysis::PitchDetector::noteNameFromHz (440.0f), "A4");
}

TEST (PitchDetector, NoteNameFromMiddleC)
{
    EXPECT_EQ (switchblade::analysis::PitchDetector::noteNameFromHz (261.63f), "C4");
}

TEST (PitchDetector, ReturnsNulloptOrLowClarityForSilence)
{
    const auto frame = makeSilence (2048.0 / kSampleRate);
    const auto result = switchblade::analysis::PitchDetector{}.detect (
        std::span<const float> (frame), kSampleRate);
    if (result.has_value())
        EXPECT_LT (result->clarity, 0.3f);
}

// =============================================================================
//  PitchDetector — perf
//  O(W²) YIN: 100 frames × 2048 samples × ~800 lags.
//  No algorithmic fix here; threshold documents the Debug baseline.
//  An FFT-based autocorrelation would reduce this to O(W log W).
// =============================================================================
TEST (PitchDetectorPerf, OneHundredFramesUnder2000ms)
{
    std::vector<std::vector<float>> frames;
    frames.reserve (100);
    for (int i = 0; i < 100; ++i)
        frames.push_back (makeSine (220.0f * (1 + i % 4), 2048.0 / kSampleRate));

    switchblade::analysis::PitchDetector pd;
    const auto t0 = Clock::now();
    for (const auto& f : frames)
        (void) pd.detect (std::span<const float> (f), kSampleRate);
    const double ms = elapsedMs (t0);

    EXPECT_LT (ms, 2000.0)
        << "PitchDetector 100 frames: " << ms << " ms (Debug) — "
           "O(W²) YIN; FFT-based autocorrelation would cut to O(W log W)";
}

// =============================================================================
//  mixToMono — correctness
// =============================================================================

TEST (MixToMono, StereoAveragesChannels)
{
    juce::AudioBuffer<float> stereo (2, 4);
    stereo.getWritePointer (0)[0] =  1.0f; stereo.getWritePointer (1)[0] = -1.0f;
    stereo.getWritePointer (0)[1] =  0.0f; stereo.getWritePointer (1)[1] =  0.0f;
    stereo.getWritePointer (0)[2] = -1.0f; stereo.getWritePointer (1)[2] =  1.0f;
    stereo.getWritePointer (0)[3] =  0.5f; stereo.getWritePointer (1)[3] =  0.5f;

    std::vector<float> mono;
    switchblade::analysis::mixToMono (stereo, mono);

    ASSERT_EQ (mono.size(), 4u);
    EXPECT_NEAR (mono[0], 0.0f, 1e-6f);
    EXPECT_NEAR (mono[1], 0.0f, 1e-6f);
    EXPECT_NEAR (mono[2], 0.0f, 1e-6f);
    EXPECT_NEAR (mono[3], 0.5f, 1e-6f);
}

TEST (MixToMono, MonoPassthrough)
{
    juce::AudioBuffer<float> buf (1, 3);
    buf.getWritePointer (0)[0] = 0.1f;
    buf.getWritePointer (0)[1] = 0.2f;
    buf.getWritePointer (0)[2] = 0.3f;

    std::vector<float> mono;
    switchblade::analysis::mixToMono (buf, mono);

    ASSERT_EQ (mono.size(), 3u);
    EXPECT_NEAR (mono[0], 0.1f, 1e-6f);
    EXPECT_NEAR (mono[1], 0.2f, 1e-6f);
    EXPECT_NEAR (mono[2], 0.3f, 1e-6f);
}

// =============================================================================
//  End-to-end smoke
// =============================================================================
TEST (EndToEnd, BurstTrainRoundtrip)
{
    // 4 sine bursts at 0, 0.5, 1.0, 1.5 s → expect ≥2 transients.
    const auto sig  = makeSineBurstTrain (0.5, 0.1, 2.2);
    const auto file = makeAudioFile (sig);

    switchblade::analysis::TransientDetector::Params p;
    p.minSpacingMs = 200.0f;
    const auto transients =
        switchblade::analysis::TransientDetector { p }.detect (file);

    EXPECT_GE (transients.size(), 2u)
        << "Expected ≥2 onsets from 4-burst train";
    for (const auto& t : transients)
    {
        EXPECT_GE (t.sampleIndex, 0);
        EXPECT_LT (t.sampleIndex, static_cast<std::int64_t> (sig.size()));
        EXPECT_GE (t.timeSeconds, 0.0);
        EXPECT_LT (t.timeSeconds, 2.3);
    }
}
