#define AppName "BreadReplay"
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\release"
#endif
; BreadReplay ships BreadCamera's libraries, so it ships BreadCamera's notices and licence texts.
#ifndef BcInstaller
  #define BcInstaller "..\..\BreadCamera\installer"
#endif

[Setup]
AppId={{ED5CE976-D4A2-4C5C-8C71-6C64D3866EB2}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=BM Solutions
AppPublisherURL=https://bmsolutions.vn
DefaultDirName={autopf}\BreadMedia\BreadReplay
DefaultGroupName=BreadReplay
OutputDir=..\dist
OutputBaseFilename=BreadReplay-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0.19041
AppMutex=Local\BreadReplayShell,Global\BreadReplayShell
CloseApplications=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=THIRD_PARTY_NOTICES.md

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#BuildDir}\BreadReplay.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\breadreplay-engine.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs
Source: "{#VcRedist}"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "THIRD_PARTY_NOTICES.md"; DestDir: "{app}"
Source: "LGPL-source-offer.txt"; DestDir: "{app}"
Source: "{#BcInstaller}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; DestName: "BreadCamera-THIRD_PARTY_NOTICES.md"
Source: "{#BcInstaller}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\BreadReplay"; Filename: "{app}\BreadReplay.exe"
Name: "{autodesktop}\BreadReplay"; Filename: "{app}\BreadReplay.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Visual C++ runtime..."
Filename: "{app}\BreadReplay.exe"; Description: "Start BreadReplay"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/im breadreplay-engine.exe /f"; RunOnceId: "StopEngine"; Flags: runhidden

[Code]
function InitializeSetup: Boolean;
var Code: Integer;
begin
  { The engine outlives the window and may be recording; stop it so its files can be replaced. }
  if CheckForMutexes('Local\BreadReplayEngine') then
  begin
    if MsgBox('BreadReplay is running and may be recording. Stop it and continue the installation?', mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
      exit;
    end;
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/im breadreplay-engine.exe /f', '', SW_HIDE, ewWaitUntilTerminated, Code);
  end;
  Result := True;
end;
