// =============================================================================
//  tempo_grid_test.cpp
//
//  Proof for the "Grid gave 83-second files" bug. The user loaded a long,
//  multi-bar four-on-the-floor kick track, chose Grid → 1/4, and expected each
//  kick sliced at quarter-note intervals. The old Grid divided the WHOLE file
//  into N==4 equal chunks → four ~83-second slices.
//
//  These tests build a synthetic four-on-the-floor kick track at a known tempo
//  and assert:
//    1. TempoEstimator recovers the BPM within tolerance.
//    2. buildGridTransients lays quarter-note-relative boundaries across the
//       whole file (many short slices), NOT N huge chunks.
//    3. A manual BPM override produces exact, evenly-spaced boundaries.
//    4. Un-pulsed material falls back to legacy whole-file/N division.
// =============================================================================

#include <gtest/gtest.h>

#include "Analysis/TempoEstimator.h"
#include "Analysis/GridSlicer.h"
#include "Analysis/GridCuration.h"
#include "Analysis/AudioFile.h"
#include "Analysis/SliceBoundary.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <vector>

namespace
{
    using namespace switchblade::analysis;

    constexpr double kSR = 44100.0;

    // Synthetic four-on-the-floor: a short decaying 60 Hz kick burst at the
    // start of every beat, `numBeats` beats at `bpm`, plus a little tail.
    [[nodiscard]] std::vector<float> fourOnTheFloor (double bpm, int numBeats)
    {
        const std::int64_t beat = static_cast<std::int64_t> (std::llround (60.0 / bpm * kSR));
        const std::int64_t len  = beat * numBeats + beat / 2;
        std::vector<float> x (static_cast<std::size_t> (len), 0.0f);

        const int   kickLen = static_cast<int> (0.060 * kSR);    // 60 ms
        const double w      = 2.0 * std::numbers::pi * 60.0 / kSR;
        for (int b = 0; b < numBeats; ++b)
        {
            const std::int64_t at = static_cast<std::int64_t> (b) * beat;
            for (int i = 0; i < kickLen && (at + i) < len; ++i)
            {
                const double env = std::exp (-static_cast<double> (i) / (0.012 * kSR));
                x[static_cast<std::size_t> (at + i)] +=
                    static_cast<float> (0.9 * env * std::sin (w * i));
            }
        }
        return x;
    }
} // namespace

TEST (TempoGrid, DetectsFourOnTheFloorTempo)
{
    const double bpm = 120.0;
    const auto   x   = fourOnTheFloor (bpm, 16);

    const auto est = TempoEstimator {}.estimate (std::span<const float> (x.data(), x.size()), kSR);

    ASSERT_TRUE (est.valid());
    EXPECT_NEAR (est.bpm, bpm, 3.0)
        << "detected " << est.bpm << " BPM, expected " << bpm;
    EXPECT_GT (est.confidence, 0.0f);
}

TEST (TempoGrid, QuarterGridSlicesWholeFileNotIntoFourChunks)
{
    const double bpm       = 120.0;
    const int    numBeats  = 16;
    const auto   x         = fourOnTheFloor (bpm, numBeats);
    const std::int64_t len = static_cast<std::int64_t> (x.size());

    // N == 4  →  one slice per quarter note (one per beat).
    const auto gb = buildGridTransients (
        std::span<const float> (x.data(), x.size()), kSR, len, 4, /*manualBpm*/ 0.0);

    // The OLD behaviour produced exactly 4 chunks; the fix produces ~one per
    // beat across the whole file.
    EXPECT_GT (gb.transients.size(), 8u)
        << "got only " << gb.transients.size() << " slices — looks like whole-file/N division";
    EXPECT_NEAR (static_cast<double> (gb.transients.size()), numBeats, 2.0);

    ASSERT_TRUE (gb.bpm.has_value());
    EXPECT_NEAR (*gb.bpm, bpm, 3.0);

    // No slice may be anywhere near the whole-file length (the 83-second bug).
    const std::int64_t beat = static_cast<std::int64_t> (std::llround (60.0 / bpm * kSR));
    for (std::size_t i = 0; i + 1 < gb.transients.size(); ++i)
    {
        const std::int64_t sliceLen =
            gb.transients[i + 1].sampleIndex - gb.transients[i].sampleIndex;
        EXPECT_LT (sliceLen, beat * 2)
            << "slice " << i << " spans " << sliceLen << " samples (>2 beats)";
    }
}

