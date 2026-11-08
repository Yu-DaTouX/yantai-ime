[Setup]
AppId=MsimeCloudChoiceProbe
AppName=MsimeCloudChoiceProbe
AppVersion=1
DefaultDirName={localappdata}\MsimeCloudChoiceProbe
PrivilegesRequired=lowest
CreateUninstallRegKey=no
Uninstallable=no
OutputBaseFilename=cloud-choice-probe
Compression=none
DisableProgramGroupPage=yes

[Files]
Source: "..\default_config\config.default.toml"; DestDir: "{param:TARGET}"; DestName: "config.toml"; Flags: onlyifdoesntexist

[Code]
function GetDataDir(Param: String): String;
begin
  Result := ExpandConstant('{param:TARGET|}');
end;

function ResolvePreviousDataDir: String;
begin
  Result := ExpandConstant('{param:PREVIOUS|}');
end;

#include "../cloud_choice.iss"

{ 与 msime_setup.iss 的 PrepareToInstall / MigrateUserDataDir 同序：先记下配置是否已存在，
  再查冲突，然后用同样的 robocopy /MOVE 搬 config.toml，失败时用同一个函数判断要不要中止。}
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  OldDir, NewDir: String;
  ResultCode: Integer;
begin
  OldDir := ResolvePreviousDataDir;
  NewDir := GetDataDir('');
  Result := ConfigMigrationProblem(OldDir, NewDir);
  if (Result <> '') or (OldDir = '') or (CompareText(OldDir, NewDir) = 0) or
     (not DirExists(OldDir)) then
    Exit;
  ForceDirectories(NewDir);
  DropMigratedConfigCopy(OldDir, NewDir);
  if not (Exec(
      ExpandConstant('{sys}\robocopy.exe'),
      '"' + OldDir + '" "' + NewDir + '" config.toml /MOVE /R:0 /W:0 /NJH /NJS /NP /NFL /NDL',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode < 8)) then
    Result := ConfigMigrationFailure(OldDir, NewDir);
end;
