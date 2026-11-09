# 默认全量白霜：构建、更新、真机替换

中文默认词库已经切换为固定版本的**全量白霜**，不是精简推荐版。范围与此前实测约152MiB版一致：
6个 cn_dicts 基础/扩展包＋全部23个默认cell分包；tencent在该上游默认配置中是注释项，corrections
是故意错误的读音别名，二者均不属于那份152MiB对照版。精确文件列表见
`scripts/dictionary_build/frost.lock.json`，许可与修改见 [dictionary-frost-sources.md](dictionary-frost-sources.md)。

## 构建与验证

```bash
# Python标准库；首次下载逐文件验SHA256的源，不需要兄弟词库仓/pypinyin。
python -B scripts/build-dictionary.py --if-stale
python -B scripts/build-dictionary.py --verify
# 指定独立产物目录，例如本次交付：
python -B scripts/build-dictionary.py --output E:/yantai-wt/frost-out --if-stale
python -B scripts/build-dictionary.py --output E:/yantai-wt/frost-out --verify
```

本地完整打包 `installer/Invoke-LocalTest.ps1` 和所有 CI 的历史入口
`scripts/ci/download-dictionaries.sh` 均委派到同一个源构建器。product-lock 的 `frost-source` 模式
绑定白霜提交、配方摘要及独立英文/杂项资源；不再下载旧中文msime.db、五笔或日语资产。

跨SQLite版本允许页布局不同；规范记录按表/拼音/词排序摘要，必须与锁中的
`expected_content_sha256` 一致。任何源或词条内容篡改（即使连旁边的manifest摘要一起改）均不能通过；
允许SQLite页布局差异不等于对任意本地攻击者的完整文件签名验证。
构建器/来源说明变更使缓存失效；新库全部校验通过才整目录切换，失败保留原产物。不要把输出
目录指向用户数据目录；构建器拒绝默认已安装用户目录，但用户也应自行确认自定义路径安全。

### 小占用本机测试

用户在用电脑时至多4个编译任务，低优先级，无前台窗口/按键；不运行探活：

```bash
cmake --build server/build --config Release --parallel 4 -- -m:4 -v:m
pwsh -NoProfile -File scripts/ci/test-server.ps1 -ServerRoot server \
  -StagingRoot E:/yantai-wt/frost-test-data -BuildDir build -SkipProbe
cmake --build windows/build64 --config Release --parallel 4 -- -m:4 -v:m
python -B -m unittest discover -s tests -p test_frost_dictionary.py
python -B -m unittest discover -s tests -p test_product_lock.py
```

本次也使用所提供 yantai-build.sh 的本地副本，**唯一改动是三处 `--parallel`/MSBuild `-m` 限为4**，
以无窗口 BelowNormal 优先级启动；test/tsf 均 BUILD OK。原助手自身没有并行度参数，未修改共享助手。
CI继续按原工作流完整编译和发版；本分支没有push或创建release。

## 更新来源

1. 明确选择一个白霜40位提交，评审每个包的新增/删除/许可变化；不能用浮动HEAD。
2. 更新 frost.lock.json 的逐包大小/SHA256、必要的窄过滤项，再重建取得规范词条摘要。
   这个审计更新步骤应在临时输出目录完成，最后更新 `expected_content_sha256` 与product-lock配方摘要。
   不能通过修改manifest绕过旧的规范内容锁，也不能用旧 `refresh --dictionary-tag` 把默认切回旧库。
3. 固定开发/留出集评测覆盖、首选、延迟及候选变化；用户词日志不参与出厂过滤。
4. 运行升级测试、构建/契约/打包测试；提交对应可重建原料、规则与结果。GPL源码发布应附固定上游
   原始文件缓存和脚本，不仅给一个未来会变化的分支链接。

## 本次产物

`E:\yantai-wt\frost-out` 含 msime.db、完整 desktop dictionary-manifest.json、SHA256SUMS.txt、
独立英文/杂项库、白霜GPL与来源说明、frost-build-report.json，以及便于回放的DictionaryReplay工具及DLL。
它不是安装包，不包含新版TSF/Server，**没有自动部署到用户目录**。

本机产物 msime.db：159,899,648字节，2,027,140条全拼记录；SHA256：
`58ec3baf2a564e98efaedd2e8d264fbd4bc645da477ccaf36ab698a87f2bb31b`。
规范内容摘要：`2e4c5cc77cab601800d214559bc508d6283d75246c51bba6fd3dabeab6ad9344`。
不同SQLite版本复建的文件摘要可不同，应以该次manifest与规范摘要为准。

## 真机替换（由用户择时执行；本任务没有停进程）

**先备份，确认输入会话结束，再停止自动重启和Server。不要一边打字一边复制/替换。**

1. 先在设置页导出完整用户数据备份（如当前版本有此入口），放在数据目录之外；备份含私人词条，
   不要上传。没有该入口也可以等下面停机后做完整目录副本，备份成功前绝不覆盖文件。
