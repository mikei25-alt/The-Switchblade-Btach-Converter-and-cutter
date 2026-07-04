# Changelog

All notable changes to Switchblade are documented here.

---

## v0.8.0 — 2026-07-04

### Analysis Engine — Speed

| Component | Change | Impact (Release, same hardware) |
|---|---|---|
| **PitchDetector (YIN)** | Difference function computed via FFT autocorrelation (`d = P + Q − 2R` with prefix-sum energies) instead of the O(W·τmax) direct loop; exact double-precision recompute at the dip preserves cents-level accuracy | 100-frame perf test 84 → 12 ms; per-slice detection ~7×; worst-case 30 s file-wide scan ~1 s → 141 ms. Speeds up Auto classification, Melodic segmentation, Grid labelling, and curation |
| **AdaptiveThreshold** | Two full sorts per novelty frame replaced with two `std::nth_element` selections (value-identical median/MAD) | 50k-call perf test 117 → 67 ms |
| **NoteSegmenter** | Frame RMS via per-hop prefix sums instead of re-summing 75 %-overlapping windows | O(1) per frame, tiny memory |
| **ZeroCrossing / mixToMono / GridCuration / TextureAnalyzer** | Outward early-exit crossing search; memcpy/SIMD mono mixdown; hoisted channel pointers; single std-dev pass | Assorted hot-path wins |

### Analysis Engine — Accuracy

- **TempoEstimator**: overlap-normalised (unbiased) autocorrelation removes the inherent fast-tempo bias; 3-point Hann smoothing of the novelty fixes half-tempo errors from fractional-frame beat periods (e.g. 150 BPM at 512-hop); harmonic reinforcement plus a calibrated half-lag octave check fix backbeat half-tempo errors. Synthetic benchmark: **17/17** correct vs 14/17 before, no regressions.
- Pitch accuracy preserved exactly: the 24-note sweep still resolves every note to the cent, including 41 Hz low bass and ±12-cent detuning.
- `AnalysisResult` now carries the loaded audio, so the UI no longer re-reads each file from disk on the message thread.

### UI / UX

- **Responsive top bar** — control widths squeeze uniformly when space is tight; the preview grid yields height so the vault keeps ≥ 55 % of the right panel; minimum window is now 1000×560 (the old 800×500 could never fit the header, and Grid mode overflowed even at the 1280 default).
- **Background exports with progress** — Produce / Export Selection / per-card Extract run on a worker pool with a live `Exporting n/N` readout and disabled buttons, instead of freezing the UI for per-slice pitch detection + WAV writes.
- **Slice-marker editing** — double-click empty waveform adds a zero-crossing-snapped marker (64-slice ceiling enforced); double-click a marker deletes it; zoom reset moved to clicking the zoom badge.
- **Failure states** — failed analyses show a crimson ANALYSIS FAILED card with the reason; the batch summary reports `N FAILED`; the badge dropdown doubles as a retry control.
- **macOS multi-select fixed** — Cmd+click (Ctrl+click is the popup gesture on macOS and opened the delete menu instead); Ctrl elsewhere.
- **Mode changes re-analyse loaded cards** — switching the mode combo re-slices everything live, matching Grid-tweak behaviour.
- **In-place vault updates** — marker edits replace only the affected file's tiles (same position/numbering, selection restored); previously any nudge cleared the whole vault and the user's selection.
- **Vault CLEAR ("Trash Compactor")** with confirm; the "Don't ask me again" delete preference is now reversible from the card context menu.
- **Keyboard** — Space previews the selected card, Up/Down walk the card list, Cmd/Ctrl+A arms all vault tiles, Esc stops playback and clears selections (alongside existing Delete and the 1-4/QWER/ASDF/ZXCV pads).
- **"Extract All" removed** — it was an exact duplicate of Produce; Produce is the single primary action and carries the ANALYZING… indicator.
- Polish: normalization right-click surfaced via tooltips; EXPORT COLLECTION is a real button; drag-out to a DAW starts only from the card header grip (grab cursor) and its re-ingestion guard now works on macOS/Linux; classification colours unified between cards and vault tiles; hover cursors; plain wheel scrolls the card list while Cmd/Ctrl+wheel zooms with a fixed anchor; pad-grid edge dead zones removed.
- Removed the window-wide OpenGL context and the never-instantiated FrostedGlassPanel / NeonBloomShader — the frosted/bloom look is painted per-component in software.

### Packaging & Portability

- **App icon** — `Source/Assets/logo.png` embedded as the real application icon (`.ico` on Windows, `.icns` in the macOS bundle).
- **Installers** — GitHub Actions workflow builds a Windows setup `.exe` (Inno Setup) and a macOS `.dmg` (universal arm64 + x86_64); version tags (`v*`) attach both to a GitHub Release. Binaries are unsigned: SmartScreen/Gatekeeper warn on first launch.
- **Linux/GCC build fixed** — GCC-incompatible `= {}` default-constructor arguments in five headers, an MSVC-only linker flag applied unconditionally, and a missing top-level C language declaration all blocked non-MSVC builds entirely.

