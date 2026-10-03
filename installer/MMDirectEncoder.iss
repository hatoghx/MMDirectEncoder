#define AppName "MMDirectEncoder"
#define BuildDir "..\..\build"
#ifndef AppVersion
  #define AppVersion GetStringFileInfo(BuildDir + "\MMDirectEncoder.dll", "ProductVersion")
#endif
#define FilterClsid "{{D79D43B2-F005-40A4-BE18-AFD19C03E6E6}"
#define VideoCompressorCategory "{{33d9a760-90c8-11d0-bd43-00a0c911ce86}"
#define LegacyFilterCategory "{{083863F1-70DE-11d0-BD40-00A0C911CE86}"

[Setup]
AppId={{DACEB0A6-5C41-42D3-B383-E135FF0847C2}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=hato
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=MMDirectEncoderSetup_{#AppVersion}
SetupIconFile=setup.ico
UninstallDisplayIcon={app}\MMDirectEncoderConfig.exe
UninstallDisplayName={#AppName}
Compression=lzma2
SolidCompression=yes
CloseApplications=no
SetupLogging=yes
WizardStyle=classic
DisableReadyMemo=yes
DisableWelcomePage=no
LicenseFile={#BuildDir}\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
SettingsShortcut=MMDirectEncoder Settings
MmdRunning=Close MikuMikuDance and MMDirectEncoder Settings, then click Retry.
NewerInstalled=Version %1 is installed. Replace it with %2?
AlreadyInstalled=MMDirectEncoder %1 is already installed.%n%nYes: Open settings%nNo: Reinstall%nCancel: Exit
KeepSettingsQuestion=Also delete settings and logs?
TestNotRegistered=Registration failed. Run the installer again.
TestLoadFailed=MMDirectEncoder.dll could not be loaded. It may be blocked by antivirus software.
TestFfmpegMissing=ffmpeg.exe is missing. It may have been quarantined by antivirus software.
TestFfmpegBlocked=ffmpeg.exe could not run. It may be blocked by antivirus software.
TestVideoFailed=Test encode failed. Update the GPU driver or set Encoder to CPU.
TestExrFailed=Test EXR write failed. Check free disk space.
TestUnknown=Self test failed (code %1).
TestLogHint=Log: %1
FinishedWithProblem=MMDirectEncoder was installed, but the self test failed.%n%n%1

[Files]
Source: "{#BuildDir}\MMDirectEncoder.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\MMDirectEncoderConfig.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\bin\ffmpeg.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#BuildDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\LICENSES\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\LICENSES"; Flags: ignoreversion

[Icons]
Name: "{group}\{cm:SettingsShortcut}"; Filename: "{app}\MMDirectEncoderConfig.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Classes\CLSID\{#FilterClsid}"; ValueType: string; ValueName: ""; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\CLSID\{#FilterClsid}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{app}\MMDirectEncoder.dll"
Root: HKCU; Subkey: "Software\Classes\CLSID\{#FilterClsid}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Both"
Root: HKCU; Subkey: "Software\Classes\CLSID\{#VideoCompressorCategory}\Instance\{#FilterClsid}"; ValueType: string; ValueName: "CLSID"; ValueData: "{#FilterClsid}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\CLSID\{#VideoCompressorCategory}\Instance\{#FilterClsid}"; ValueType: string; ValueName: "FriendlyName"; ValueData: "{#AppName}"
Root: HKCU; Subkey: "Software\Classes\CLSID\{#LegacyFilterCategory}\Instance\{#FilterClsid}"; ValueType: string; ValueName: "CLSID"; ValueData: "{#FilterClsid}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\CLSID\{#LegacyFilterCategory}\Instance\{#FilterClsid}"; ValueType: string; ValueName: "FriendlyName"; ValueData: "{#AppName}"

[InstallDelete]
Type: files; Name: "{app}\LICENSE.txt"
Type: files; Name: "{app}\LICENSES\OpenEXR-LICENSE.md"
Type: files; Name: "{app}\LICENSES\Imath-LICENSE.md"
Type: files; Name: "{app}\LICENSES\libdeflate-LICENSE.txt"
Type: files; Name: "{app}\LICENSES\OpenJPH-LICENSE.txt"
Type: files; Name: "{app}\install.exe"
Type: files; Name: "{app}\uninstall.exe"
Type: files; Name: "{app}\uninstall.cmd"
Type: files; Name: "{app}\uninstall.ps1"
Type: files; Name: "{app}\MMDEncoderConfig.exe"
Type: files; Name: "{userprograms}\MMDirect Encoder\MMDirect Encoder Settings.lnk"
Type: files; Name: "{userprograms}\MMDirect Encoder\Uninstall.lnk"
Type: dirifempty; Name: "{userprograms}\MMDirect Encoder"
Type: files; Name: "{userprograms}\FFmpeg Video Encoder\FFmpeg Video Encoder Settings.lnk"
Type: files; Name: "{userprograms}\FFmpeg Video Encoder\Uninstall.lnk"
Type: dirifempty; Name: "{userprograms}\FFmpeg Video Encoder"
Type: files; Name: "{userprograms}\MMD FFmpeg Encoder\MMD FFmpeg Encoder Settings.lnk"
Type: files; Name: "{userprograms}\MMD FFmpeg Encoder\Uninstall.lnk"
Type: dirifempty; Name: "{userprograms}\MMD FFmpeg Encoder"
Type: files; Name: "{userprograms}\MMDirectEncoder\MMDirectEncoder Settings.lnk"
Type: files; Name: "{userprograms}\MMDirectEncoder\Uninstall.lnk"

[UninstallDelete]
Type: dirifempty; Name: "{app}\bin"
Type: dirifempty; Name: "{app}\LICENSES"
Type: dirifempty; Name: "{group}"

[Code]
var
  SelfTestMessage: String;

function IsMmdRunning(): Boolean;
begin
  Result := (FindWindowByClassName('Polygon Movie Maker') <> 0) or
    (FindWindowByWindowName('MMDirectEncoder Settings') <> 0) or
    (FindWindowByWindowName('MMDirectEncoder 設定') <> 0);
end;

function WaitForMmdToClose(): Boolean;
begin
  Result := True;
  while IsMmdRunning() do
  begin
    if SuppressibleMsgBox(CustomMessage('MmdRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

function VersionPart(var S: String): Integer;
var
  P: Integer;
begin
  P := Pos('.', S);
  if P = 0 then
  begin
    Result := StrToIntDef(S, 0);
    S := '';
  end
  else
  begin
    Result := StrToIntDef(Copy(S, 1, P - 1), 0);
    Delete(S, 1, P);
  end;
end;

function CompareVersions(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  for I := 1 to 4 do
  begin
    X := VersionPart(A);
    Y := VersionPart(B);
    if X <> Y then
    begin
      if X > Y then Result := 1 else Result := -1;
      Exit;
    end;
  end;
end;

function AskSameVersionInstalled(UninstallKey: String): Boolean;
var
  AppDir, ConfigExe: String;
  Answer, Code: Integer;
begin
  Result := True;
  if WizardSilent() then
    Exit;
  if not RegQueryStringValue(HKCU, UninstallKey, 'InstallLocation', AppDir) then
    Exit;
  ConfigExe := AddBackslash(AppDir) + 'MMDirectEncoderConfig.exe';
  if not FileExists(ConfigExe) or not FileExists(AddBackslash(AppDir) + 'MMDirectEncoder.dll') then
    Exit;
  Answer := MsgBox(FmtMessage(CustomMessage('AlreadyInstalled'), ['{#AppVersion}']), mbInformation, MB_YESNOCANCEL or MB_DEFBUTTON1);
  if Answer = IDNO then
    Exit;
  Result := False;
  if Answer = IDYES then
    ShellExec('', ConfigExe, '', AppDir, SW_SHOWNORMAL, ewNoWait, Code);
end;

function InitializeSetup(): Boolean;
var
  Installed, UninstallKey: String;
  Order: Integer;
begin
  UninstallKey := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{DACEB0A6-5C41-42D3-B383-E135FF0847C2}_is1';
  Result := True;
  if RegQueryStringValue(HKCU, UninstallKey, 'DisplayVersion', Installed) then
  begin
    Order := CompareVersions(Installed, '{#AppVersion}');
    if Order = 0 then
      Result := AskSameVersionInstalled(UninstallKey)
    else if Order > 0 then
      Result := SuppressibleMsgBox(FmtMessage(CustomMessage('NewerInstalled'), [Installed, '{#AppVersion}']), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
  end;
  if Result then
    Result := WaitForMmdToClose();
end;

function InitializeUninstall(): Boolean;
begin
  Result := WaitForMmdToClose();
end;

procedure MigrateLegacySettings();
var
  Target: String;
begin
  Target := ExpandConstant('{app}\MMDirectEncoder.ini');
  if FileExists(Target) then
    Exit;
  if FileExists(ExpandConstant('{localappdata}\MMDirect Encoder\MMDirectEncoder.ini')) then
    FileCopy(ExpandConstant('{localappdata}\MMDirect Encoder\MMDirectEncoder.ini'), Target, True)
  else if FileExists(ExpandConstant('{localappdata}\FFmpeg Video Encoder\FFmpegVideoEncoder.ini')) then
    FileCopy(ExpandConstant('{localappdata}\FFmpeg Video Encoder\FFmpegVideoEncoder.ini'), Target, True);
end;

function DescribeSelfTest(Code: Integer): String;
begin
  case Code of
    0: Result := '';
    10: Result := CustomMessage('TestNotRegistered');
    11: Result := CustomMessage('TestLoadFailed');
    20: Result := CustomMessage('TestFfmpegMissing');
    21: Result := CustomMessage('TestFfmpegBlocked');
    30: Result := CustomMessage('TestVideoFailed');
    31: Result := CustomMessage('TestExrFailed');
  else
    Result := FmtMessage(CustomMessage('TestUnknown'), [IntToStr(Code)]);
  end;
  if Result <> '' then
    Result := Result + #13#10#13#10 + FmtMessage(CustomMessage('TestLogHint'), [ExpandConstant('{app}\logs\selftest.log')]);
end;

procedure RunSelfTest();
var
  Code: Integer;
begin
  WizardForm.StatusLabel.Caption := SetupMessage(msgStatusRunProgram);
  if not Exec(ExpandConstant('{app}\MMDirectEncoderConfig.exe'), '--selftest', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) then
    Code := 11;
  Log('Self test result: ' + IntToStr(Code));
  SelfTestMessage := DescribeSelfTest(Code);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\MMDirectEncoder');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\FFmpegVideoEncoder');
  end;
  if CurStep = ssPostInstall then
  begin
    MigrateLegacySettings();
    RunSelfTest();
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and (SelfTestMessage <> '') then
  begin
    WizardForm.FinishedLabel.Caption := FmtMessage(CustomMessage('FinishedWithProblem'), [SelfTestMessage]);
    WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  if UninstallSilent() then
    Exit;
  AppDir := ExpandConstant('{app}');
  if MsgBox(CustomMessage('KeepSettingsQuestion'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
  begin
    DeleteFile(AppDir + '\MMDirectEncoder.ini');
    DelTree(AppDir + '\logs', True, True, True);
    RemoveDir(AppDir);
  end;
end;
