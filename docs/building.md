# Building Switchblade from Source

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| Windows | 10 or 11 (64-bit) | |
| Visual Studio | 2019, 2022, or 2026 | "Desktop development with C++" workload |
| CMake | 3.22+ | Bundled with Visual Studio, or install standalone |
| Git | Any recent | Required by `setup.bat` to clone JUCE |
| Internet | — | First build only (FetchContent downloads JUCE + GoogleTest) |

---

## Quick Start

```bat
git clone https://github.com/mikei25-alt/The-Switchblade-Btach-Converter-and-cutter.git
cd The-Switchblade-Btach-Converter-and-cutter
setup.bat
```

`setup.bat` locates CMake, clones JUCE 8.0.6 into `External/JUCE`, configures the CMake project in `build/`, and opens the solution. If CMake is not on your PATH, the script searches for the Visual Studio-bundled copy.

Inside Visual Studio:
1. Set startup project → **Switchblade**
2. Configuration → **Debug | x64**
3. **Ctrl+Shift+B** — Build Solution
4. **Ctrl+F5** — Start Without Debugging

---

## Building Just the Analysis Core

```bat
cmake --build build --target SwitchbladeCore --config Debug
```

---

## Building and Running Tests

```bat
cmake --build build --target SwitchbladeTests --config Debug -j 4
build\tests\Debug\SwitchbladeTests.exe
```

To run a specific test suite:

```bat
build\tests\Debug\SwitchbladeTests.exe --gtest_filter="TransientDetector.*"
```

To run with colour output:

```bat
build\tests\Debug\SwitchbladeTests.exe --gtest_color=yes
```

Expected output (Debug, ~7 seconds):

```
[==========] Running 36 tests from 13 test suites.
...
[  PASSED  ] 36 tests.
```

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `SWITCHBLADE_TESTS` | `ON` | Build the GoogleTest suite |
| `CMAKE_CONFIGURATION_TYPES` | `Debug;Release` | Build configurations |

Example — disable tests:

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DSWITCHBLADE_TESTS=OFF
```

---

## Release Build

```bat
cmake --build build --target Switchblade --config Release
```

The Release binary appears at `build\Switchblade_artefacts\Release\Switchblade.exe`. Release builds with /O2 + AVX beat all Debug performance baselines by 10–50×.

---

## Troubleshooting

**`cmake: command not found`**
Run `setup.bat` — it finds the Visual Studio-bundled cmake automatically. Alternatively, install CMake from cmake.org and check "Add to PATH".

**`JUCE not found`**
Delete `External/JUCE` and re-run `setup.bat`. JUCE is cloned fresh.

**`FetchContent download failed`** (GoogleTest)
Check internet access. If behind a proxy, set `https_proxy` in the environment before running cmake.

**Build fails with `C2039: 'format': is not a member of 'std'`**
The project avoids `std::format` for VS 2019 compatibility. If you see this, you may have a conflicting header pulled in by a third-party library. File an issue.
