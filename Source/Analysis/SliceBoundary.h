#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/TransientDetector.h"

#include <cmath>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  computeNaturalEnds
    //
    //  Post-process a list of Transients to fill in the naturalEnd field for
    //  each onset using an RMS-envelope energy-decay heuristic.
    //
    //  Each one-shot slice starts at t.sampleIndex and ends where the RMS
    //  envelope drops to silenceDb dB below the peak-RMS found in the first
    //  peakWindowSec seconds after that onset.  Slices are ALLOWED TO OVERLAP:
    //  the tail of one hit may extend past the next onset.  This preserves
    //  the full ADSR of every sound regardless of how densely packed the
    //  onsets are.
    //
    //  Algorithm
    //  ---------
    //  1. Build a coarse mono-RMS envelope over the whole file (one value per
    //     ~5 ms window) — O(N) one-time cost, shared across all transients.
    //  2. Per transient:
    //       a. Find peak RMS within [onset, onset + peakWindowSec].
    //       b. Scan forward until RMS < peak × 10^(silenceDb/20), capped at
    //          maxDurationSec from the onset.
    //       c. Write the resulting sample position into t.naturalEnd.
    //
    //  Callers should provide a fallback for slices where naturalEnd == 0
    //  (e.g. empty transient list or audio below the noise floor), though in
    //  practice this only arises for completely silent source material.
    //==========================================================================
    inline void computeNaturalEnds (
        const AudioFile&        file,
        std::vector<Transient>& transients,
        float silenceDb      = -50.0f,  // dB below peak → considered "silence"
        float maxDurationSec =  6.0f,   // hard cap per slice (sustain / pad safe)
        float peakWindowSec  =  0.20f)  // search window for the post-onset peak
    {
        if (transients.empty() || ! file.isValid())
            return;

        const int   numCh  = file.samples.getNumChannels();
        const int   totalS = file.samples.getNumSamples();
        const int   sr     = static_cast<int> (std::round (file.sampleRate));

        // RMS window ≈ 5 ms — fine enough for ADSR envelope detail
        const int winSize = std::max (32, sr / 200);

        // ── Build mono-RMS envelope ──────────────────────────────────────────
        const int numW = (totalS + winSize - 1) / winSize;
        std::vector<float> env (static_cast<std::size_t> (numW), 0.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* data = file.samples.getReadPointer (ch);
            for (int w = 0; w < numW; ++w)
            {
                const int s0  = w * winSize;
                const int s1  = std::min (s0 + winSize, totalS);
                float     acc = 0.0f;
                for (int s = s0; s < s1; ++s)
                    acc += data[s] * data[s];
                env[static_cast<std::size_t> (w)] += acc;
            }
        }

        const float invCh    = 1.0f / static_cast<float> (numCh);
        const float invWinSz = 1.0f / static_cast<float> (winSize);
        for (auto& v : env)
            v = std::sqrt (v * invCh * invWinSz);

        // ── Per-transient natural-end computation ────────────────────────────
        const int maxW  = static_cast<int> (maxDurationSec * file.sampleRate) / winSize + 1;
        const int peakW = std::max (1, static_cast<int> (peakWindowSec * file.sampleRate) / winSize);
        const float silenceRatio = std::pow (10.0f, silenceDb / 20.0f);
        const juce::int64 minEndSamples = static_cast<juce::int64> (sr / 20); // ≥ 50 ms

        for (auto& t : transients)
        {
            const int wOn  = static_cast<int> (t.sampleIndex) / winSize;
            const int wLim = std::min (wOn + maxW, numW);

            // Find peak RMS in the first peakWindowSec after onset
            float peakRms = 0.0f;
            {
                const int wPk = std::min (wOn + peakW, wLim);
                for (int w = wOn; w < wPk; ++w)
                    peakRms = std::max (peakRms, env[static_cast<std::size_t> (w)]);
            }

            if (peakRms < 1e-7f)
            {
                // Silent onset — minimum 50 ms slice
                t.naturalEnd = std::min (t.sampleIndex + minEndSamples,
                                         static_cast<juce::int64> (totalS));
                continue;
            }

            const float threshold = peakRms * silenceRatio;

            // Scan forward for first window at or below the silence threshold.
            // Start one window after onset so we don't trip on a quiet moment
            // right before the attack finishes building up.
            int wEnd = wLim;  // pessimistic: use max-duration cap
            for (int w = wOn + 1; w < wLim; ++w)
            {
                if (env[static_cast<std::size_t> (w)] <= threshold)
                {
                    // Include one extra window so the very tail of the release
                    // is audible rather than clipped at the threshold crossing.
                    wEnd = std::min (w + 2, wLim);
                    break;
                }
            }

            t.naturalEnd = static_cast<juce::int64> (
                std::min (wEnd * winSize, totalS));
        }
    }
} // namespace switchblade::analysis