2. 在设置页确认真正的数据目录；默认通常是 `%LOCALAPPDATA%\metasequoiaime`，若注册表/安装时改过
   路径，以实际路径为准。切换到其他输入法，提交/清空当前组合串，关闭设置窗口。
3. **由用户**临时退出/停用Watchdog的自动重启，然后退出Server；若只停Server而Watchdog还在，
   它会重新占用数据库。以下脚本只检查是否已退出，不替用户结束进程。
4. 在PowerShell 7中调整 `$data` 后执行以下离线替换。`msime_user.db` 及其WAL/SHM/journal、
   用户Google解码器 `user_dict.dat`、配置、选择/固定位置状态都保留。不要覆盖英文/杂项工作库，
   它们可能已有个人学习；回放程序会把英文用户词也恢复到现有英文库。

```powershell
$ErrorActionPreference = 'Stop'
$source = 'E:\yantai-wt\frost-out'
$data = Join-Path $env:LOCALAPPDATA 'metasequoiaime' # 若非默认，请先改这里！
$backup = 'E:\yantai-wt\frost-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
if (Get-Process MetasequoiaImeServer,MetasequoiaImeWatchdog,MetasequoiaImeSettings -ErrorAction SilentlyContinue) {
    throw '请先自行退出设置/Watchdog/Server，再执行；本脚本不结束任何进程。'
}
if (-not (Test-Path -LiteralPath (Join-Path $data 'msime.db'))) { throw '数据目录不对' }
if (-not (Test-Path -LiteralPath (Join-Path $data 'english.db'))) { throw '不是完整安装；请先用安装包安装' }
if (Test-Path -LiteralPath $backup) { throw '备份目录已存在，换一个新目录' }
# Server已停，复制整个目录才是主库+WAL一致的停机备份。
Copy-Item -LiteralPath $data -Destination $backup -Recurse -Force
$expected = '58ec3baf2a564e98efaedd2e8d264fbd4bc645da477ccaf36ab698a87f2bb31b'
if ((Get-FileHash (Join-Path $source 'msime.db') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw '实验库摘要不符，停止替换'
}
# 只清理旧出厂主库的协调文件，绝不删除 msime_user.db*。
foreach ($suffix in @('-wal','-shm','-journal')) {
    $old = Join-Path $data ('msime.db' + $suffix)
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force }
}
Copy-Item (Join-Path $source 'msime.db') (Join-Path $data 'msime.db') -Force
Copy-Item (Join-Path $source 'dictionary-manifest.json') (Join-Path $data 'dictionary-manifest.json') -Force
Copy-Item (Join-Path $source 'rime-frost-NOTICE.md') $data -Force
Copy-Item (Join-Path $source 'rime-frost-LICENSE.txt') $data -Force
# 日志不是词库的附属垃圾：必须回放，且新manifest必须先到位以触发跨库调频重基。
& (Join-Path $source 'MetasequoiaImeDictionaryReplay.exe') --data-dir $data
if ($LASTEXITCODE -ne 0) { throw "回放失败；不要启动Server，按下文从 $backup 回滚" }
Write-Host "替换与回放成功；完整停机备份：$backup。现在可以恢复原来的Server/Watchdog启动方式。"
```

5. 成功后按原安装版本的正常方式启动Server/Watchdog，检查常用自造词、个人词频/固定位置、
   7/8/9音节长词、英文自造词与快捷短语。测试后再恢复日常输入。不要直接运行此工作树的新Server来
   覆盖原TSF/协议版本；仅换库仍使用现有安装的程序。
6. 回放失败时不要继续输入或启动Server。保持进程退出，将失败的数据目录改名为 `.failed-时间`，
   再把完整备份复制还原到原数据目录（不是向失败目录叠加复制，以免残留WAL混入）。检查旧manifest、
   msime_user.db及sidecar都恢复后再启动。保留失败目录和备份供人工诊断，不静默丢弃学习数据。

自动安装/升级仍使用已有安装器：先保留用户日志及其sidecar，再重建出厂库，然后立即
ReplayUserDictionary。此次增加的真实升级路径测试也覆盖未checkpoint的WAL、自造词、手动改权重、
自动权重重基/保留、删除墓碑、快捷短语/英文及固定/选择状态；没有在用户真机执行这些操作。

## others.db 的符号表补丁

`support` 里固定的 `others.db`（MSIME-Engine `dict-v1.0.0`）没有 `symbol_code` 表，符号混输与 `Shift+S`
符号模式因此查不到任何符号。构建时 `scripts/dictionary_build/symbol_patch.py` 在拷贝 support 文件之后，
用 `symbols.lock.json` 固定的 msime-dictionary 提交（符号源数据按大小和 SHA-256 校验）加上本仓的
`symbol-names-extra.tsv`，重建 `symbol_catalog` 与 `symbol_code`；emoji、颜文字表不动。
`verify` 按 `symbols.lock.json` 的 `others_content_sha256`（逐表排序后的内容摘要，不受各机器 SQLite 版本造成的页布局差异影响）核对 `others.db`。
要增删符号名称，改 `symbol-names-extra.tsv`，重新构建后把新的 `others_content_sha256` 写回锁文件。
