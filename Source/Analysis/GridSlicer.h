#pragma once

#include "Analysis/TransientDetector.h"   // Transient
#include "Analysis/ZeroCrossing.h"        // snapToZeroCrossing
#include "Analysis/TempoEstimator.h"      // TempoEstimator

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace switchblade::analysis
{
    struct GridBuild
    {
        std::vector<Transient> transients;
        std::optional<double>  bpm;   // set when a tempo was used (auto or manual)
    };

    //==========================================================================
    //  Tempo-relative Grid boundary builder. Shared by the async worker path
    //  and analyzeSync (CLI) so both slice identically, and unit-tested directly.
    //
    //  kGridStops semantics (see MainContainer): N == 4 → one quarter note,
    //  N == 8 → one eighth, etc. A 4/4 bar is 4 beats, so the spacing for a
    //  given N is  step = beatPeriod * 4 / N. Boundaries are laid from the first
    //  detected beat across the WHOLE file — this is the fix for the old
    //  "divide the file into N equal chunks" behaviour, which only made sense
    //  when the file was exactly one bar long and produced 83-second slices on
    //  a multi-bar track.
    //
    //  manualBpm > 0 forces that tempo; otherwise the beat period is
    //  auto-detected. When no tempo can be found (un-pulsed or very short
    //  material) it falls back to the legacy whole-file/N division so Grid still
    //  yields N boundaries rather than nothing.
    //==========================================================================
    [[nodiscard]] inline GridBuild buildGridTransients (std::span<const float> mono,
                                                        double         sampleRate,
                                                        std::int64_t   totalLen,
                                                        int            N,
                                                        double         manualBpm)
    {
        GridBuild gb;
        if (totalLen <= 0 || N < 2 || sampleRate <= 0.0)
            return gb;

        double       beatSamples = 0.0;
        std::int64_t anchor      = 0;
        double       bpm         = 0.0;

        if (manualBpm > 0.0)
        {
            bpm         = manualBpm;
            beatSamples = 60.0 / bpm * sampleRate;
        }
        else if (! mono.empty())
        {
            const auto est = TempoEstimator {}.estimate (mono, sampleRate);
            if (est.valid())
            {
                bpm         = est.bpm;
                beatSamples = est.beatPeriodSamples;
                anchor      = est.firstBeatSample;
            }
        }

        if (bpm > 0.0)
            gb.bpm = bpm;

        double stepF = (beatSamples > 0.0)
            ? beatSamples * 4.0 / static_cast<double> (N)
            : static_cast<double> (totalLen) / static_cast<double> (N);
        std::int64_t step = static_cast<std::int64_t> (std::llround (stepF));
        if (step < 1) step = 1;

        if (anchor < 0 || anchor >= totalLen) anchor = 0;

        // Runaway guard: a pathologically small detected period must not spawn
        // tens of thousands of slices. 1024 covers a full song even at fine
        // subdivisions; coarser stops stay well under it.
        constexpr std::int64_t kMaxGridSlices = 1024;

        const std::int64_t snapRadius =
            static_cast<std::int64_t> (std::llround (0.005 * sampleRate)); // ±5 ms

        gb.transients.reserve (64);
        std::int64_t prevSnapped = -1;
        std::int64_t i = 0;
        for (std::int64_t raw = anchor;
             raw < totalLen && i < kMaxGridSlices;
             raw += step, ++i)
        {
            std::int64_t snapped = mono.empty()
                ? raw
                : snapToZeroCrossing (mono, raw, snapRadius);

            // Monotonicity guard — high N vs short step can let neighbouring
            // snaps overlap; fall back to the strictly increasing raw index.
            if (snapped <= prevSnapped)
                snapped = std::max (raw, prevSnapped + 1);
            if (snapped >= totalLen)
                break;
            prevSnapped = snapped;

            Transient t;
            t.sampleIndex    = snapped;
            t.rawSampleIndex = raw;
            t.timeSeconds    = static_cast<double> (snapped) / sampleRate;
            t.confidence     = 1.0f;   // deterministic — every grid line is "certain"
            gb.transients.push_back (t);
        }
        return gb;
    }
} // namespace switchblade::analysis
