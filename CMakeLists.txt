cmake_minimum_required(VERSION 3.22)
project(Switchblade
    VERSION 0.1.0
    DESCRIPTION "The Switchblade — intelligent batch one-shot extractor"
    LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Language / toolchain
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /FS serialises concurrent PDB writes through mspdbsrv, preventing LNK4099.
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /FS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# -----------------------------------------------------------------------------
# JUCE: expected at External/JUCE (add as submodule or copy SDK there)
# -----------------------------------------------------------------------------
set(JUCE_DIR "${CMAKE_SOURCE_DIR}/External/JUCE" CACHE PATH "Path to JUCE")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_DIR}. "
        "Run: git clone --depth 1 https://github.com/juce-framework/JUCE.git External/JUCE")
endif()

# -----------------------------------------------------------------------------
# Binary assets — logo PNG embedded at compile time via JUCE binary data
# -----------------------------------------------------------------------------
juce_add_binary_data(SwitchbladeAssets SOURCES
    Source/Assets/logo.png)

# -----------------------------------------------------------------------------
# Core DSP / analysis library (used by CLI + future GUI app)
# -----------------------------------------------------------------------------
add_library(SwitchbladeCore STATIC
    Source/Analysis/SpectralFlux.cpp
    Source/Analysis/TransientDetector.cpp
    Source/Analysis/TextureAnalyzer.cpp
    Source/Analysis/AnalysisEngine.cpp)
    # PitchDetector.h is header-only (inline YIN) — no .cpp needed

target_include_directories(SwitchbladeCore PUBLIC Source)

target_link_libraries(SwitchbladeCore
    PUBLIC
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_events      # MessageManager::callAsync in AnalysisEngine
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(SwitchbladeCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

# -----------------------------------------------------------------------------
# UI library — LookAndFeel, shaders, visual components
# -----------------------------------------------------------------------------
add_library(SwitchbladeUI STATIC
    Source/UI/SwitchbladeLookAndFeel.cpp
    Source/UI/FrostedGlassPanel.cpp
    Source/UI/NeonBloomShader.cpp)

target_include_directories(SwitchbladeUI PUBLIC Source)

target_link_libraries(SwitchbladeUI
    PUBLIC
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_opengl
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(SwitchbladeUI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# -----------------------------------------------------------------------------
# CLI analyzer: `switchblade-analyze <in.wav> [out.json]`
# -----------------------------------------------------------------------------
juce_add_console_app(SwitchbladeAnalyzeCLI
    PRODUCT_NAME "switchblade-analyze")

target_sources(SwitchbladeAnalyzeCLI PRIVATE CLI/analyze_cli.cpp)

target_link_libraries(SwitchbladeAnalyzeCLI
    PRIVATE SwitchbladeCore)

# -----------------------------------------------------------------------------
# GUI application — The Switchblade desktop app
# -----------------------------------------------------------------------------
juce_add_gui_app(Switchblade
    PRODUCT_NAME    "The Switchblade"
    COMPANY_NAME    "Grain of Salt Audio"
    BUNDLE_ID       "com.grainofsalt.switchblade"
    VERSION         "0.1.0")

target_compile_features(Switchblade PRIVATE cxx_std_20)

target_sources(Switchblade PRIVATE
    Source/Main.cpp
    Source/UI/SampleCard.cpp
    Source/UI/PreviewGrid.cpp
    Source/UI/ResultTile.cpp
    Source/UI/ResultsVault.cpp
    Source/UI/MainContainer.cpp
    # SwitchbladeLookAndFeel, FrostedGlassPanel, NeonBloomShader
    # are compiled via SwitchbladeUI static lib below
)

target_include_directories(Switchblade PRIVATE Source)

target_compile_definitions(Switchblade PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STANDALONE_APPLICATION=1)

# Route static-lib compiler PDBs into the exe output dir so LNK4099 is silent.
# PDB_OUTPUT_DIRECTORY is for linker PDBs (exe/dll); static libs use COMPILE_PDB_OUTPUT_DIRECTORY.
set_target_properties(SwitchbladeCore SwitchbladeUI PROPERTIES
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Switchblade_artefacts/$<CONFIG>")

target_link_options(Switchblade PRIVATE /ignore:4099)   # PDB not co-located with static libs — debug via sub-project

target_link_libraries(Switchblade
    PRIVATE
        SwitchbladeAssets
        SwitchbladeCore
        SwitchbladeUI
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_opengl
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Tests (GoogleTest — requires internet on first configure for FetchContent)
# -----------------------------------------------------------------------------
option(SWITCHBLADE_TESTS "Build GoogleTest suite" ON)
if(SWITCHBLADE_TESTS)
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
# Optional sanitizer preset (cmake -DSWITCHBLADE_ASAN=ON ...)
# -----------------------------------------------------------------------------
option(SWITCHBLADE_ASAN "Enable AddressSanitizer + UBSan" OFF)
if(SWITCHBLADE_ASAN AND NOT MSVC)
    foreach(target SwitchbladeCore SwitchbladeUI SwitchbladeAnalyzeCLI Switchblade)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()