TEST (TempoGrid, ManualBpmOverrideGivesExactEvenSpacing)
{
    const double bpm       = 120.0;
    const auto   x         = fourOnTheFloor (bpm, 16);
    const std::int64_t len = static_cast<std::int64_t> (x.size());
    const std::int64_t beat = static_cast<std::int64_t> (std::llround (60.0 / bpm * kSR));

    // Manual BPM bypasses detection: raw (pre-snap) boundaries must be exactly
    // one beat apart for N == 4.
    const auto gb = buildGridTransients (
        std::span<const float> (x.data(), x.size()), kSR, len, 4, /*manualBpm*/ bpm);

    ASSERT_GT (gb.transients.size(), 8u);
    ASSERT_TRUE (gb.bpm.has_value());
    EXPECT_DOUBLE_EQ (*gb.bpm, bpm);

    for (std::size_t i = 0; i + 1 < gb.transients.size(); ++i)
    {
        const std::int64_t step =
            gb.transients[i + 1].rawSampleIndex - gb.transients[i].rawSampleIndex;
        EXPECT_EQ (step, beat) << "raw step at " << i << " was " << step;
    }
}

TEST (TempoGrid, FinerSubdivisionYieldsMoreSlices)
{
    const auto   x         = fourOnTheFloor (120.0, 16);
    const std::int64_t len = static_cast<std::int64_t> (x.size());
    const auto span        = std::span<const float> (x.data(), x.size());

    const auto q  = buildGridTransients (span, kSR, len, 4,  120.0);   // 1/4
    const auto s  = buildGridTransients (span, kSR, len, 16, 120.0);   // 1/16

    // 1/16 packs four times as many boundaries as 1/4.
    EXPECT_NEAR (static_cast<double> (s.transients.size()),
                 static_cast<double> (q.transients.size()) * 4.0,
                 4.0);
}

TEST (TempoGrid, UnpulsedMaterialFallsBackToWholeFileDivision)
{
    // Flat silence → no onsets, no detectable tempo. Grid must still yield N
    // boundaries (legacy whole-file/N behaviour) rather than nothing.
    std::vector<float> flat (static_cast<std::size_t> (kSR * 4), 0.0f);
    const std::int64_t len = static_cast<std::int64_t> (flat.size());

    const auto gb = buildGridTransients (
        std::span<const float> (flat.data(), flat.size()), kSR, len, 4, /*manualBpm*/ 0.0);

    EXPECT_EQ (gb.transients.size(), 4u);
    EXPECT_FALSE (gb.bpm.has_value());
}

// ── One-shot trimming of grid cells ─────────────────────────────────────────
namespace
{
    // A sequence of one decaying tone burst per cell. decayTau controls how fast
    // each note dies (small = short staccato note that leaves dead air before the
    // next grid line; huge = a note that rings through the whole cell and beyond).
    [[nodiscard]] AudioFile sequence (int cell, int numCells, double decayTau,
                                      double toneHz = 200.0)
    {
        AudioFile f;
        f.sampleRate              = kSR;
        const int len             = cell * numCells + cell / 2;
        f.originalLengthInSamples = len;
        f.samples.setSize (1, len);
        f.samples.clear();
        auto* w = f.samples.getWritePointer (0);
        const double wv = 2.0 * std::numbers::pi * toneHz / kSR;
        for (int c = 0; c < numCells; ++c)
        {
            const int at = c * cell;
            for (int i = 0; i < cell && (at + i) < len; ++i)
            {
                const double env = std::exp (-static_cast<double> (i) / decayTau);
                w[at + i] += static_cast<float> (0.8 * env * std::sin (wv * i));
            }
        }
        return f;
    }

    // Grid boundaries at exact cell multiples (what buildGridTransients lays for
    // an on-grid sequence); we exercise the trim pass directly here.
    [[nodiscard]] std::vector<Transient> gridAt (int cell, int numCells)
    {
        std::vector<Transient> ts;
        for (int c = 0; c < numCells; ++c)
        {
            Transient t;
            t.sampleIndex = static_cast<std::int64_t> (c) * cell;
            t.timeSeconds = t.sampleIndex / kSR;
            t.confidence  = 1.0f;
            ts.push_back (t);
        }
        return ts;
    }
} // namespace

