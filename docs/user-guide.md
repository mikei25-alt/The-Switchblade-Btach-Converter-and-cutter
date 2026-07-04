# Switchblade — User Guide

**Version 0.8.0** · Art-Deco batch sample converter and intelligent slicer

---

## Table of Contents

1. [Overview](#overview)
2. [Installing](#installing)
3. [The Production Line](#the-production-line)
   - [Loading Files](#loading-files)
   - [Sample Cards](#sample-cards)
   - [Choosing a Mode](#choosing-a-mode)
   - [Grid Mode Controls](#grid-mode-controls)
   - [Producing Slices](#producing-slices)
   - [The Preview Grid](#the-preview-grid)
   - [The Results Vault](#the-results-vault)
4. [Editing Slice Markers](#editing-slice-markers)
5. [Keyboard Shortcuts](#keyboard-shortcuts)
6. [Slice Naming](#slice-naming)
7. [Settings Reference](#settings-reference)

---

## Overview

Switchblade is a desktop tool for producers who need to batch-convert audio files and automatically slice them into usable samples. Drop in a folder of loops, one-shots, or mixed material — Switchblade analyses each file, classifies it, slices it at musically meaningful boundaries, and exports named 24-bit WAV slices ready for your sampler.

The interface is styled after Art-Deco industrial machinery: chrome frames, neon filaments, and heavy physical controls.

> **Modifier key:** everywhere this guide says **Cmd/Ctrl**, use **Cmd** on macOS and **Ctrl** on Windows/Linux.

---

## Installing

Prebuilt installers come from the **Build installers** GitHub Actions workflow (and are attached to GitHub Releases on version tags):

- **Windows** — run `TheSwitchblade-…-Windows-Setup.exe`. The binary is unsigned, so SmartScreen will warn: *More info → Run anyway*.
- **macOS** — open `TheSwitchblade-macOS.dmg` and drag the app to Applications. Unsigned: **right-click → Open** the first time to pass Gatekeeper.

To build from source instead, see [building.md](building.md).

---

## The Production Line

### Loading Files

Drag audio files — or entire folders, scanned recursively — anywhere onto the window. Before anything is loaded, the animated drop-zone panel shows what's accepted:

**Supported formats:** WAV · AIFF · MP3 · FLAC · OGG (batch drops are capped at 500 files)

Every dropped file immediately gets a card and is analysed in the background; the Produce button shows a live `ANALYZING… (N)` count.

### Sample Cards

Each loaded file is a card in the central scrolling strip:

| Element | Interaction |
|---|---|
| **Play button** (green triangle) | Preview the whole file |
| **Filename strip** | Drag from here to export the *source file* out to your DAW / file manager (grab-hand cursor) |
| **Classification badge** (right) | Click to force a different analysis mode for *this file only* — also the retry button if analysis failed |
| **Waveform** | Cmd/Ctrl+wheel zooms around the cursor; drag to pan when zoomed; the `N×` badge resets zoom |
| **Gold markers** | Slice boundaries — drag to nudge (zero-crossing snapped), double-click to add/delete |
| **Extract** | Export just this card's slices |

Click a card to select it (cyan glow) — the preview grid follows the selection. **Cmd/Ctrl+click** toggles the gold multi-select used for bulk deletion. **Right-click** opens the context menu (delete, re-enable delete confirmation). Failed files show a crimson **ANALYSIS FAILED** card with the reason.

### Choosing a Mode

The mode combo in the top bar selects the slicing algorithm. Changing it re-analyses every loaded card immediately.

| Mode | Best For |
|---|---|
| **Auto** | Mixed or unknown material — classifies each file and picks |
| **Percussive** | Drums, one-shots, rhythmic content (spectral-flux onsets) |
| **Melodic** | Note-based material — segments at pitch changes, names every slice |
| **Texture** | Pads and atmospheres — fires where the spectrum settles |
| **Grid** | Loops at a known/detectable tempo — musical subdivisions |

The **SENS** slider tunes detection sensitivity (lower = stricter, fewer slices).

### Grid Mode Controls

In Grid mode the slider becomes the **subdivision** control (1/2 … 1/64, triplets included) and two fields appear:

- **BPM field** — shows the detected tempo. Tempo precedence: a BPM in the filename (`Loop_128_Cmaj.wav`) → a number you type here → automatic detection. Clear it (or type `AUTO`) to return to auto-detect.
- **MAX field** — `ALL` keeps one slice per subdivision; a number curates down to that many of the strongest, most *distinct* one-shots.

Grid tweaks re-slice all loaded cards live.

### Producing Slices

- **Produce** — exports every slice from every card. If any vault tiles are armed (gold), it exports just those instead.
- **Export Selection** — exports only the armed vault tiles.
- **Right-click either button** — choose peak normalization (off / −1 / −3 / −6 dBFS). The current level shows on the buttons and as a `⊕` pill on cards/tiles.
- **Source folder** — where files land: next to each source by default; click to pick a folder, right-click to reset.

Exports run in the background with an `Exporting n/N` progress readout — the UI stays responsive.

### The Preview Grid

The 4×4 pad grid maps the first four slices of up to four loaded cards (one card per row). Click a pad or use the keys printed on it: `1 2 3 4` / `Q W E R` / `A S D F` / `Z X C V`. Up to 8 voices play polyphonically.

### The Results Vault

Every slice lands as a tile in the vault (staggered as analysis completes; the badge bar counts `N READY +M INCOMING`):

- **Click** a tile to audition it; **double-click** to audition and select.
- **Cmd/Ctrl+click** arms tiles (gold) for Export Selection — the `[N] ARMED` counter tracks them.
- **Drag a tile out** into your DAW or file manager. Tiles are pre-rendered in the background, so the drag is instant; an armed multi-selection drags as a bundle.
- **CLEAR** (badge bar) empties the vault after a confirm — the "Trash Compactor". Source cards stay.
- When a batch finishes, the ceremony bar stamps `✦ N SAMPLES READY` with an **EXPORT COLLECTION** button.

Deleting a card that produced vault slices asks what to do with them (with a rememberable choice — reversible from the card's right-click menu).

---

## Editing Slice Markers

Detection is a starting point; every boundary is editable on the card:

| Action | Result |
|---|---|
| **Drag a gold marker** | Move the boundary (snaps to the nearest zero-crossing within ±5 ms) |
| **Double-click empty waveform** | Add a marker there (also zero-snapped; 64-marker ceiling) |
| **Double-click a marker** | Delete it |

Edits immediately re-render only that file's vault tiles — your tile selection elsewhere survives.

---

## Keyboard Shortcuts

| Key | Action |
|---|---|
| `Space` | Preview the selected card (replaces any playing voice) |
| `↑` / `↓` | Walk the card list (auto-scrolls into view) |
| `Cmd/Ctrl+A` | Arm every vault tile for Export Selection |
| `Esc` | Stop playback and clear all selections |
| `Delete` / `Backspace` | Delete the selected (or multi-selected) card(s) |
| `1-4` `QWER` `ASDF` `ZXCV` | Trigger preview-grid pads |
| `Cmd/Ctrl+wheel` on a waveform | Zoom (plain wheel scrolls the list) |

---

## Slice Naming

| Mode | Pattern | Example |
|---|---|---|
| Percussive / Texture | `[stem]_[tag]_[index].wav` | `drums_perc_001.wav` |
| Melodic / Grid (pitched) | `[stem]_[Note±cents]_[index].wav` | `SerumLead_C#3+12c_001.wav` |

Each *slice* gets its own pitch estimate (not the file-wide pitch), the cents suffix is omitted when in tune, and pitched exports carry ACID root-note metadata plus a BWAV `bext` chunk recording the source file and offset.

---

## Settings Reference

| Setting | Where | Default | Description |
|---|---|---|---|
| Sensitivity | top bar | 1.0 | Onset detection sensitivity (0.3 strict … 1.3 loose) |
| Subdivision | top bar (Grid) | 1/16 | Musical grid step, incl. triplet stops |
| BPM | top bar (Grid) | AUTO | Manual tempo override |
| Max samples | top bar (Grid) | ALL | Curate grid one-shots down to N distinct hits |
| Normalization | right-click Produce / Export Selection | off | Peak-normalize exports to −1/−3/−6 dBFS |
| Output folder | Source folder button | next to source | Click to choose; right-click to reset |
| Slice Count Ceiling | fixed | 64 | Hard cap on markers per file |
