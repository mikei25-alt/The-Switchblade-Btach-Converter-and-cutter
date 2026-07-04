#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  TempoEstimator
    //
    //  Estimates the beat period of a mono signal so AnalysisMode::Grid can lay
    //  musical-subdivision boundaries (1/4, 1/8, …) at TRUE tempo-relative
    //  intervals across the whole file — not by naively dividing the file into
    //  N equal chunks (which only works when the file is exactly one bar long).
    //
    //  Method (deterministic, FFT-free, cheap):
    //    1. Short-time energy envelope over non-overlapping `hop`-sample windows.
    //    2. Onset-novelty = half-wave-rectified first difference of the energy.
    //    3. Autocorrelate the (mean-removed) novelty and pick the lag with the
    //       strongest correlation inside the [minBpm, maxBpm] search band.
    //    4. firstBeatSample = first window whose novelty exceeds a fraction of
    //       the peak — used to phase-align the grid to the first downbeat so a
    //       few ms of leading silence don't shift every slice off the hit.
    //
    //  For four-on-the-floor material (e.g. an isolated kick track) the novelty
    //  spikes once per beat and the autocorrelation peak lands exactly on the
    //  quarter-note period. Tonal loops with a clear pulse work too.
    //==========================================================================
    struct TempoEstimate
    {
        double       bpm               { 0.0 };  // 0 = no reliable estimate
        double       beatPeriodSamples { 0.0 };  // one quarter note, in samples
        std::int64_t firstBeatSample   { 0 };    // phase anchor for the grid
        float        confidence        { 0.0f }; // 0..1 — autocorr peak strength

        [[nodiscard]] bool valid() const noexcept { return bpm > 0.0; }
    };

    class TempoEstimator
    {
    public:
        struct Params
        {
            double minBpm  { 60.0 };
            double maxBpm  { 200.0 };
            int    hop     { 512 };   // energy-window size in samples
            // Perceptual tempo prior: a log-Gaussian preference curve centred on
            // centreBpm down-weights autocorrelation peaks at metrical
            // sub-/super-multiples (the classic "detected 79 for a 120 BPM
            // groove" error). biasSigma is in natural-log-tempo units;
            // <= 0 disables the prior.
            double centreBpm  { 120.0 };
            double biasSigma  { 0.55 };
        };

        TempoEstimator() = default;
        explicit TempoEstimator (Params p) : params_ (p) {}

        [[nodiscard]] TempoEstimate estimate (std::span<const float> mono,
                                              double sampleRate) const
        {
            TempoEstimate out;
            const int hop = std::max (64, params_.hop);
            if (sampleRate <= 0.0 || mono.size() < static_cast<std::size_t> (hop * 4))
                return out;

            // ── 1. Short-time energy envelope ────────────────────────────────
            const std::int64_t n         = static_cast<std::int64_t> (mono.size());
            const std::int64_t numFrames = n / hop;
            if (numFrames < 4)
                return out;

            std::vector<double> energy (static_cast<std::size_t> (numFrames), 0.0);
            for (std::int64_t f = 0; f < numFrames; ++f)
            {
                double sum = 0.0;
                const std::int64_t base = f * hop;
                for (int i = 0; i < hop; ++i)
                {
                    const double s = static_cast<double> (mono[static_cast<std::size_t> (base + i)]);
                    sum += s * s;
                }
                energy[static_cast<std::size_t> (f)] = sum;
            }

            // ── 2. Onset novelty (half-wave-rectified first difference) ──────
            std::vector<double> novelty (static_cast<std::size_t> (numFrames), 0.0);
            double maxNov = 0.0;
            for (std::int64_t f = 1; f < numFrames; ++f)
            {
                const double d = energy[static_cast<std::size_t> (f)]
                               - energy[static_cast<std::size_t> (f - 1)];
                const double nv = d > 0.0 ? d : 0.0;
                novelty[static_cast<std::size_t> (f)] = nv;
                maxNov = std::max (maxNov, nv);
            }
            if (maxNov <= 0.0)
                return out;   // silence / DC — no pulse to find

            // Phase anchor: first window with a clear onset (> 25 % of peak).
            // Found on the raw novelty, before the smoothing below widens it.
            std::int64_t firstBeatFrame = 0;
            for (std::int64_t f = 1; f < numFrames; ++f)
            {
                if (novelty[static_cast<std::size_t> (f)] > 0.25 * maxNov)
                {
                    firstBeatFrame = f;
                    break;
                }
            }

            // Smooth the novelty with a 3-point Hann kernel. A beat period that
            // is a non-integer number of frames (e.g. 150 BPM at 512-hop =
            // 34.45 frames) otherwise splits its autocorrelation peak across
            // two lag bins, halving it — which loses to the sharper peak at
            // DOUBLE the period and produces a classic half-tempo error.
            // Widening each onset spike keeps the peak mass in one bin.
            {
                std::vector<double> smoothed (novelty.size());
                smoothed[0] = novelty[0];
                for (std::size_t f = 1; f + 1 < smoothed.size(); ++f)
                    smoothed[f] = 0.25 * novelty[f - 1]
                                + 0.50 * novelty[f]
                                + 0.25 * novelty[f + 1];
                smoothed[smoothed.size() - 1] = novelty[novelty.size() - 1];
                novelty = std::move (smoothed);
            }

            // Mean-remove so the autocorrelation reflects periodicity, not the
            // DC level of the (always non-negative) novelty curve.
            double mean = 0.0;
            for (double v : novelty) mean += v;
            mean /= static_cast<double> (numFrames);
            for (double& v : novelty) v -= mean;

            // ── 3. Autocorrelation over the BPM search band ──────────────────
            const auto framesForBpm = [&] (double bpm)
            {
                return (60.0 / bpm) * sampleRate / static_cast<double> (hop);
            };
            std::int64_t lagMin = static_cast<std::int64_t> (std::floor (framesForBpm (params_.maxBpm)));
            std::int64_t lagMax = static_cast<std::int64_t> (std::ceil  (framesForBpm (params_.minBpm)));
            lagMin = std::max<std::int64_t> (1, lagMin);
            lagMax = std::min<std::int64_t> (lagMax, numFrames - 1);
            if (lagMin >= lagMax)
                return out;

            double energyZero = 0.0;
            for (double v : novelty) energyZero += v * v;
            if (energyZero <= 0.0)
                return out;

            // Unbiased estimate: dividing by the overlap length removes the
            // inherent bias of raw autocorrelation towards short lags (fast
            // tempos), which simply accumulate more product terms.
            const auto autocorrAt = [&] (std::int64_t lag)
            {
                double acc = 0.0;
                for (std::int64_t f = 0; f + lag < numFrames; ++f)
                    acc += novelty[static_cast<std::size_t> (f)]
                         * novelty[static_cast<std::size_t> (f + lag)];
                return acc / static_cast<double> (numFrames - lag);
            };

            // Perceptual tempo prior — weights each lag by how musically
            // plausible its tempo is, so a stronger-but-wrong sub-multiple peak
            // loses to the true beat near centreBpm.
            const auto tempoWeight = [&] (std::int64_t lag)
            {
                if (params_.biasSigma <= 0.0)
                    return 1.0;
                const double bpm = 60.0 * sampleRate / (static_cast<double> (lag) * hop);
                const double r   = std::log (bpm / params_.centreBpm) / params_.biasSigma;
                return std::exp (-0.5 * r * r);
            };

            double bestScore = -1.0;   // weighted score used for selection
            double bestRaw   = 0.0;    // raw autocorrelation at the winner
            std::int64_t bestLag = 0;
            for (std::int64_t lag = lagMin; lag <= lagMax; ++lag)
            {
                const double acc = autocorrAt (lag);

                // Harmonic reinforcement: the true beat period is also periodic
                // at twice its lag, while a spurious half-period peak (double
                // tempo) has no support at ITS double. Adding 0.5 * acf(2*lag)
                // disambiguates octave errors that the log-Gaussian prior alone
                // can't always settle.
                double support = acc;
                if (2 * lag < numFrames - 1)
                    support += 0.5 * autocorrAt (2 * lag);

                const double score = support * tempoWeight (lag);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestRaw   = acc;
                    bestLag   = lag;
                }
            }
            if (bestLag <= 0 || bestRaw <= 0.0)
                return out;

            // ── Octave disambiguation ────────────────────────────────────────
            // A pulse train with period P has near-equal autocorrelation at P
            // and 2P, and a backbeat (kick 1/3, snare 2/4) even peaks at the
            // bar level; the log-Gaussian prior then often keeps the half-tempo
            // reading. If the winner's HALF lag carries substantial support,
            // events repeat at the half period too: take the faster, denser
            // reading. Threshold calibration on synthetic material: when the
            // winner is a spurious double period the true beat shows >= 0.57 of
            // its correlation; a genuinely subordinate half period (eighth-note
            // hats under a backbeat) shows <= 0.20 — so 0.45 splits the gap
            // with margin on both sides. Staying above lagMin keeps the result
            // inside the configured BPM band.
            {
                std::int64_t bestHalf    = 0;
                double       bestHalfAcc = -1.0;
                // Floor and ceiling of bestLag/2 — one candidate when even.
                const std::int64_t halfLo = bestLag / 2;
                const std::int64_t halfHi = (bestLag + 1) / 2;
                for (std::int64_t cand = halfLo; cand <= halfHi; ++cand)
                {
                    if (cand >= lagMin && cand < numFrames)
                    {
                        const double a = autocorrAt (cand);
                        if (a > bestHalfAcc) { bestHalfAcc = a; bestHalf = cand; }
                    }
                }
                if (bestHalf > 0 && bestHalfAcc >= 0.45 * bestRaw)
                {
                    bestLag = bestHalf;
                    bestRaw = bestHalfAcc;
                }
            }

            // ── 4. Refine the lag to the sub-frame peak by parabolic interp ──
            double refinedLag = static_cast<double> (bestLag);
            if (bestLag > lagMin && bestLag < lagMax)
            {
                const double ym = autocorrAt (bestLag - 1);
                const double y0 = bestRaw;   // raw autocorr — must match ym/yp
                const double yp = autocorrAt (bestLag + 1);
                const double denom = (ym - 2.0 * y0 + yp);
                if (std::abs (denom) > 1e-12)
                {
                    const double delta = 0.5 * (ym - yp) / denom;
                    if (delta > -1.0 && delta < 1.0)
                        refinedLag += delta;
                }
            }

            out.beatPeriodSamples = refinedLag * static_cast<double> (hop);
            out.bpm               = 60.0 * sampleRate / out.beatPeriodSamples;
            out.firstBeatSample   = firstBeatFrame * static_cast<std::int64_t> (hop);
            // Both sides are per-term averages now: bestRaw is the unbiased
            // acf and energyZero/numFrames is the unbiased acf at lag 0.
            out.confidence        = static_cast<float> (
                std::clamp (bestRaw * static_cast<double> (numFrames) / energyZero,
                            0.0, 1.0));
            return out;
        }

    private:
        Params params_;
    };

    //==========================================================================
    //  parseBpmFromFilename
    //
    //  Sample libraries usually encode tempo in the filename (e.g.
    //  "TSE_120_D_1_Tabla.wav", "Loop_128_Cmaj.wav", "groove_140bpm.wav"). That
    //  label is far more reliable than blind audio detection on syncopated
    //  material, so Grid mode trusts it when present. Returns a BPM in [40, 300].
    //
    //  Matching, most-specific first:
    //    1. A number adjacent to "bpm" (either side, case-insensitive).
    //    2. Otherwise, exactly ONE delimited integer in the tempo range — if the
    //       name has several plausible numbers it is ambiguous and we decline
    //       (audio detection / the manual field then take over).
    //==========================================================================
    [[nodiscard]] inline std::optional<double> parseBpmFromFilename (std::string_view name)
    {
        std::string lower (name);
        std::transform (lower.begin(), lower.end(), lower.begin(),
                        [] (unsigned char ch) { return static_cast<char> (std::tolower (ch)); });

        const auto isDigit = [] (char c) { return std::isdigit (static_cast<unsigned char> (c)) != 0; };

        // Parse a digit run safely (>=7 digits can't be a tempo → reject).
        const auto parseRun = [] (std::string_view s) -> long
        {
            if (s.empty() || s.size() > 6) return -1;
            long v = 0;
            for (char c : s) v = v * 10 + (c - '0');
            return v;
        };
        const auto plausible = [] (long v) { return v >= 40 && v <= 300; };

        // 1. Digits adjacent to "bpm".
        if (const auto bp = lower.find ("bpm"); bp != std::string::npos)
        {
            // left side
            long r = static_cast<long> (bp) - 1;
            while (r >= 0 && std::isspace (static_cast<unsigned char> (lower[static_cast<std::size_t> (r)]))) --r;
            long hi = r;
            while (r >= 0 && isDigit (lower[static_cast<std::size_t> (r)])) --r;
            if (hi > r)
            {
                const long v = parseRun (std::string_view (lower).substr (
                    static_cast<std::size_t> (r + 1), static_cast<std::size_t> (hi - r)));
                if (plausible (v)) return static_cast<double> (v);
            }
            // right side
            long a = static_cast<long> (bp) + 3;
            while (a < static_cast<long> (lower.size())
                   && std::isspace (static_cast<unsigned char> (lower[static_cast<std::size_t> (a)]))) ++a;
            long b = a;
            while (b < static_cast<long> (lower.size()) && isDigit (lower[static_cast<std::size_t> (b)])) ++b;
            if (b > a)
            {
                const long v = parseRun (std::string_view (lower).substr (
                    static_cast<std::size_t> (a), static_cast<std::size_t> (b - a)));
                if (plausible (v)) return static_cast<double> (v);
            }
        }

        // 2. Exactly one plausible delimited integer.
        std::optional<double> only;
        bool ambiguous = false;
        for (std::size_t i = 0; i < lower.size(); )
        {
            if (! isDigit (lower[i])) { ++i; continue; }
            std::size_t j = i;
            while (j < lower.size() && isDigit (lower[j])) ++j;
            const long v = parseRun (std::string_view (lower).substr (i, j - i));
            if (plausible (v))
            {
                if (only.has_value()) ambiguous = true;
                else                  only = static_cast<double> (v);
            }
            i = j;
        }
        return (only.has_value() && ! ambiguous) ? only : std::nullopt;
    }
} // namespace switchblade::analysis