TEST (TempoGrid, OneShotTrimRemovesDeadAirOnShortNotes)
{
    constexpr int cell     = 8000;                 // ~181 ms cell
    constexpr int numCells = 10;
    auto f  = sequence (cell, numCells, /*decayTau*/ 0.010 * kSR);   // ~58 ms note
    auto ts = gridAt (cell, numCells);

    trimGridSlicesToOneShots (f, ts);

    const std::int64_t gap = static_cast<std::int64_t> (0.020 * kSR);
    for (std::size_t i = 0; i + 1 < ts.size(); ++i)
    {
        const std::int64_t end     = ts[i].naturalEnd;
        const std::int64_t sliceLen = end - ts[i].sampleIndex;
        const std::int64_t nextStart = ts[i + 1].sampleIndex;

        ASSERT_GT (end, ts[i].sampleIndex) << "slice " << i << " is empty";
        // Dead air trimmed: a 58 ms note must not fill the 181 ms cell.
        EXPECT_LT (sliceLen, static_cast<std::int64_t> (cell / 2))
            << "slice " << i << " kept dead air: len " << sliceLen;
        // No bleed into the next cell.
        EXPECT_LE (end, nextStart - gap);
    }
}

TEST (TempoGrid, OneShotTrimCapsLongNotesAtNextGridLine)
{
    constexpr int cell     = 8000;
    constexpr int numCells = 8;
    // decayTau huge → effectively a sustained tone that never decays within a
    // cell, so the only thing that can end each one-shot is the next grid line.
    auto f  = sequence (cell, numCells, /*decayTau*/ 1000.0 * kSR);
    auto ts = gridAt (cell, numCells);

    trimGridSlicesToOneShots (f, ts);

    const std::int64_t gap = static_cast<std::int64_t> (0.020 * kSR);
    for (std::size_t i = 0; i + 1 < ts.size(); ++i)
    {
        const std::int64_t nextStart = ts[i + 1].sampleIndex;
        EXPECT_EQ (ts[i].naturalEnd, nextStart - gap)
            << "slice " << i << " not capped at the next grid line";
    }
}

// ── Filename BPM parsing ─────────────────────────────────────────────────────
TEST (TempoGrid, FilenameBpmParsing)
{
    using switchblade::analysis::parseBpmFromFilename;

    EXPECT_DOUBLE_EQ (*parseBpmFromFilename ("TSE_120_D_1_Tabla.wav"), 120.0);
    EXPECT_DOUBLE_EQ (*parseBpmFromFilename ("groove_140bpm.wav"),     140.0);
    EXPECT_DOUBLE_EQ (*parseBpmFromFilename ("Loop 128 BPM Cmaj.wav"), 128.0);
    EXPECT_DOUBLE_EQ (*parseBpmFromFilename ("bpm90_kit.wav"),          90.0);
    EXPECT_DOUBLE_EQ (*parseBpmFromFilename ("Tabla_100.wav"),         100.0);

    EXPECT_FALSE (parseBpmFromFilename ("Kick_01.wav").has_value());          // 1 < 40
    EXPECT_FALSE (parseBpmFromFilename ("Track_85_120_mix.wav").has_value()); // ambiguous
    EXPECT_FALSE (parseBpmFromFilename ("master_2023.wav").has_value());      // > 300
    EXPECT_FALSE (parseBpmFromFilename ("Snare.wav").has_value());            // no number
}

// ── Grid curation (keep N strongest + most distinct one-shots) ───────────────
namespace
{
    // Build a file of `numCells` cells. Cells 0..numCells-3 are an identical
    // 200 Hz tone; the penultimate cell is an 800 Hz tone (pitch/brightness
    // outlier) and the last is broadband noise (unpitched, high zero-crossing
    // outlier). Each note occupies the first `noteLen` samples of its cell.
    [[nodiscard]] AudioFile cellsWithTwoOutliers (int cell, int numCells, int noteLen)
    {
        AudioFile f;
        f.sampleRate              = kSR;
        const int len             = cell * numCells;
        f.originalLengthInSamples = len;
        f.samples.setSize (1, len);
        f.samples.clear();
        auto* w = f.samples.getWritePointer (0);

        const int     brightCell = numCells - 2;
        const int     noiseCell  = numCells - 1;
        const double  w200 = 2.0 * std::numbers::pi * 200.0 / kSR;
        const double  w800 = 2.0 * std::numbers::pi * 800.0 / kSR;
        std::uint32_t seed = 2463534242u;

        for (int c = 0; c < numCells; ++c)
        {
            const int at = c * cell;
            for (int i = 0; i < noteLen && (at + i) < len; ++i)
            {
                float s;
                if (c == noiseCell)
                {
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    s = (static_cast<float> (seed) / 2147483648.0f - 1.0f);
                }
                else
                {
                    const double wv = (c == brightCell) ? w800 : w200;
                    s = static_cast<float> (std::sin (wv * i));
                }
                w[at + i] += 0.8f * s;
            }
        }
        return f;
    }

