; =============================================================================
;  windows.iss — Inno Setup script for The Switchblade Windows installer.
;
;  Built by the "Build installers" GitHub Actions workflow, which compiles the
;  app first and generates installer/switchblade.ico from Source/Assets/logo.png.
;  To build locally:  iscc installer\windows.iss   (after a Release build).
; =============================================================================

#define MyAppName      "The Switchblade"
#define MyAppVersion   "0.8.0"
#define MyAppPublisher "Grain of Salt Audio"
#define MyAppExeName   "The Switchblade.exe"
#define MyBuildDir     "..\build\Switchblade_artefacts\Release"

[Setup]
AppId={{7B7A2A6E-9C3B-4A56-8B1E-5D2C6F0A11ED}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=TheSwitchblade-{#MyAppVersion}-Windows-Setup
SetupIconFile=switchblade.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#MyBuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}";       Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent
