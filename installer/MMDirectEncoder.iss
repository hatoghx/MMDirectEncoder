#define AppName "MMDirectEncoder"
#define BuildDir "..\..\build"
#define AppVersion GetStringFileInfo(BuildDir + "\MMDirectEncoder.dll", "ProductVersion")
#define FilterClsid "{{D79D43B2-F005-40A4-BE18-AFD19C03E6E6}"
#define VideoCompressorCategory "{{33d9a760-90c8-11d0-bd43-00a0c911ce86}"
#define LegacyFilterCategory "{{083863F1-70DE-11d0-BD40-00A0C911CE86}"

[Setup]
AppId={{DACEB0A6-5C41-42D3-B383-E135FF0847C2}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=MMDirectEncoder Project
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
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
SetupLogging=yes
WizardStyle=modern

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
japanese.SettingsShortcut=MMDirectEncoder 設定
english.SettingsShortcut=MMDirectEncoder Settings
japanese.MmdRunning=MikuMikuDance が起動しています。%n%nMikuMikuDance を終了してから［再試行］を押してください。
english.MmdRunning=MikuMikuDance is running.%n%nClose MikuMikuDance and then press Retry.
japanese.NewerInstalled=新しい版（%1）がすでにインストールされています。%n%n古い版（%2）で上書きしますか？
english.NewerInstalled=A newer version (%1) is already installed.%n%nDo you want to replace it with the older version (%2)?
japanese.KeepSettingsQuestion=設定ファイルとログも削除しますか？%n%n「いいえ」を選ぶと、次回のインストールで同じ設定を使えます。
english.KeepSettingsQuestion=Do you also want to delete the settings file and logs?%n%nChoose "No" to keep them for the next installation.
japanese.FinishedUsage=インストールと動作確認が完了しました。%n%n使い方:%n1. MikuMikuDance（64bit 版）を起動します。%n2. 「ファイル」→「AVIファイルに出力」を選びます。%n3. 「ビデオ圧縮コーデック」で MMDirectEncoder を選び、［詳細設定］で出力形式を選びます。%n%n変換後のファイルは、AVI と同じ場所に保存されます。
english.FinishedUsage=Installation and the self test are complete.%n%nHow to use:%n1. Start MikuMikuDance (64-bit).%n2. Choose File > Export to AVI file.%n3. Select MMDirectEncoder as the video compression codec and choose the output format with the settings button.%n%nThe converted file is saved next to the AVI.
japanese.FinishedWithProblem=インストールは完了しましたが、動作確認で問題が見つかりました。%n%n%1
english.FinishedWithProblem=Installation finished, but the self test found a problem.%n%n%1
japanese.TestNotRegistered=MMD から MMDirectEncoder を呼び出せない状態です。%n対処: インストーラーをもう一度実行してください。
english.TestNotRegistered=MMD cannot load MMDirectEncoder.%nWhat to do: run this installer again.
japanese.TestLoadFailed=MMDirectEncoder.dll を読み込めませんでした。%n考えられる原因: ウイルス対策ソフトによる遮断。%n対処: インストーラーをもう一度実行し、直らない場合はインストール先フォルダーをウイルス対策ソフトの除外に追加してください。%nインストール先: %2
english.TestLoadFailed=MMDirectEncoder.dll could not be loaded.%nPossible cause: blocked by antivirus software.%nWhat to do: run this installer again. If that does not help, add the installation folder to the antivirus exclusions.%nInstallation folder: %2
japanese.TestFfmpegMissing=変換に使う ffmpeg.exe が見つかりません。%n考えられる原因: ウイルス対策ソフトによる隔離。%n対処: ウイルス対策ソフトで隔離を解除するか、インストール先フォルダーを除外に追加してから、インストーラーをもう一度実行してください。%nインストール先: %2
english.TestFfmpegMissing=ffmpeg.exe, which is used for conversion, is missing.%nPossible cause: quarantined by antivirus software.%nWhat to do: restore it in your antivirus software or add the installation folder to the exclusions, then run this installer again.%nInstallation folder: %2
japanese.TestFfmpegBlocked=ffmpeg.exe を実行できませんでした。%n考えられる原因: ウイルス対策ソフトによる遮断、またはファイルの破損。%n対処: インストール先フォルダーをウイルス対策ソフトの除外に追加し、インストーラーをもう一度実行してください。%nインストール先: %2
english.TestFfmpegBlocked=ffmpeg.exe could not be run.%nPossible cause: blocked by antivirus software, or the file is damaged.%nWhat to do: add the installation folder to the antivirus exclusions and run this installer again.%nInstallation folder: %2
japanese.TestVideoFailed=試験用の動画変換に失敗しました。%n考えられる原因: GPU ドライバーの不具合。%n対処: GPU ドライバーを更新してください。出力時は設定画面の「エンコーダー」を「CPU」にすると回避できます。
english.TestVideoFailed=The test video conversion failed.%nPossible cause: a GPU driver problem.%nWhat to do: update the GPU driver. Setting Encoder to CPU in the settings avoids the problem when exporting.
japanese.TestExrFailed=試験用の EXR 書き出しに失敗しました。%n対処: 一時フォルダーの空き容量を確認し、インストーラーをもう一度実行してください。
english.TestExrFailed=The test EXR write failed.%nWhat to do: check the free space of the temporary folder and run this installer again.
japanese.TestUnknown=動作確認を完了できませんでした（コード %1）。%n対処: インストーラーをもう一度実行してください。
english.TestUnknown=The self test could not be completed (code %1).%nWhat to do: run this installer again.
japanese.TestLogHint=詳しい記録: %1
english.TestLogHint=Details: %1

[Files]
Source: "{#BuildDir}\MMDirectEncoder.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\MMDirectEncoderConfig.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\bin\ffmpeg.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#BuildDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\LICENSES\*"; DestDir: "{app}\LICENSES"; Flags: ignoreversion

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
  Result := FindWindowByClassName('Polygon Movie Maker') <> 0;
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

function InitializeSetup(): Boolean;
var
  Installed: String;
begin
  Result := WaitForMmdToClose();
  if not Result then
    Exit;
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{DACEB0A6-5C41-42D3-B383-E135FF0847C2}_is1', 'DisplayVersion', Installed) then
  begin
    if CompareVersions(Installed, '{#AppVersion}') > 0 then
      Result := SuppressibleMsgBox(FmtMessage(CustomMessage('NewerInstalled'), [Installed, '{#AppVersion}']), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
  end;
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
var
  AppDir: String;
begin
  AppDir := ExpandConstant('{app}');
  case Code of
    0: Result := '';
    10: Result := CustomMessage('TestNotRegistered');
    11: Result := FmtMessage(CustomMessage('TestLoadFailed'), ['', AppDir]);
    20: Result := FmtMessage(CustomMessage('TestFfmpegMissing'), ['', AppDir]);
    21: Result := FmtMessage(CustomMessage('TestFfmpegBlocked'), ['', AppDir]);
    30: Result := CustomMessage('TestVideoFailed');
    31: Result := CustomMessage('TestExrFailed');
  else
    Result := FmtMessage(CustomMessage('TestUnknown'), [IntToStr(Code)]);
  end;
  if Result <> '' then
    Result := Result + #13#10#13#10 + FmtMessage(CustomMessage('TestLogHint'), [AppDir + '\logs\selftest.log']);
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
  if (SelfTestMessage <> '') and not WizardSilent() then
    MsgBox(SelfTestMessage, mbError, MB_OK);
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
  if CurPageID = wpFinished then
  begin
    if SelfTestMessage = '' then
      WizardForm.FinishedLabel.Caption := CustomMessage('FinishedUsage')
    else
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
