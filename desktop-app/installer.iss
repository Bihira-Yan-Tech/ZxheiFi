; installer.iss - Inno Setup script for ZxheiFi Setup Companion.
; Wraps the PyInstaller --onedir build (dist\ZxheiFi-Setup-Companion\) into
; a traditional Windows installer: Program Files install, Start Menu
; shortcut, uninstaller. Built by build.py's build_installer(), or
; directly via: ISCC.exe installer.iss (run build.py --portable/--installer
; first isn't needed - build_installer() runs the onedir PyInstaller step
; itself if the folder is missing).

#define MyAppName "ZxheiFi Setup Companion"
#define MyAppExeName "ZxheiFi-Setup-Companion.exe"
#define MyAppPublisher "ZxheiFi"
#define MySourceDir "dist\ZxheiFi-Setup-Companion"

[Setup]
AppName={#MyAppName}
AppVersion=1.0.0
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=dist
OutputBaseFilename=ZxheiFi-Setup-Companion-Installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
VersionInfoVersion=1.0.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#MySourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
