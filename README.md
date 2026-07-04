# Switchblade

**v0.8.0** · Art-Deco batch sample converter and intelligent slicer for Windows, macOS, and Linux

> *"Brass, Neon, and Heavy Machinery"*

---

## What It Does

Drop in a folder of audio files. Switchblade analyses each one, classifies it, slices it at musically meaningful boundaries, and exports named WAV slices ready for your sampler — all in one pass.

- **Batch conversion** — WAV · AIFF · MP3 · FLAC · OGG → 24-bit WAV
- **Five slicing modes** — Auto · Percussive · Melodic · Texture · Grid (tempo-relative musical subdivisions, triplets included)
- **Per-slice note naming** — cents-aware names like `SerumLead_C#3+12c_01.wav`, plus ACID root-note and BWAV metadata
- **Editable slice markers** — drag to nudge, double-click to add or delete
- **Drag to DAW** — drag any result tile straight into your DAW; slices are pre-rendered in the background so the handoff is instant
- **Zero-crossing alignment** — every boundary snaps to the nearest zero-crossing
- **Density Guard** — 64-slice ceiling prevents over-slicing

---

## Install

Prebuilt, unsigned installers are produced by the **Build installers** GitHub Actions workflow (also attached to GitHub Releases on version tags):

| Platform | Package | Note |
|---|---|---|
| Windows 10/11 (x64) | `TheSwitchblade-…-Windows-Setup.exe` | SmartScreen warns on first run — *More info → Run anyway* |
| macOS 11+ (Apple Silicon + Intel) | `TheSwitchblade-macOS.dmg` | Unsigned — right-click the app → *Open* the first time |

Or build from source (below).

---

## Quick Start (from source)

**Windows** — `setup.bat` locates CMake, clones JUCE, configures, and opens Visual Studio. Then **Ctrl+Shift+B** → **Ctrl+F5**.

**macOS / Linux:**

```bash
git clone https://github.com/mikei25-alt/The-Switchblade-Btach-Converter-and-cutter.git
cd The-Switchblade-Btach-Converter-and-cutter
git clone --depth 1 https://github.com/juce-framework/JUCE.git External/JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Switchblade
```

Linux needs the usual JUCE dev packages (ALSA, X11/Xrandr/Xinerama/Xcursor, FreeType, fontconfig, Mesa GL, GTK3/WebKit for the build helper). See [docs/building.md](docs/building.md).

---

## The Interface

| Component | Role |
|---|---|
| **Drop Zone** | Drag audio files or folders anywhere onto the window |
| **Sample Cards** | Scrolling queue — neon waveform, play button, classification badge (click to override the mode per file), draggable/editable slice markers, Cmd/Ctrl+wheel zoom |
| **Mode Combo + Slider** | Auto / Percussive / Melodic / Texture / Grid; the slider is detection sensitivity (or musical subdivision in Grid mode, with BPM + Max fields) |
| **Produce** | The single primary action — exports every slice (or the vault selection); right-click sets the normalization level |
| **Preview Grid** | 4×4 pad grid — click or play with keys `1-4 / QWER / ASDF / ZXCV` |
| **Results Vault** | One tile per slice — click to audition, Cmd/Ctrl+click to arm for Export Selection, drag out to your DAW, CLEAR to compact |

**Keyboard:** Space = preview selected card · Up/Down = walk the card list · Cmd/Ctrl+A = arm all tiles · Esc = stop + clear selections · Delete = remove card(s).

---

## Analysis Modes

| Mode | Method |
|---|---|
| **Percussive** | Spectral-flux novelty (2048-pt FFT, 512 hop) → median+MAD adaptive threshold → peak picking → silence gate → zero-snap |
| **Melodic** | Pitch-continuity note segmentation (frame-wise YIN) with transient fallback; per-slice YIN gives each export its own cents-aware note name |
| **Texture** | Spectral-centroid stability — fires where the spectrum *settles*, ideal for granular material |
| **Grid** | Tempo-relative subdivisions (1/2 … 1/64 incl. triplets). Tempo from filename → manual field → autocorrelation estimator with octave-error correction; cells trimmed to one-shots, optional curation to the N most distinct hits |
| **Auto** | Classifier over onset rate + YIN clarity dispatches to the right mode per file |

The YIN difference function is FFT-accelerated (O(W log W)) — a full-file pitch scan of 30 s of audio takes ~140 ms in Release.

---

## Documentation

| Page | Contents |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | Full UI walkthrough and settings reference |
| [docs/analysis-architecture.md](docs/analysis-architecture.md) | Pipeline design, algorithm details |
| [docs/building.md](docs/building.md) | Build instructions for all platforms, CMake options, installers |
| [docs/changelog.md](docs/changelog.md) | Version history |

---

## Tests

```bash
cmake --build build --target SwitchbladeTests
ctest --test-dir build
```

76 GoogleTest cases covering every analysis component, including performance budgets and a 24-note pitch-resolution sweep. All pass on MSVC and GCC, Debug and Release.

---

## Requirements

- Windows 10/11 (x64), macOS 11+, or a modern Linux
- A C++20 compiler — MSVC 2019+/GCC 13+/recent Clang — and CMake 3.22+
- Internet on first configure (clones JUCE; tests fetch GoogleTest)

---

## License

Source available — see repository for terms.
