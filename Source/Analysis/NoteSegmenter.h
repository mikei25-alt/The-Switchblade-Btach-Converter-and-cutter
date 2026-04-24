#pragma once

#include "Analysis/AudioFile.h"
#include "Analysis/TransientDetector.h"
#include "Analysis/PitchDetector.h"

#include <cmath>
#include <vector>

namespace switchblade::analysis
{
    //==========================================================================
    //  segmentNotes — pitch-continuity based note onset detector.
    //
    //  Purpose
    //  -------
    //  When material is melodic / mellow and note attacks are soft (violin,
    //  flute, voice, pads), amplitude-transient detectors miss note boundaries.
    //  This routine tracks pitch frame-by-frame and segments at:
    //    • voiced → unvoiced transitions  (note release)
    //    • unvoiced → voiced transitions  (note attack)
    //    • MIDI-note jumps ≥ midiJumpSemitones between consecutive voiced frames
    //
    //  Each returned Transient has sampleIndex = note onset.  naturalEnd is
    //  left at 0 so that computeNaturalEnds() (SliceBoundary.h) fills it in
    //  from the energy-decay envelope, capturing the full release + reverb tail.
    //
    //  Parameters
    //  ----------
    //  clarityThresh     YIN clarity ≥ this → "voiced".  Lower = more sensitive.
    //  midiJumpSemitones Whole MIDI-semitone jump to split a note.  Value of 2
    //                    catches all diatonic steps without tripping on vibrato
    //                    (≤ 1 semitone) or pitch estimation noise.
    //  minNoteSec        Discard detections shorter than this (suppresses noise).
    //  silenceDb         RMS floor relative to global peak; frames below floor
    //                    are treated as unvoiced regardless of YIN clarity.
    //  voiceOnCount      Number of consecutive voiced frames needed to commit
    //                    to "note started" — prevents single-frame false positives.
    //  voiceOffCount     Consecutive unvoiced frames before declaring note ended
    //                    — allows brief pitch gaps (e.g. glottals, bow changes).
    //==========================================================================
    inline std::vector<Transient> segmentNotes (
        const AudioFile& file,
        float clarityThresh     = 0.45f,
        int   midiJumpSemitones = 2,
        float minNoteSec        = 0.04f,
        float silenceDb         = -52.0f,
        int   voiceOnCount      = 2,
        int   voiceOffCount     = 3)
    {
        if (! file.isValid())
            return {};

        std::vector<float> mono;
        mixToMono (file.samples, mono);
        if (mono.empty())
            return {};

        const int sr          = static_cast<int> (std::round (file.sampleRate));
        const int frameSize   = 2048;
        const int hopSize     = 512;   // ≈ 11.6 ms at 44100 Hz
        const int totalS      = static_cast<int> (mono.size());
        const int minNoteSamp = std::max (hopSize * voiceOnCount,
                                          static_cast<int> (minNoteSec * sr));

        // ── Global RMS peak for noise-floor computation ──────────────────────
        float globalPeak = 0.0f;
        for (float v : mono)
            globalPeak = std::max (globalPeak, std::abs (v));
        const float noiseFloor = globalPeak * std::pow (10.0f, silenceDb / 20.0f);

        // ── Per-frame pitch/RMS analysis ─────────────────────────────────────
        struct Frame { float rms { 0.0f }; float f0Hz { 0.0f }; float clarity { 0.0f }; };

        const int numFrames = std::max (1, (totalS - frameSize) / hopSize + 1);
        std::vector<Frame> frames;
        frames.reserve (static_cast<std::size_t> (numFrames));

        PitchDetector pd { PitchDetector::Config { .frameSize = frameSize } };

        for (int pos = 0; pos + frameSize <= totalS; pos += hopSize)
        {
            Frame fr;
            float sum = 0.0f;
            for (int i = 0; i < frameSize; ++i)
                sum += mono[pos + i] * mono[pos + i];
            fr.rms = std::sqrt (sum / static_cast<float> (frameSize));

            if (fr.rms >= noiseFloor)
            {
                auto pr = pd.detect (
                    std::span<const float> (mono.data() + pos, frameSize),
                    file.sampleRate);
                if (pr.has_value())
                {
                    fr.f0Hz    = pr->f0Hz;
                    fr.clarity = pr->clarity;
                }
            }
            frames.push_back (fr);
        }

        // ── State machine ────────────────────────────────────────────────────
        // States: SILENT | ENTERING (accumulating voiceOnCount) | VOICED
        enum class State { Silent, Entering, Voiced } state = State::Silent;

        std::vector<Transient> notes;
        int   voicedRun   = 0;     // consecutive voiced frames in ENTERING
        int   unvoicedRun = 0;     // consecutive unvoiced frames in VOICED
        int   noteOnsetF  = 0;     // frame index where current note started
        int   prevMidi    = -1;    // MIDI note of previous voiced frame

        auto isVoiced = [&] (const Frame& fr) -> bool
        {
            return fr.rms >= noiseFloor && fr.clarity >= clarityThresh && fr.f0Hz > 0.0f;
        };

        auto commitNote = [&] (int onsetF, int offsetF) -> void
        {
            const int s0 = std::min (onsetF  * hopSize, totalS);
            const int s1 = std::min (offsetF * hopSize, totalS);
            if (s1 - s0 < minNoteSamp)
                return;

            Transient t;
            t.sampleIndex    = static_cast<juce::int64> (s0);
            t.rawSampleIndex = t.sampleIndex;
            t.naturalEnd     = 0;   // filled in by computeNaturalEnds()
            t.timeSeconds    = static_cast<double> (s0) / sr;
            t.confidence     = 1.0f;
            notes.push_back (t);
        };

        const int nf = static_cast<int> (frames.size());

        for (int fi = 0; fi < nf; ++fi)
        {
            const Frame& fr    = frames[static_cast<std::size_t> (fi)];
            const bool   v     = isVoiced (fr);
            const int    midi  = (v && fr.f0Hz > 0.0f)
                                 ? PitchDetector::midiNoteFromHz (fr.f0Hz)
                                 : -1;

            switch (state)
            {
                case State::Silent:
                    if (v)
                    {
                        state       = State::Entering;
                        voicedRun   = 1;
                        noteOnsetF  = fi;
                        prevMidi    = midi;
                    }
                    break;

                case State::Entering:
                    if (v)
                    {
                        ++voicedRun;
                        prevMidi = midi;
                        if (voicedRun >= voiceOnCount)
                            state = State::Voiced;
                    }
                    else
                    {
                        // Didn't confirm — reset
                        state      = State::Silent;
                        voicedRun  = 0;
                        prevMidi   = -1;
                    }
                    break;

                case State::Voiced:
                    if (! v)
                    {
                        ++unvoicedRun;
                        if (unvoicedRun >= voiceOffCount)
                        {
                            // Close current note at the first unvoiced frame
                            commitNote (noteOnsetF, fi - unvoicedRun + 1);
                            state        = State::Silent;
                            unvoicedRun  = 0;
                            voicedRun    = 0;
                            prevMidi     = -1;
                        }
                    }
                    else
                    {
                        unvoicedRun = 0;

                        // MIDI-note jump → end current note, start a new one
                        if (prevMidi >= 0 && midi >= 0
                            && std::abs (midi - prevMidi) >= midiJumpSemitones)
                        {
                            commitNote (noteOnsetF, fi);
                            noteOnsetF = fi;
                            // Stay in VOICED for the new pitch
                        }
                        prevMidi = midi;
                    }
                    break;
            }
        }

        // ── Close any note still open at end of file ─────────────────────────
        if (state == State::Voiced || state == State::Entering)
            commitNote (noteOnsetF, nf);

        return notes;
    }
} // namespace switchblade::analysis
