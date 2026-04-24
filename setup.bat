@echo off
setlocal enabledelayedexpansion

echo ==========================================================
echo  The Switchblade  --  Visual Studio project setup
echo ==========================================================
echo.

:: ---- Locate Git -------------------------------------------------------
where git >nul 2>&1
if errorlevel 1 (
    echo [ERROR] git not found in PATH.
    echo         Install Git for Windows from https://git-scm.com and retry.
    pause & exit /b 1
)

:: ---- Locate CMake (PATH first, then VS-bundled fallback) --------------
set "CMAKE_EXE=cmake"
where cmake >nul 2>&1
if errorlevel 1 (
    echo [..] cmake not in PATH -- searching Visual Studio bundled CMake...

    :: VS 2022 Community / Professional / Enterprise
    for /f "delims=" %%P in ('dir /b /s /ad "C:\Program Files\Microsoft Visual Studio" 2^>nul ^| findstr /i "CMake\\CMake\\bin"') do (
        if exist "%%P\cmake.exe" (
            set "CMAKE_EXE=%%P\cmake.exe"
            echo [OK]  Found VS-bundled cmake: %%P\cmake.exe
            goto :cmake_found
        )
    )
    :: VS 2019
    for /f "delims=" %%P in ('dir /b /s /ad "C:\Program Files (x86)\Microsoft Visual Studio" 2^>nul ^| findstr /i "CMake\\CMake\\bin"') do (
        if exist "%%P\cmake.exe" (
            set "CMAKE_EXE=%%P\cmake.exe"
            echo [OK]  Found VS-bundled cmake: %%P\cmake.exe
            goto :cmake_found
        )
    )

    echo [ERROR] cmake not found anywhere.
    echo.
    echo  Fix options (pick one):
    echo    A) Open VS Installer ^> Modify ^> Individual components ^>
    echo       tick "C++ CMake tools for Windows" ^> Modify
    echo    B) Install standalone: https://cmake.org/download/
    echo       (check "Add CMake to the system PATH" during install)
    echo.
    pause & exit /b 1
)
:cmake_found

:: ---- Locate a Visual Studio generator --------------------------------
:: Prefer VS 2026, fall back to 2022, then 2019.
set "VS_GEN="
"%CMAKE_EXE%" --help 2>&1 | findstr /C:"Visual Studio 18 2026" >nul
if not errorlevel 1 (
    set "VS_GEN=Visual Studio 18 2026"
    goto :gen_found
)
"%CMAKE_EXE%" --help 2>&1 | findstr /C:"Visual Studio 17 2022" >nul
if not errorlevel 1 (
    set "VS_GEN=Visual Studio 17 2022"
    goto :gen_found
)
"%CMAKE_EXE%" --help 2>&1 | findstr /C:"Visual Studio 16 2019" >nul
if not errorlevel 1 (
    set "VS_GEN=Visual Studio 16 2019"
    goto :gen_found
)

echo [ERROR] No supported Visual Studio generator found (need 2019, 2022, or 2026).
echo         Install Visual Studio with the "Desktop development with C++" workload.
pause & exit /b 1

:gen_found
echo [OK]  CMake generator: %VS_GEN%

:: ---- JUCE --------------------------------------------------------------
set "JUCE_DIR=%~dp0External\JUCE"
if exist "%JUCE_DIR%\CMakeLists.txt" (
    echo [OK]  JUCE found at External\JUCE  -- skipping clone.
) else (
    echo [..] Cloning JUCE into External\JUCE  (shallow, latest tag^) ...
    git clone --depth 1 --branch 8.0.6 ^
        https://github.com/juce-framework/JUCE.git ^
        "%JUCE_DIR%" 2>&1
    if errorlevel 1 (
        echo [WARN] Tagged clone failed -- trying HEAD instead...
        git clone --depth 1 ^
            https://github.com/juce-framework/JUCE.git ^
            "%JUCE_DIR%" 2>&1
        if errorlevel 1 (
            echo [ERROR] git clone failed.  Check your internet connection.
            pause & exit /b 1
        )
    )
    echo [OK]  JUCE cloned.
)

:: ---- CMake configure ---------------------------------------------------
set "BUILD_DIR=%~dp0build"
echo.
echo [..] Configuring CMake (x64 Debug)...
echo      Generator : %VS_GEN%
echo      Build dir : %BUILD_DIR%
echo.

"%CMAKE_EXE%" -S "%~dp0" ^
              -B "%BUILD_DIR%" ^
              -G "%VS_GEN%" ^
              -A x64 ^
              -DCMAKE_CONFIGURATION_TYPES="Debug;Release" ^
              -DJUCE_DIR="%JUCE_DIR%" 2>&1

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed.
    echo         Review the errors above.  Common fixes:
    echo           - Make sure "C++ CMake tools" is installed via VS Installer.
    echo           - Delete the build\ folder and rerun setup.bat.
    pause & exit /b 1
)

echo.
echo [OK]  CMake configuration complete.
echo.

:: ---- Open solution -----------------------------------------------------
:: VS 2026 generates .slnx; VS 2022/2019 generate .sln
set "SLN=%BUILD_DIR%\Switchblade.slnx"
if not exist "%SLN%" set "SLN=%BUILD_DIR%\Switchblade.sln"
if not exist "%SLN%" (
    echo [WARN] No .slnx or .sln found in build\.
    echo        Open build\Switchblade.slnx (or .sln) manually.
) else (
    echo [..] Opening %SLN% in Visual Studio...
    start "" "%SLN%"
)

echo.
echo ==========================================================
echo  SETUP COMPLETE
echo.
echo  In Visual Studio:
echo    1. Set startup project  ^>  Switchblade
echo    2. Select configuration ^>  Debug ^| x64
echo    3. Build ^> Build Solution  (Ctrl+Shift+B)
echo    4. Debug ^> Start Without Debugging  (Ctrl+F5)
echo.
echo  CLI tool (optional, no UI):
echo    cmake --build build --target SwitchbladeAnalyzeCLI --config Debug
echo    build\Debug\switchblade-analyze.exe <in.wav> [out.json]
echo ==========================================================
pause
