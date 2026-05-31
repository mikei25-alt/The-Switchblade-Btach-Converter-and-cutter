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

## AI Workflow & Skill Directives
- **Direct Reads:** This project has no knowledge graph. Read files directly.
- **Bug-Fix Loop:** Use `diagnose` for wrong behavior -> `cpp-build` for CMake/linker errors -> `cpp-test` for regressions -> `verify` -> commit.
- **UI Exploration:** The Neon-Deco vision is greenfield. Use `design-an-interface` to generate 3 variants -> `prototype` to test them -> `feature-dev` for implementation.
- **Final Checks:** Always run `code-review` followed by `simplify` before committing.


## Context Navigation
When you need to understand the codebase, docs, or any 
files in this project:

1. ALWAYS query the knowledge graph first: 
   '/graphify query "your question"'

2. Only read raw files if I explicitly say "read the file" 
   or "look at the raw file"

3. Use 'graphify-out/wiki/index.md' as your navigation 
   entrypoint for browsing structure