### Tests

76 GoogleTest cases (was 71): new coverage for NoteSegmenter (previously untested), tempo detection across 90–174 BPM, FFT-vs-direct YIN path agreement, and a worst-case file-wide pitch-scan perf budget. All pass in Debug and Release on Linux/GCC.

---

## v0.6.0 – v0.7.1 — 2026-06

Tagged without changelog entries; highlights from the commit history: tempo-relative **Grid mode** (musical subdivisions with triplets, filename-BPM parsing, manual BPM override, one-shot trimming, variety-based curation), per-slice cents-aware note naming (`A4+12c`), background pre-render of vault tiles for instant drag-to-DAW, and the synthetic-tone pitch-resolution proof suite.

---

## v0.5.0 — 2026-05-10

### Pitch Detection — Per-Slice Accuracy

| Component | Change | Impact |
|---|---|---|
| **PitchDetector** | Default `frameSize` raised 2048 → 4096 | Better low-end resolution; reliable note detection on cello/bass material |
| **PitchDetector** | New `noiseFloor` config (default −45 dBFS); `detect()` short-circuits on near-silent frames | Quiet slices no longer get spurious low-note labels |
| **PitchDetector** | New free function `detectSlicePitchHz(file, start, end)` — runs YIN on a specific slice region, skips ~10% past the attack into the sustain | Each slice is analysed in isolation instead of inheriting the file's first-frame pitch |
| **MainContainer / ResultsVault** | All three export paths and the vault display now call `detectSlicePitchHz` per slice | Multi-note recordings (e.g. cello phrases) produce distinct per-slice note labels in both the on-screen badges and the exported WAV filenames |

Bug closed: a multi-note cello recording sliced into four pieces was previously labelled `…_E2_…` on every output, because pitch was detected once on the first confident frame and reused. Each slice is now its own analysis frame.

### UI — Header Reconstruction

A multi-pass overhaul guided by a series of design specs. Final state:

- **Branding pod** — 50 px logo with neon-cyan `DropShadow` glow, fixed 15 px gap to a two-line wordmark ("THE" / "SWITCHBLADE" at 14 pt boldened, 0.25f kerning), wordmark group vertically centred on the logo's mid-point.
- **Centre pod** — Mode combo, "SENS" label, sensitivity slider hard-capped at 150 px, slider thumb scaled to 28 px row height (no more giant pills). The whole group is horizontally centred between brand and right pod.
- **Right pod** — `[N] ARMED` counter in Neon Gold (12.5 pt boldened, primary visual anchor), then four 120 × 32 buttons (Source folder / Extract All / Produce / Export Selection at 150 px). Export Selection has a dark-gold tint fill and gold border at rest.
- **Buttons** — Flat matte gradient `#1A1A1A → #0F0F0F`, `#333333` border at rest, `#00FBFF` border on hover with a 1 px cyan top-accent line.
- **Header sentinel border** — solid `#00FBFF` 1 px line, 20 px L/R outer padding so content doesn't choke window edges.

### UI — Bug Fixes

- **`SwitchbladeLookAndFeel::getLabelFont`** was returning `label.getHeight() * 0.6f`, which evaluated to ~36 pt for a full-height bar label and silently overrode every `setFont(10pt)` call in the codebase. JUCE's default `LookAndFeel_V2::drawLabel` queries `getLabelFont`, **not** `label.getFont()`, so all explicit font sizing was being thrown away. Header labels rendered at the wrong size and `drawFittedText` then truncated them, producing the `…` ellipses the user kept reporting on `SENS` and `[0] ARMED`. Fixed by returning `label.getFont()`, matching the default behaviour.
- **`EllipsisLabel`** — now uses `drawFittedText` with `minimumHorizontalScale = 1.0f` instead of injecting a `…` glyph. Per UI policy, header text never shows truncation marks; if the layout is genuinely too tight, text simply clips.
- **Bullet character** in the Produce/Export Selection normalisation suffix was raw bytes interpreted as Windows-1252; switched to `juce::String::fromUTF8` so it renders as an actual `•`.

### File / Format Support

- **Output folder picker** — new "Source folder" button in the header opens an async `juce::FileChooser`. Right-click resets to "next to source file" (the previous default). Used by all three export paths.
- **External drag-out** — vault tiles can be dragged out of the application window directly into Explorer, the file system, or another DAW. Multi-selected tiles drag together. Slices are rendered to temp WAVs in `%TEMP%\sb_drag_<index>.wav` and cleaned up after the drop completes.
- **MP4 ingest** — `juce::WindowsMediaAudioFormat` is registered alongside the basic format manager, so `.mp4` files (audio track only) can be loaded for analysis on Windows. Output is still WAV.

### Counter Restyle

- "0 selected" → `[N] ARMED` in Neon Gold, 12.5 pt boldened with kerning. Right-aligned at fixed 96 px.

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
