# Switchblade — User Guide

**Version 0.4.0** · Art-Deco batch sample converter and intelligent slicer

---

## Table of Contents

1. [Overview](#overview)
2. [Getting Started](#getting-started)
3. [The Production Line](#the-production-line)
   - [The Aperture — Loading Files](#the-aperture--loading-files)
   - [Sample Cards — Reviewing Your Queue](#sample-cards--reviewing-your-queue)
   - [The Mechanism — Choosing a Mode](#the-mechanism--choosing-a-mode)
   - [The Igniter — Running Analysis](#the-igniter--running-analysis)
   - [The Vault — Exporting Results](#the-vault--exporting-results)
4. [Analysis Modes](#analysis-modes)
5. [Slice Naming](#slice-naming)
6. [Settings Reference](#settings-reference)
7. [Building from Source](#building-from-source)
8. [Changelog](Changelog)

---

## Overview

Switchblade is a desktop tool for producers who need to batch-convert audio files and automatically slice them into usable samples. Drop in a folder of loops, one-shots, or mixed material — Switchblade analyses each file, classifies it (Percussive / Melodic / Auto), slices it at musically meaningful boundaries, and exports named WAV slices ready for your sampler.

The interface is styled after Art-Deco industrial machinery: brass frames, neon filaments, and heavy physical controls.

---

## Getting Started

### Requirements
- Windows 10 / 11 (64-bit)
- Visual Studio 2019, 2022, or 2026 with **Desktop development with C++** workload
- Internet connection for first build (fetches JUCE 8.0.6 and GoogleTest via CMake FetchContent)

### First Run (from source)

```
git clone https://github.com/mikei25-alt/The-Switchblade-Btach-Converter-and-cutter.git
cd The-Switchblade-Btach-Converter-and-cutter
setup.bat
```

`setup.bat` will:
1. Locate CMake (PATH or VS-bundled)
2. Clone JUCE 8.0.6 into `External/JUCE`
3. Configure the CMake build in `build/`
4. Open the solution in Visual Studio

Then press **Ctrl+Shift+B** (Build Solution), then **Ctrl+F5** (Start Without Debugging).

---

## The Production Line

Switchblade's UI follows a top-to-bottom production-line metaphor.

### The Aperture — Loading Files

The large circular panel at the top of the window is **The Aperture**. Drag audio files or folders directly onto it. The blades rotate open on hover and a ripple effect confirms a successful drop.

**Supported formats:** WAV · AIFF · MP3 · FLAC · OGG

You can drop a single file, a multi-file selection, or an entire folder. Subfolders are scanned recursively.

---

### Sample Cards — Reviewing Your Queue

Each loaded file appears as a **Sample Card** in the central scrolling strip. Cards show:

| Element | Meaning |
|---|---|
| **Waveform** | "Neon Filament" waveform drawn in real time |
| **Play button** | Glass jewel button — click to preview in-app |
| **Duration / Format** | File metadata |
| **Classification badge** | Set after analysis (Percussive / Melodic / Auto) |

Cards can be dismissed individually with the × button before analysis.

---

### The Mechanism — Choosing a Mode

The triple-throw lever to the right of the aperture selects the slicing algorithm:

| Position | Mode | Best For |
|---|---|---|
| **Left** | **Percussive** | Drum loops, one-shots, rhythmic content — uses Spectral Flux onset detection |
| **Centre** | **Auto** | Mixed or unknown material — classifies each file and picks the best algorithm |
| **Right** | **Tonal** | Sustained notes, pads, melodic phrases — uses centroid stability analysis |

---

### The Igniter — Running Analysis

The large circular **ENGAGE** plunger starts the analysis pipeline. Press it once all files are loaded and a mode is selected. A neon power-surge animation travels down toward the sample cards as each file is processed.

Progress is shown per-card. You can add more files while analysis is running.

---

### The Vault — Exporting Results

Processed slices appear as small **gem cards** in The Vault at the bottom of the window. Each gem card shows:
- Slice number and detected pitch (Melodic mode)
- Duration
- Confidence score

**Exporting:** Click any gem card to hear the slice. Click **Export All** to write all slices to a folder you choose. The **Trash Compactor** lever clears the vault.

---

## Analysis Modes

### Percussive Mode

Uses **Spectral Flux** onset detection:

1. Audio is mixed to mono and split into 2048-sample frames (hop 512).
2. Each frame is Hann-windowed and FFT'd.
3. Half-wave rectified magnitude differences between consecutive frames form the **novelty curve**.
4. An **Adaptive Threshold** (sliding-window median + MAD) gates the novelty curve.
5. Local maxima above the threshold become slice candidates.
6. A silence gate (-40 dBFS) removes false triggers in quiet passages.
7. Each onset is snapped to the nearest zero-crossing (±5 ms search radius).

**Density Guard** caps percussive slices at 8 per file (4/sec for files longer than 2 seconds), preventing over-slicing on dense loops.

### Tonal / Melodic Mode

Uses **TextureAnalyzer** centroid stability:

1. Spectral centroid is computed per frame.
2. A ring-buffer sliding std-dev measures centroid stability over a configurable window.
3. Transitions from unstable → stable spectral content mark region boundaries.
4. Each region is zero-crossing snapped.

In Melodic mode, **PitchDetector** (YIN algorithm) estimates the fundamental frequency of each slice. The detected pitch is embedded in the output filename.

### Auto Mode

Runs a lightweight classifier on the first 2 seconds:

- **Onset rate** is estimated from the novelty curve.
- A **YIN pitch scan** over several frames returns the best clarity score.
- Files with `pitchClarity > 0.40` **and** `onsetRate < 1.5 /s` are classified Melodic; all others are Percussive.
- The appropriate algorithm is then applied.

---

## Slice Naming

Output files follow this pattern:

| Mode | Pattern | Example |
|---|---|---|
| Percussive / Tonal | `[stem]_[tag]_[index].wav` | `drums_perc_01.wav` |
| Melodic (pitch detected) | `[stem]_[Note]_[index].wav` | `SerumLead_C#3_01.wav` |

`[stem]` is the source filename without extension. `[index]` is zero-padded to 2 digits.

---

## Settings Reference

Settings are accessible via the gear icon in the top bar.

| Setting | Default | Description |
|---|---|---|
| **Min Spacing** | 200 ms | Minimum gap between two consecutive onset candidates |
| **Sensitivity** | 0.7 | Onset detection sensitivity (0.5 = strict, 2.0 = loose) |
| **RMS Floor** | –40 dBFS | Silence gate threshold — onsets below this are rejected |
| **Zero-Snap Radius** | 5 ms | Search radius for zero-crossing alignment |
| **Stability Window** | 0.1 s | Centroid stability window (Tonal mode) |
| **Stability Threshold** | 0.5 | Minimum stability score to mark a region start (Tonal mode) |
| **Slice Count Ceiling** | 64 | Hard cap on output slices per file |

---

## Building from Source

See the full build guide on the [Building](Building) page.

Quick start:

```bash
# Configure (once)
setup.bat

# Build GUI
build_debug.bat

# Build + run tests
cmake --build build --target SwitchbladeTests --config Debug
build\tests\Debug\SwitchbladeTests.exe
```

All 36 tests should pass in under 15 seconds on Debug/MSVC.
