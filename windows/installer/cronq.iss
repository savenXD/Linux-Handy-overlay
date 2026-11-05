; Cronq Inno Setup Installer Script
; Targets Windows 10 (2004+) and Windows 11 x64
; Per-user installation (no administrator privileges required)

#ifndef AppVersion
  #define AppVersion Trim(FileRead(FileOpen("..\VERSION")))
#endif

[Setup]
AppId=Cronq.Windows
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
AppName=Cronq
AppVersion={#AppVersion}
AppPublisher=Cronq
DefaultDirName={localappdata}\Programs\Cronq
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=cronq-setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
ChangesEnvironment=yes
UninstallDisplayIcon={app}\cronqw.exe
CloseApplications=no

[Tasks]
Name: "autostart"; Description: "Start Cronq automatically when I sign in"; GroupDescription: "Options:"; Flags: checkedonce
Name: "addtopath"; Description: "Add Cronq to user PATH (enables 'cronq' in any terminal)"; GroupDescription: "Options:"; Flags: unchecked

[Files]
Source: "..\dist\cronq.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\cronqw.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Cronq"; Filename: "{app}\cronqw.exe"; Parameters: "--supervise --show"; WorkingDir: "{app}"

[InstallDelete]
; The kill switch is native since 1.2; remove the old PowerShell helper on upgrade.
Type: files; Name: "{app}\killswitch.ps1"

[Run]
Filename: "{app}\cronqw.exe"; Parameters: "--supervise --show"; Description: "Launch Cronq"; Flags: nowait postinstall skipifsilent

[Registry]
; Autostart Run key
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Cronq"; ValueData: """{app}\cronqw.exe"" --supervise"; Flags: uninsdeletevalue; Tasks: autostart
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "Cronq"; Flags: deletevalue; Check: not WizardIsTaskSelected('autostart')
Root: HKCU; Subkey: "Software\Cronq\Installer"; ValueType: dword; ValueName: "PathAdded"; ValueData: "1"; Flags: uninsdeletevalue; Check: NeedsAddPath(ExpandConstant('{app}')); Tasks: addtopath
; User PATH addition
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{code:GetUserPath|{app}}"; Check: NeedsAddPath(ExpandConstant('{app}')); Tasks: addtopath

[Code]
var
  PurgeCheckBox: TCheckBox;
  ShouldPurge: Boolean;

function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + UpperCase(Param) + ';', ';' + UpperCase(OrigPath) + ';') = 0;
end;

function GetUserPath(Param: string): string;
var
  Original: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Original) then Original := '';
  if Original = '' then Result := Param
  else Result := Original + ';' + Param;
end;

function StopRunningCronq(Dir: string): Boolean;
var
  ResultCode: Integer;
  Exe: string;
  Attempt: Integer;
