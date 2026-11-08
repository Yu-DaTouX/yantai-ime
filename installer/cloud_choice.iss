function SameFileContent(const A, B: String): Boolean;
begin
  try
    Result := CompareText(GetSHA256OfFile(A), GetSHA256OfFile(B)) = 0;
  except
    Result := False;
  end;
end;

function MigratedFileConflicts(const OldDir, NewDir, FileName: String): Boolean;
begin
  Result :=
    FileExists(AddBackslash(OldDir) + FileName) and
    FileExists(AddBackslash(NewDir) + FileName) and
    (not SameFileContent(AddBackslash(OldDir) + FileName, AddBackslash(NewDir) + FileName));
end;

{ 迁移前先检查冲突。迁移时再次检查，防止目录内容在停机期间改变。
  两份内容相同说明上次迁移已经复制过去、只是没删掉源文件，重试时不算冲突。
  custom_translations.txt 同样要查：它是用户手写的翻译覆盖表，没有出厂副本，
  robocopy /MOVE 会直接用旧目录那份盖掉目标目录那份。}
function ConfigMigrationProblem(const OldDir, NewDir: String): String;
begin
  Result := '';
  if (OldDir = '') or (CompareText(OldDir, NewDir) = 0) or
     (not DirExists(OldDir)) then
    Exit;
  if MigratedFileConflicts(OldDir, NewDir, 'config.toml') then
    Result := '目标数据目录已有 config.toml，不能覆盖原有配置。请选择其他数据目录。'
  else if MigratedFileConflicts(OldDir, NewDir, 'custom_translations.txt') then
    Result :=
      '目标数据目录已有内容不同的 custom_translations.txt，不能覆盖原有的自定义翻译。' + #13#10 +
      '请选择其他数据目录，或先手动合并两份文件。';
end;

{ 冲突检查通过后两份还同时存在，只能是上次迁移留下的相同副本，删掉源文件再搬。}
procedure DropMigratedCopy(const OldDir, NewDir, FileName: String);
begin
  if FileExists(AddBackslash(OldDir) + FileName) and
     FileExists(AddBackslash(NewDir) + FileName) then
    DeleteFile(AddBackslash(OldDir) + FileName);
end;

procedure DropMigratedConfigCopy(const OldDir, NewDir: String);
begin
  DropMigratedCopy(OldDir, NewDir, 'config.toml');
  DropMigratedCopy(OldDir, NewDir, 'custom_translations.txt');
end;

{ 旧配置没搬过去，新目录就会装上出厂配置，用户的设置会被悄悄重置，
  所以必须中止安装。}
function ConfigMigrationFailure(const OldDir, NewDir: String): String;
begin
  Result := '';
  if FileExists(AddBackslash(OldDir) + 'config.toml') and
     (not FileExists(AddBackslash(NewDir) + 'config.toml')) then
    Result :=
      '无法把用户配置从 ' + OldDir + ' 移动到 ' + NewDir + '。' + #13#10 +
      '请确认输入法相关进程已全部退出、目标磁盘可写，然后重试安装。' + #13#10 +
      '你的配置仍留在 ' + OldDir + '，本次安装没有改动它。';
end;

{ 自定义翻译没搬过去，新目录里就没有覆盖表，用户只会看到翻译回退成大表释义，不会有任何报错。}
function TranslationMigrationFailure(const OldDir, NewDir: String): String;
begin
  Result := '';
  if FileExists(AddBackslash(OldDir) + 'custom_translations.txt') and
     (not FileExists(AddBackslash(NewDir) + 'custom_translations.txt')) then
    Result :=
      '无法把自定义翻译 custom_translations.txt 从 ' + OldDir + ' 移动到 ' + NewDir + '。' + #13#10 +
      '请确认文件没有被其他程序占用、目标磁盘可写，然后重试安装。' + #13#10 +
      '该文件仍留在 ' + OldDir + '，本次安装没有改动它。';
end;
