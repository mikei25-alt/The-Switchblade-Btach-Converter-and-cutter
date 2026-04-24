@echo off
setlocal

:: Locate MSBuild from VS 2026 / 2022 / 2019 in order
set "MSBUILD="
for %%P in (
    "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files\Microsoft Visual Studio\17\Community\MSBuild\Current\Bin\MSBuild.exe"
    "C:\Program Files\Microsoft Visual Studio\16\Community\MSBuild\Current\Bin\MSBuild.exe"
) do (
    if exist %%P (
        set "MSBUILD=%%P"
        goto :found_msbuild
    )
)
echo [ERROR] MSBuild not found.
pause & exit /b 1
:found_msbuild

:: Prefer .slnx (VS 2026), fall back to .sln
set "SLN=%~dp0build\Switchblade.slnx"
if not exist "%SLN%" set "SLN=%~dp0build\Switchblade.sln"
if not exist "%SLN%" (
    echo [ERROR] No solution file found in build\. Run setup.bat first.
    pause & exit /b 1
)

echo [..] Building Switchblade (Debug ^| x64) ...
echo      Solution : %SLN%
echo      MSBuild  : %MSBUILD%
echo.

:: /m:2          = max 2 parallel project builds
:: /p:CL_MPCount=2 = max 2 cl.exe threads per project (avoids OOM on JUCE unity builds)
:: /t:Switchblade = only build the GUI target (skips CLI etc.)
%MSBUILD% "%SLN%" /m:2 /p:Configuration=Debug /p:Platform=x64 /p:CL_MPCount=2 /t:Switchblade /v:minimal

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Check errors above.
    pause & exit /b 1
)

echo.
echo [OK] Build succeeded.
echo      Executable: build\Switchblade_artefacts\Debug\Switchblade.exe
start "" "%~dp0build\Switchblade_artefacts\Debug\Switchblade.exe"
pause
