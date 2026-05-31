#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/TransientDetector.h"   // Transient
#include "Analysis/PitchDetector.h"       // detectSlicePitchHz

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  curateGridOneShots
    //
    //  Reduce a list of grid one-shot Transients to at most maxKeep
    //  representative hits. Slicing has already happened on the subdivision;
    //  this picks the strongest + most timbrally/pitch-DISTINCT slices so the
    //  user gets "a few good ones" instead of every repeat.
    //
    //  Algorithm (cheap, FFT-free):
    //    1. Per-slice features: level (RMS dB), brightness (zero-crossing rate),
    //       decay length (slice duration), pitch (log2 Hz, median-filled when a
    //       slice is unpitched so the dimension stays neutral).
    //    2. Drop silent cells (keep slices within 20 dB of the loudest).
    //    3. Farthest-point selection (k-center greedy) in z-normalised feature
    //       space, seeded by the strongest slice — maximises variety.
    //    4. Keep the chosen Transients in temporal order.
    //
    //  maxKeep <= 0, or >= the eligible count, keeps every (non-silent) slice.
    //==========================================================================
    inline void curateGridOneShots (const AudioFile&        file,
                                    std::vector<Transient>& transients,
                                    int                     maxKeep)
    {
        const int n = static_cast<int> (transients.size());
        if (n <= 1 || ! file.isValid())
            return;

        const int    numCh = file.samples.getNumChannels();
        const int    total = file.samples.getNumSamples();
        const double sr    = file.sampleRate;

        struct Feature
        {
            int    index    { 0 };     // back-reference into transients
            double levelDb  { 0.0 };
            double zcr      { 0.0 };
            double durSec   { 0.0 };
            double pitchLog { 0.0 };   // log2(Hz); NaN when unpitched
        };

        std::vector<Feature> feats;
        feats.reserve (static_cast<std::size_t> (n));

        double maxRms = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const std::int64_t s = transients[static_cast<std::size_t> (i)].sampleIndex;
            std::int64_t e = transients[static_cast<std::size_t> (i)].naturalEnd > 0
                ? transients[static_cast<std::size_t> (i)].naturalEnd
                : (i + 1 < n ? transients[static_cast<std::size_t> (i + 1)].sampleIndex
                             : static_cast<std::int64_t> (total));
            e = std::min<std::int64_t> (e, total);
            const std::int64_t len = std::max<std::int64_t> (1, e - s);

            double       sumSq = 0.0;
            std::int64_t zc    = 0;
            float        prev  = 0.0f;
            for (std::int64_t k = 0; k < len; ++k)
            {
                float m = 0.0f;
                for (int c = 0; c < numCh; ++c)
                    m += file.samples.getSample (c, static_cast<int> (s + k));
                m /= static_cast<float> (std::max (1, numCh));
                sumSq += static_cast<double> (m) * static_cast<double> (m);
                if (k > 0 && ((m >= 0.0f) != (prev >= 0.0f)))
                    ++zc;
                prev = m;
            }
            const double rms = std::sqrt (sumSq / static_cast<double> (len));
            maxRms = std::max (maxRms, rms);

            const auto pitch = detectSlicePitchHz (file, s, e);

            feats.push_back (Feature {
                i,
                20.0 * std::log10 (std::max (rms, 1e-9)),
                static_cast<double> (zc) / static_cast<double> (len),
                static_cast<double> (len) / sr,
                pitch.has_value() ? std::log2 (static_cast<double> (*pitch))
                                  : std::numeric_limits<double>::quiet_NaN()
            });
        }

        // ── 2. Drop silent cells (within 20 dB of the loudest survive) ───────
        const double loudestDb = (maxRms > 0.0 ? 20.0 * std::log10 (maxRms) : -120.0);
        const double floorDb    = loudestDb - 20.0;
        std::vector<Feature> elig;
        elig.reserve (feats.size());
        for (const auto& f : feats)
            if (f.levelDb >= floorDb)
                elig.push_back (f);
        if (elig.empty())
            elig = feats;   // everything quiet — fall back to keeping all

        const auto emit = [&] (const std::vector<Feature>& picks)
        {
            std::vector<Transient> out;
            out.reserve (picks.size());
            for (const auto& f : picks)
                out.push_back (transients[static_cast<std::size_t> (f.index)]);
            transients = std::move (out);
        };

        if (maxKeep <= 0 || static_cast<int> (elig.size()) <= maxKeep)
        {
            emit (elig);   // nothing to curate beyond dropping silence
            return;
        }

        // Median-fill unpitched slices so pitch is neutral, not an outlier.
        {
            std::vector<double> pitched;
            for (const auto& f : elig)
                if (! std::isnan (f.pitchLog)) pitched.push_back (f.pitchLog);
            double median = 0.0;
            if (! pitched.empty())
            {
                std::sort (pitched.begin(), pitched.end());
                median = pitched[pitched.size() / 2];
            }
            for (auto& f : elig)
                if (std::isnan (f.pitchLog)) f.pitchLog = median;
        }

        // ── 3. z-normalise each dimension, then farthest-point select ────────
        const auto zStats = [&] (auto proj) -> std::pair<double, double>
        {
            double mean = 0.0;
            for (const auto& f : elig) mean += proj (f);
            mean /= static_cast<double> (elig.size());
            double var = 0.0;
            for (const auto& f : elig)
            { const double d = proj (f) - mean; var += d * d; }
            var /= static_cast<double> (elig.size());
            const double sd = std::sqrt (var);
            return { mean, sd > 1e-9 ? sd : 1.0 };
        };
        const auto [lMean, lSd] = zStats ([] (const Feature& f) { return f.levelDb;  });
        const auto [zMean, zSd] = zStats ([] (const Feature& f) { return f.zcr;      });
        const auto [dMean, dSd] = zStats ([] (const Feature& f) { return f.durSec;   });
        const auto [pMean, pSd] = zStats ([] (const Feature& f) { return f.pitchLog; });

        const int m = static_cast<int> (elig.size());
        std::vector<std::array<double, 4>> vecs (static_cast<std::size_t> (m));
        for (int i = 0; i < m; ++i)
        {
            const auto& f = elig[static_cast<std::size_t> (i)];
            vecs[static_cast<std::size_t> (i)] = {
                (f.levelDb  - lMean) / lSd,
                (f.zcr      - zMean) / zSd,
                (f.durSec   - dMean) / dSd,
                (f.pitchLog - pMean) / pSd
            };
        }

        const auto dist2 = [&] (int a, int b)
        {
            double s = 0.0;
            for (int d = 0; d < 4; ++d)
            {
                const double diff = vecs[static_cast<std::size_t> (a)][static_cast<std::size_t> (d)]
                                  - vecs[static_cast<std::size_t> (b)][static_cast<std::size_t> (d)];
                s += diff * diff;
            }
            return s;
        };

        // Seed with the strongest (highest level) slice.
        int seed = 0;
        for (int i = 1; i < m; ++i)
            if (elig[static_cast<std::size_t> (i)].levelDb > elig[static_cast<std::size_t> (seed)].levelDb)
                seed = i;

        std::vector<int>    chosen { seed };
        std::vector<double> minD (static_cast<std::size_t> (m));
        for (int i = 0; i < m; ++i)
            minD[static_cast<std::size_t> (i)] = dist2 (i, seed);

        while (static_cast<int> (chosen.size()) < maxKeep)
        {
            int best = -1; double bestD = -1.0;
            for (int i = 0; i < m; ++i)
            {
                if (std::find (chosen.begin(), chosen.end(), i) != chosen.end())
                    continue;
                if (minD[static_cast<std::size_t> (i)] > bestD)
                { bestD = minD[static_cast<std::size_t> (i)]; best = i; }
            }
            if (best < 0)
                break;
            chosen.push_back (best);
            for (int i = 0; i < m; ++i)
                minD[static_cast<std::size_t> (i)] =
                    std::min (minD[static_cast<std::size_t> (i)], dist2 (i, best));
        }

        // ── 4. Emit kept slices in temporal order ────────────────────────────
        std::sort (chosen.begin(), chosen.end(), [&] (int a, int b)
        {
            return elig[static_cast<std::size_t> (a)].index
                 < elig[static_cast<std::size_t> (b)].index;
        });
        std::vector<Transient> out;
        out.reserve (chosen.size());
        for (int c : chosen)
            out.push_back (transients[static_cast<std::size_t> (elig[static_cast<std::size_t> (c)].index)]);
        transients = std::move (out);
    }
} // namespace switchblade::analysis
