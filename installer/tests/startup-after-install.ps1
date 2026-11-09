$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# 静默安装（/SILENT）不会走完成页，旧实现只在 wpFinished 的 NextButtonClick 里启动 Server，
# 静默/自动更新装完后 Server 没人拉起。这里静态锁住：启动逻辑在 ssPostInstall 里、
# 先 schtasks /Run 任务、并确认 Server 进程，且不再依赖完成页。
# [Code] 段只在安装器进程里执行，没有运行测试的基建，只能校验源码。

$iss = [IO.File]::ReadAllText((Resolve-Path (Join-Path $PSScriptRoot '../msime_setup.iss')))

function Get-Body([string]$Signature) {
    $start = [regex]::Match($iss, [regex]::Escape($Signature))
    if (-not $start.Success) { throw "msime_setup.iss is missing: $Signature" }
    $end = [regex]::Match($iss.Substring($start.Index), '(?m)^end;\s*$')
    if (-not $end.Success) { throw "unterminated body: $Signature" }
    return $iss.Substring($start.Index, $end.Index)
}

$failures = @()
$step = Get-Body 'procedure CurStepChanged'
$start = Get-Body 'procedure StartImeForUser'

if ($step -notmatch 'StartImeForUser') { $failures += 'CurStepChanged must call StartImeForUser (ssPostInstall)' }
if ($step.IndexOf('StartImeForUser') -lt $step.IndexOf('CreateWatchdogLogonTask')) {
    $failures += 'StartImeForUser must run after CreateWatchdogLogonTask'
}
if ($start -notmatch '/Run /TN "\{#MyWatchdogTaskName\}"') { $failures += 'must start the watchdog with schtasks /Run first' }
if ($start -notmatch 'ShellExecAsOriginalUser') { $failures += 'must fall back to ShellExecAsOriginalUser' }
if ($start -notmatch "WaitForProcess\('\{#MyAppExeName\}'") { $failures += 'must wait for the server process' }
if ($start -match 'MsgBox') { $failures += 'must not show a message box' }
if ($iss -match 'LaunchInstalledComponents\s*;') { $failures += 'launching from the finish page is back (silent installs skip it)' }

if ($failures.Count -gt 0) { $failures | ForEach-Object { Write-Error $_ -ErrorAction Continue }; exit 1 }
Write-Host 'startup-after-install: OK'
