# Building Switchblade from Source

Switchblade builds on **Windows (MSVC)**, **macOS (Xcode/clang)**, and **Linux (GCC 13+/clang)** with CMake 3.22+ and a C++20 compiler. JUCE is expected at `External/JUCE` (cloned, not FetchContent).

---

## Windows

| Requirement | Version | Notes |
|---|---|---|
| Windows | 10 or 11 (64-bit) | |
| Visual Studio | 2019, 2022, or 2026 | "Desktop development with C++" workload |
| CMake | 3.22+ | Bundled with Visual Studio, or install standalone |
| Git | Any recent | Required by `setup.bat` to clone JUCE |
| Internet | — | First build only (JUCE clone; tests fetch GoogleTest) |

```bat
git clone https://github.com/mikei25-alt/The-Switchblade-Btach-Converter-and-cutter.git
cd The-Switchblade-Btach-Converter-and-cutter
setup.bat
```

`setup.bat` locates CMake, clones JUCE into `External/JUCE`, configures the CMake project in `build/`, and opens the solution. Inside Visual Studio:

1. Set startup project → **Switchblade**
2. Configuration → **Debug | x64**
3. **Ctrl+Shift+B** — Build Solution
4. **Ctrl+F5** — Start Without Debugging

---

## macOS

```bash
git clone https://github.com/mikei25-alt/The-Switchblade-Btach-Converter-and-cutter.git
cd The-Switchblade-Btach-Converter-and-cutter
git clone --depth 1 https://github.com/juce-framework/JUCE.git External/JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Switchblade
open "build/Switchblade_artefacts/Release/The Switchblade.app"
```

For a universal (Apple Silicon + Intel) binary add
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.

---

## Linux

Install the JUCE build dependencies first (Debian/Ubuntu names):

```bash
sudo apt-get install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev \
    libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev \
    libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev
```

```bash
git clone --depth 1 https://github.com/juce-framework/JUCE.git External/JUCE
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
"./build/Switchblade_artefacts/Release/The Switchblade"
```

---

## Building and Running Tests

```bash
cmake --build build --target SwitchbladeTests
ctest --test-dir build                 # or run the binary directly:
./build/tests/SwitchbladeTests         # Windows: build\tests\Debug\SwitchbladeTests.exe
```

Filter a suite with `--gtest_filter="TempoGrid.*"`. Expected: **76 tests, 75 passed / 1 skipped** (the skipped case needs an on-disk sample). Perf-test thresholds are calibrated for unoptimised Debug builds; Release beats them by 10–50×.

GoogleTest is fetched on first configure. To use a local copy (offline/proxied environments):
`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest`.

---

## Installers (CI)

The **Build installers** GitHub Actions workflow (`.github/workflows/build-installers.yml`) produces:

- **Windows** — `TheSwitchblade-<version>-Windows-Setup.exe` via Inno Setup (`installer/windows.iss`); installs to Program Files with Start-Menu entry, optional desktop icon, and uninstaller.
- **macOS** — `TheSwitchblade-macOS.dmg`, a universal binary with a drag-to-Applications layout.

Triggers: manual dispatch, pushes to `claude/**` branches, and version tags `v*` — tag builds also attach both installers to a GitHub Release. The app icon comes from `Source/Assets/logo.png` (converted by JUCE's `juceaide` at build time). Binaries are **unsigned**: SmartScreen and Gatekeeper warn on first launch.

To build the Windows installer locally after a Release build: `iscc installer\windows.iss` (needs an `installer/switchblade.ico`, which CI generates from the logo with Pillow).

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `SWITCHBLADE_TESTS` | `ON` | Build the GoogleTest suite |
| `SWITCHBLADE_ASAN` | `OFF` | AddressSanitizer + UBSan (non-MSVC) |
| `JUCE_DIR` | `External/JUCE` | Path to a JUCE checkout |

---

## Troubleshooting

**`cmake: command not found`** (Windows)
Run `setup.bat` — it finds the Visual Studio-bundled cmake automatically. Alternatively, install CMake from cmake.org and check "Add to PATH".

**`JUCE not found`**
Clone it: `git clone --depth 1 https://github.com/juce-framework/JUCE.git External/JUCE`.

**`FetchContent download failed`** (GoogleTest)
Check internet access, or point at a local clone with `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` (see Tests above). Behind a proxy, set `https_proxy` before configuring.

**Linux configure fails inside JUCE's `juceaide`**
A GUI dev package is missing — install the full dependency list above (the build helper needs X11/FreeType/GTK even for a headless build).

**Build fails with `C2039: 'format': is not a member of 'std'`**
The project avoids `std::format` for VS 2019 compatibility. If you see this, you may have a conflicting header pulled in by a third-party library. File an issue.
