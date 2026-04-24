# CLAUDE.md - Project "Switchblade"

## Project Overview
A high-tactile, Art-Deco inspired sample analysis and batch conversion tool. The aesthetic is "Brass, Neon, and Heavy Machinery."

## Languages
- **Main Language Used:** Modern C++, Python


## Core Technical Constraints
- **Audio Logic:** Implement "Density Guard" for all processing.
- **Slice Limit:** Maximum "Slice Count Ceiling" is **64**.
- **Build Command:** `npm run build`

## The Production Line (UX Flow)

### 1. The Aperture (Entry)
- **UI:** Circular frosted-glass aperture with brass framing.
- **Interaction:** Rotate "blades" open on drag-over with neon borders.
- **Feedback:** "Ripple" background effect on file drop.

### 2. Sample Cards (Queue/Audit)
- **Visuals:** Vertical stack of cards with "Neon Filament" waveforms.
- **Control:** 3D glass jewel play button; pulse waveform on playback.

### 3. The Mechanism (The Toggle)
- **Control:** Triple-throw physical lever or 3D dial.
- **Modes:** `Percussive` | `Tonal` | `Auto`.
- **Aesthetic:** Polished chrome, etched glowing letters, heavy "metallic clunk" sound.

### 4. The Igniter (The Plunger)
- **Control:** Large circular "STAMP/ENGAGE" plunger.
- **Action:** Deep physical "throw" into the chassis; triggers a neon "power surge" animation toward the cards.

### 5. The Vault (Results)
- **Visuals:** Small, gem-like cards ejected at the bottom.
- **Management:** "Trash Compactor" lever for `Clear All`.

## UI Layout Strategy
- **Pinned Controls:** Aperture, Toggle, and Plunger stay visible/fixed.
- **Film Strip:** Central scrolling section for Loaded Files and Results to prevent screen real-estate exhaustion.

## Development Guidelines
- **Style:** Prioritize 3D depth, kinetic feedback, and "physical" mechanical sounds.
- **Naming:** Maintain the "Production Line" metaphor in variable and component names (e.g., `ApertureComponent`, `PlungerButton`).

##
- **skills:** Use these: /cpp-pro for any c++ processing, /batch-engineer: Build and configure batch processing tools.
  - shell-pro: Handle shell scripts and command-line tools

## 
**Next Steps:**  1. Check Density Guard (Fixing the "Thin Waveform" issue)

  - Problem: You are seeing a very thin or nearly invisible waveform in the middle row (R_20210804-212747.wav) because the density setting (1.68) is too high for that specific file size. When a file is too short, the tool cannot generate   enough data slices, resulting in invisible audio.

  - Solution: You need a Density Guard to limit the density slider so the tool is always given a safe amount of data (usually between 1.00 and 3.00) to process.                                                                                 - Set Density to: 1.3 (Keep 130% of the data).



  2. Slice Count Ceiling (Fixing the "4 loaded" stopping issue)

  - Problem: The current display shows "4 loaded." As the file is analyzed, the system will try to generate more slices than this "4" limit. Once the limit is hit, the tool stops working to save resources.

  - Solution: You need a Slice Count Ceiling to act as a hard stop.

    - Set Ceiling to: 10 (or higher, like 50, to ensure the file is fully processed).