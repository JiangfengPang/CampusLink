#define AppVersion "1.1.4"
[Setup]
AppId={{6B637BB7-F74A-47F5-93D8-A721F0CB9F75}
AppName=校园网 CampusLink
AppVersion={#AppVersion}
AppPublisher=CampusLink
AppComments=喀什大学校园网一键认证工具（非官方）
DefaultDirName={localappdata}\Programs\CampusLink
DefaultGroupName=校园网 CampusLink
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\release
OutputBaseFilename=CampusLink-{#AppVersion}-Setup-x64
SetupIconFile=..\assets\CampusLink.ico
UninstallDisplayIcon={app}\CampusLink.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
AppMutex=Local\CampusLink.KashiUniversity
DisableWelcomePage=no
VersionInfoVersion=1.1.4.0
VersionInfoDescription=CampusLink installer

[Languages]
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimp.CreateDesktopIcon=创建桌面快捷方式
chinesesimp.LaunchCampusLink=启动校园网
english.CreateDesktopIcon=Create a desktop shortcut / 创建桌面快捷方式
english.LaunchCampusLink=Launch CampusLink / 启动校园网

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "..\release\CampusLink-{#AppVersion}-Portable-x64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\校园网 CampusLink"; Filename: "{app}\CampusLink.exe"; WorkingDir: "{app}"
Name: "{group}\卸载校园网 CampusLink"; Filename: "{uninstallexe}"
Name: "{autodesktop}\校园网一键登录"; Filename: "{app}\CampusLink.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\CampusLink.exe"; Description: "{cm:LaunchCampusLink}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SavedCommand: String;
  InstalledCommand: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    InstalledCommand := '"' + ExpandConstant('{app}\CampusLink.exe') + '" --startup';
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'CampusLink', SavedCommand) then
      if CompareText(Trim(SavedCommand), InstalledCommand) = 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'CampusLink');
  end;
end;
