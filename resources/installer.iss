#ifndef EngineDir
  #error EngineDir must point to a staged GM Engine installation
#endif
#ifndef OutputPath
  #define OutputPath "."
#endif
[Setup]
AppId={{F09C1127-7BD8-48C9-9D41-82226BC2C14A}
AppName=GM Engine
AppVersion=0.8.0
AppPublisher=GM Engine
DefaultDirName={localappdata}\Programs\GM Engine
DefaultGroupName=GM Engine
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputPath}
OutputBaseFilename=GMEngine-0.8.0-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\gm_editor.exe
ChangesAssociations=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "associate"; Description: "Open .gmproject files with GM Engine"; GroupDescription: "Project files:"

[Files]
Source: "{#EngineDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: ".gmengine\*,build\*,dist\*"

[Icons]
Name: "{group}\GM Engine"; Filename: "{app}\bin\gm_editor.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\GM Engine"; Filename: "{app}\bin\gm_editor.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\.gmproject\OpenWithProgids"; ValueType: string; ValueName: "GMEngine.Project"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\GMEngine.Project"; ValueType: string; ValueName: ""; ValueData: "GM Engine Project"; Flags: uninsdeletekey; Tasks: associate
Root: HKCU; Subkey: "Software\Classes\GMEngine.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\gm_editor.exe"" --project ""%1"""; Tasks: associate

[Run]
Filename: "{app}\bin\gm_editor.exe"; Description: "Launch GM Engine"; Flags: nowait postinstall skipifsilent
