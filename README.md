# Switchblade

**v0.5.0** · Art-Deco batch sample converter and intelligent slicer for Windows

> *"Brass, Neon, and Heavy Machinery"*

---

## What It Does

Drop in a folder of audio files. Switchblade analyses each one, classifies it as Percussive or Melodic, slices it at musically meaningful boundaries, and exports named WAV slices ready for your sampler — all in one pass.

- **Batch conversion** — WAV · AIFF · MP3 · FLAC · OGG → WAV
- **Three slicing modes** — Percussive (spectral flux onsets), Tonal (centroid stability), Auto (classifies and picks)
- **Melodic note naming** — slices get names like `SerumLead_C#3_01.wav`
- **Zero-crossing alignment** — every slice boundary is snapped to the nearest zero-crossing
- **Density Guard** — caps slice count to prevent over-slicing

---

## Quick Start

```bat
git clone https://github.com/mikei25-alt/The-Switchblade-Btach-Converter-and-cutter.git
cd The-Switchblade-Btach-Converter-and-cutter
setup.bat
```

`setup.bat` locates CMake, clones JUCE 8.0.6, configures the build, and opens Visual Studio. Then **Ctrl+Shift+B** → **Ctrl+F5**.

Requires: **Visual Studio 2019 / 2022 / 2026** with the *Desktop development with C++* workload.

---

## The Interface

Switchblade's UI follows a top-to-bottom production-line:

| Component | Role |
|---|---|
| **The Aperture** | Circular drag-and-drop zone — rotate blades open, drop files |
| **Sample Cards** | Scrolling queue with neon-filament waveforms and glass play buttons |
| **The Mechanism** | Triple-throw lever — Percussive / Auto / Tonal |
| **The Igniter** | Large ENGAGE plunger — starts analysis |
| **The Vault** | Gem cards for each output slice — preview and export |

---

## Analysis Modes

### Percussive
Spectral Flux onset detection:
1. Hann-windowed FFT per hop (2048-pt, 512 hop ≈ 11.6 ms)
2. Half-wave-rectified magnitude differences → novelty curve
3. Adaptive threshold (median + MAD, 65-frame window)
4. Local-maxima peak picker + silence gate
5. Zero-crossing snap (±5 ms)

### Tonal / Melodic
Centroid stability analysis:
1. Spectral centroid computed per frame
2. RingVariance O(1) sliding std-dev over configurable window
3. Stable-region transitions become slice candidates
4. PitchDetector (YIN) assigns note name to each slice

### Auto
Lightweight classifier on first 2 s:
- Onset rate from novelty curve
- YIN pitch clarity scan
- `pitchClarity > 0.40` **and** `onsetRate < 1.5 /s` → Melodic; otherwise Percussive

---

## Documentation

| Page | Contents |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | Full UI walkthrough and settings reference |
| [docs/analysis-architecture.md](docs/analysis-architecture.md) | Pipeline design, algorithm details |
| [docs/building.md](docs/building.md) | Full build instructions and CMake options |
| [docs/changelog.md](docs/changelog.md) | Version history |

---

## Building Tests

```bat
cmake --build build --target SwitchbladeTests --config Debug -j 4
build\tests\Debug\SwitchbladeTests.exe
```

36 GoogleTest cases covering every analysis component. All pass in ~7 s on Debug/MSVC.

---

## Requirements

- Windows 10 / 11 (64-bit)
- Visual Studio 2019, 2022, or 2026 — *Desktop development with C++* workload
- CMake 3.22+ (bundled with Visual Studio or install standalone)
- Internet connection on first build (downloads JUCE 8.0.6 + GoogleTest)

---

## License

Source available — see repository for terms.
