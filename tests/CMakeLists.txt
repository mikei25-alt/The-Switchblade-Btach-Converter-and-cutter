# =============================================================================
#  tests/CMakeLists.txt — GoogleTest suite for the Switchblade analysis engine
# =============================================================================
cmake_minimum_required(VERSION 3.22)

include(FetchContent)

set(GTEST_VERSION v1.15.2)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/${GTEST_VERSION}.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(SwitchbladeTests
    analysis_perf_test.cpp)

target_compile_features(SwitchbladeTests PRIVATE cxx_std_20)

target_link_libraries(SwitchbladeTests
    PRIVATE
        SwitchbladeCore
        GTest::gtest
        GTest::gtest_main)

if(MSVC)
    target_compile_options(SwitchbladeTests PRIVATE /W3 /utf-8)
endif()

enable_testing()
include(GoogleTest)
gtest_discover_tests(SwitchbladeTests)