    [[nodiscard]] std::set<int> keptCells (const std::vector<Transient>& ts, int cell)
    {
        std::set<int> cells;
        for (const auto& t : ts)
            cells.insert (static_cast<int> (t.sampleIndex / cell));
        return cells;
    }
} // namespace

TEST (TempoGrid, CuratePicksDistinctOutliers)
{
    constexpr int cell     = 16000;
    constexpr int numCells = 8;
    constexpr int noteLen  = 9000;
    auto f  = cellsWithTwoOutliers (cell, numCells, noteLen);
    auto ts = gridAt (cell, numCells);
    for (auto& t : ts) t.naturalEnd = t.sampleIndex + noteLen;

    curateGridOneShots (f, ts, /*maxKeep*/ 3);

    EXPECT_EQ (ts.size(), 3u);
    const auto cells = keptCells (ts, cell);
    // The two distinct outliers (800 Hz tone + noise) must survive curation.
    EXPECT_TRUE (cells.count (numCells - 2)) << "bright 800 Hz cell was dropped";
    EXPECT_TRUE (cells.count (numCells - 1)) << "noise cell was dropped";
}

TEST (TempoGrid, CurateRespectsMaxCount)
{
    constexpr int cell     = 16000;
    constexpr int numCells = 8;
    constexpr int noteLen  = 9000;
    auto f  = cellsWithTwoOutliers (cell, numCells, noteLen);
    auto ts = gridAt (cell, numCells);
    for (auto& t : ts) t.naturalEnd = t.sampleIndex + noteLen;

    curateGridOneShots (f, ts, /*maxKeep*/ 2);
    EXPECT_EQ (ts.size(), 2u);
}

TEST (TempoGrid, CurateKeepsAllWhenCapExceedsCount)
{
    constexpr int cell     = 16000;
    constexpr int numCells = 6;
    constexpr int noteLen  = 9000;
    auto f  = cellsWithTwoOutliers (cell, numCells, noteLen);
    auto ts = gridAt (cell, numCells);
    for (auto& t : ts) t.naturalEnd = t.sampleIndex + noteLen;

    curateGridOneShots (f, ts, /*maxKeep*/ 100);
    EXPECT_EQ (ts.size(), static_cast<std::size_t> (numCells));   // none are silent
}

TEST (TempoGrid, CurateDropsSilentCells)
{
    constexpr int cell     = 16000;
    constexpr int numCells = 5;
    constexpr int noteLen  = 9000;
    // 4 tone cells + 1 fully silent cell (index 2).
    AudioFile f;
    f.sampleRate              = kSR;
    const int len             = cell * numCells;
    f.originalLengthInSamples = len;
    f.samples.setSize (1, len);
    f.samples.clear();
    auto* w = f.samples.getWritePointer (0);
    const double wv = 2.0 * std::numbers::pi * 220.0 / kSR;
    for (int c = 0; c < numCells; ++c)
    {
        if (c == 2) continue;                       // silent cell
        const int at = c * cell;
        for (int i = 0; i < noteLen; ++i)
            w[at + i] += 0.8f * static_cast<float> (std::sin (wv * i));
    }
    auto ts = gridAt (cell, numCells);
    for (auto& t : ts) t.naturalEnd = t.sampleIndex + noteLen;

    curateGridOneShots (f, ts, /*maxKeep*/ 100);   // cap exceeds count → only silence drops
    const auto cells = keptCells (ts, cell);
    EXPECT_EQ (ts.size(), 4u);
    EXPECT_FALSE (cells.count (2)) << "silent cell should have been dropped";
}
