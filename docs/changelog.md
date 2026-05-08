# Changelog

All notable changes to Switchblade are documented here.

---

## v0.4.0 — 2026-05-08

### Analysis Engine — Performance

| Component | Change | Impact |
|---|---|---|
| **SpectralFlux** | Eliminated `curMag_` intermediate vector; branchless HWR (`std::max(0,x)` vectorises to MAXPS); half-fill scratch buffer (copy N then fill N zeros, was fill 2N then copy N) | ~26 M fewer float copies per 5-min file |
| **AdaptiveThreshold** | Pre-allocated mutable `scratch_` vector reused across both `std::sort` passes inside `compute()` | ~52 K heap allocations eliminated per file |
| **TextureAnalyzer** | `RingVariance` class with O(1) `push()` replaces `std::vector::erase(begin())` O(window) sliding variance | Linear → constant cost per frame for centroid stability |

### Analysis Engine — Correctness

- **Onset offset fix (TransientDetector)** — `rawSampleIndex` was `i*hop + fftN/4`, which placed onset estimates ~512 samples before the actual event start. The silence gate then checked a window that was entirely in the pre-burst silence, erasing valid candidates. Changed to `i*hop + fftN/2`: the spectral-flux local maximum occurs when the burst's energy centre passes the Hann-window midpoint, so `i*hop + N/2 ≈ burstStart` is the physically correct estimate.
- **Auto classifier tuned** — `pitchClarity` threshold lowered `0.60 → 0.40`; onset-rate gate `5.0 → 1.5 /s`. Triangle waves, piano one-shots, and vocal phrases now classify Melodic; dense drum loops with incidental tonal content stay Percussive.
- **computeSliceRms** helper added to AnalysisEngine for the Top-4 silent-slice guardrail.
- **SliceBoundary** — `computeNaturalEnds` fills the `naturalEnd` field on each transient using the energy-decay profile.

### UI

- **Top bar** height increased 48 → 80 px; brand area with logo and wordmark.
- **Melodic slice filenames** embed the detected MIDI note: `SerumLead_C#3_01.wav` instead of `SerumLead_mel_01.wav`.
- **SampleCard** — expanded waveform renderer and playback controls.
- **PreviewGrid**, **ResultTile**, **ResultsVault** — layout and paint polish.
- **Palette** — extended colour token set.
- **SwitchbladeLookAndFeel** — additional helper paint methods.

### Tests (new)

36 GoogleTest cases in `tests/`, covering every analysis component:

```
ZeroCrossing          4 tests
SpectralFlux          5 tests + 1 perf
AdaptiveThreshold     4 tests + 1 perf
TransientDetector     6 tests + 1 perf
TextureAnalyzer       4 tests + 1 perf
PitchDetector         5 tests + 1 perf
MixToMono             2 tests
EndToEnd              1 test
```

Performance baselines are set for Debug/MSVC /Od builds. Release (/O2 + AVX) beats all thresholds by 10–50×.

---

## v0.1.0 — Initial Release

- Batch WAV/AIFF/MP3/FLAC/OGG conversion
- Percussive onset detection via Spectral Flux + Adaptive Threshold
- Tonal region detection via centroid stability
- Auto-mode classifier
- Art-Deco UI: Aperture, Sample Cards, Mechanism lever, Igniter plunger, Vault
- Zero-crossing snap on all slice boundaries
- Density Guard and Slice Count Ceiling