begin
  Result := True;
  Exe := Dir + '\cronq.exe';
  if FileExists(Exe) then
  begin
    if not Exec(Exe, 'kill', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin Result := False; exit; end;
    for Attempt := 1 to 150 do begin
      if FindWindowByClassName('CronqOverlayWindow') = 0 then exit;
      Sleep(100);
    end;
    Result := False;
  end;
end;

function IsWebView2Installed(): Boolean;
var
  pv: string;
begin
  Result := False;
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', pv) then
  begin
    if (pv <> '') and (pv <> '0.0.0.0') then begin Result := True; exit; end;
  end;
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', pv) then
  begin
    if (pv <> '') and (pv <> '0.0.0.0') then begin Result := True; exit; end;
  end;
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', pv) then
  begin
    if (pv <> '') and (pv <> '0.0.0.0') then begin Result := True; exit; end;
  end;
end;

procedure EnsureWebView2Runtime();
var
  BootstrapperPath: string;
  ResultCode: Integer;
begin
  if not IsWebView2Installed() then
  begin
    BootstrapperPath := ExpandConstant('{tmp}\MicrosoftEdgeWebview2Setup.exe');
    try
      DownloadTemporaryFile('https://go.microsoft.com/fwlink/p/?LinkId=2124703', 'MicrosoftEdgeWebview2Setup.exe', '', nil);
      if FileExists(BootstrapperPath) then
      begin
        if not Exec(BootstrapperPath, '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
          RaiseException('Could not start WebView2 installer');
        if (ResultCode <> 0) and (ResultCode <> 3010) then
          RaiseException('WebView2 installer failed: ' + IntToStr(ResultCode));
        if not IsWebView2Installed() then
          RaiseException('WebView2 runtime was not detected after installation');
      end;
    except
      MsgBox('Microsoft Edge WebView2 Runtime is required for full functionality.'#13#10 +
             'Please download and install it manually from:'#13#10 +
             'https://go.microsoft.com/fwlink/p/?LinkId=2124703'#13#10 +
             'Cronq will degrade gracefully in fallback mode until installed.',
             mbInformation, MB_OK);
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if not StopRunningCronq(ExpandConstant('{app}')) then begin
    Result := 'Cronq did not stop. Close it and retry setup.'; exit;
  end;
  EnsureWebView2Runtime();
  Result := '';
end;

procedure RemoveAppPath();
var
  Original, Part, Updated, AppDir: string;
  P: Integer;
  Owned: Cardinal;
begin
  if not RegQueryDWordValue(HKEY_CURRENT_USER, 'Software\Cronq\Installer', 'PathAdded', Owned) then exit;
  if Owned <> 1 then exit;
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Original) then exit;
  Updated := '';
  AppDir := RemoveBackslashUnlessRoot(ExpandConstant('{app}'));
  while Original <> '' do begin
    P := Pos(';', Original);
    if P = 0 then begin Part := Original; Original := ''; end
    else begin Part := Copy(Original, 1, P-1); Delete(Original, 1, P); end;
    if CompareText(RemoveBackslashUnlessRoot(Trim(Part)), AppDir) <> 0 then begin
      if Updated <> '' then Updated := Updated + ';';
      Updated := Updated + Part;
    end;
  end;
  RegWriteExpandStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Updated);
end;

function InitializeUninstall(): Boolean;
var
  Choice: TSetupForm;
  Confirm, CancelButton: TNewButton;
begin
  Result := True;
  ShouldPurge := False;
  if UninstallSilent then exit;
  Choice := CreateCustomForm(ScaleX(480), ScaleY(120), False, False);
  try
    Choice.Caption := 'Cronq data';
    Choice.ClientWidth := ScaleX(480);
    Choice.ClientHeight := ScaleY(120);
    PurgeCheckBox := TCheckBox.Create(Choice);
    PurgeCheckBox.Parent := Choice;
    PurgeCheckBox.SetBounds(ScaleX(16), ScaleY(20), ScaleX(450), ScaleY(40));
    PurgeCheckBox.Caption := 'Also delete settings, logins and screenshot index';
    PurgeCheckBox.Checked := False;
    Confirm := TNewButton.Create(Choice);
    Confirm.Parent := Choice;
    Confirm.SetBounds(ScaleX(360), ScaleY(75), ScaleX(100), ScaleY(25));
    Confirm.Caption := 'Continue'; Confirm.ModalResult := mrOk; Confirm.Default := True;
    CancelButton := TNewButton.Create(Choice);
    CancelButton.Parent := Choice;
    CancelButton.SetBounds(ScaleX(250), ScaleY(75), ScaleX(100), ScaleY(25));
    CancelButton.Caption := 'Cancel'; CancelButton.ModalResult := mrCancel; CancelButton.Cancel := True;
    Result := Choice.ShowModal() = mrOk;
    ShouldPurge := Result and PurgeCheckBox.Checked;
  finally
    Choice.Free();
    PurgeCheckBox := nil;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  AppDataDir: string;
  LocalAppDataDir: string;
begin
  if CurUninstallStep = usUninstall then
  begin
    if not StopRunningCronq(ExpandConstant('{app}')) then
      RaiseException('Cronq did not stop. Close it and retry uninstall.');
    RemoveAppPath();
  end
  else if CurUninstallStep = usPostUninstall then
  begin
    if ShouldPurge then
    begin
      AppDataDir := ExpandConstant('{userappdata}\cronq');
      LocalAppDataDir := ExpandConstant('{localappdata}\cronq');
      DelTree(AppDataDir, True, True, True);
      DelTree(LocalAppDataDir, True, True, True);
    end;
  end;
end;
