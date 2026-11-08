param(
    [Parameter(Mandatory = $true)][string]$ServerRoot,
    [Parameter(Mandatory = $true)][string]$StagingRoot,
    [string]$BuildDir = 'build',
    [switch]$SkipProbe
)
$ErrorActionPreference = 'Stop'
$automationRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$server = (Resolve-Path $ServerRoot).Path
$radical = Join-Path $automationRoot 'engine/radical/decomposition.txt'
if (-not (Test-Path -LiteralPath $radical -PathType Leaf)) {
    throw "Radical decomposition table not found at $radical; the engine is vendored in-tree, so this is an incomplete checkout"
}
python (Join-Path $automationRoot 'scripts/product_lock.py') fetch-dictionaries --staging-root $StagingRoot
if ($LASTEXITCODE -ne 0) { throw 'Could not provision locked dictionaries' }
$verified = (Resolve-Path (Join-Path $StagingRoot 'MetasequoiaImeDict/out')).Path
# Some Windows-only helpers still resolve LOCALAPPDATA; isolate those and the
# cross-platform engine under the same root so no installed dictionary is used.
$env:LOCALAPPDATA = Join-Path (Resolve-Path $StagingRoot).Path 'user-local'
$data = Join-Path $env:LOCALAPPDATA 'metasequoiaime'
# This is the private staging profile, never the installed profile. Reject stale
# WAL/learning left by an earlier test run when switching factory generations.
if (Test-Path -LiteralPath $data) { Remove-Item -LiteralPath $data -Recurse -Force }
New-Item -ItemType Directory -Force -Path $data | Out-Null
Copy-Item (Join-Path $verified '*') -Destination $data -Force
# U 模式的拆字表按 assets 清单的 radical/decomposition.txt 相对路径部署。
New-Item -ItemType Directory -Force -Path (Join-Path $data 'radical') | Out-Null
Copy-Item $radical -Destination (Join-Path $data 'radical') -Force
Copy-Item (Join-Path $server 'assets/tables/*') -Destination $data -Force
Copy-Item (Join-Path $server 'assets/config/config.toml') -Destination $data -Force
$env:METASEQUOIA_IME_DATA_DIR = $data
ctest --test-dir (Join-Path $server $BuildDir) -C Release --output-on-failure --timeout 120
if ($LASTEXITCODE -ne 0) { throw 'Server integration tests failed for the locked product' }
python (Join-Path $automationRoot 'tests/test_frost_upgrade.py') --replay (Join-Path $server "$BuildDir/bin/Release/MetasequoiaImeDictionaryReplay.exe") --factory $verified
if ($LASTEXITCODE -ne 0) { throw 'Frost dictionary upgrade/replay test failed' }
if (-not $SkipProbe) { & (Join-Path $PSScriptRoot 'probe-server.ps1') -ServerRoot $server -BuildDir $BuildDir }
