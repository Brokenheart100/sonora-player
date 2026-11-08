; Sonora Music 的 Inno Setup 6 安装脚本（M9）。
;
; 用法（装了 Inno Setup 6 之后）：
;   ISCC.exe /DAppVersion=0.1.0 installer\sonora.iss
;
; 前置条件：先跑 tools/package.ps1 生成 dist\Sonora-<版本>-win64\。
; 本脚本只负责"把那个目录装到 Program Files、加开始菜单、写卸载信息"。
;
; 设计取舍：
;   - 用户数据（曲库、设置、封面）不进安装目录，放在 %LOCALAPPDATA%\Sonora\Sonora，
;     卸载时**默认保留**并单独问一句——删库等于删用户的播放记录，不能顺手干。
;   - 不做自动更新：M9 没有更新源，写一个假的检查更新比没有更糟。

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#define AppName "Sonora Music"
#define AppExe "sonora.exe"
#define SourceDir "..\dist\Sonora-" + AppVersion + "-win64"

[Setup]
AppId={{7E1B9B4A-2C6F-4E7B-9C1D-5A3F8E2B6C41}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Sonora contributors
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=Sonora-{#AppVersion}-win64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 需要用户能改安装目录，但不需要管理员权限就能装到自己的用户目录
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#AppExe}
MinVersion=10.0

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 只清安装目录里可能生成的临时物；用户数据不在这个目录里。
Type: filesandordirs; Name: "{app}\data"

[Code]
// 卸载时问一句要不要连数据一起删。默认不删。
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{localappdata}\Sonora');
    if DirExists(DataDir) then
    begin
      if MsgBox('要一起删除曲库和设置吗？' + #13#10 +
                DataDir + #13#10 +
                '选"否"会保留你的播放记录和设置，重装后还能用。',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(DataDir, True, True, True);
      end;
    end;
  end;
end;
