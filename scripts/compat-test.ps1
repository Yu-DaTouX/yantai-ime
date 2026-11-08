# 真机兼容性测试：在各类软件里用真实按键打字，读回上屏结果，检查候选窗位置，生成报告。
#
#   pwsh -STA -File scripts\compat-test.ps1 [-Apps notepad,chrome,edge,word,excel,terminal,startsearch] [-Only <用例名片段>]
#
# 会占用键盘鼠标几分钟，运行期间不要操作电脑。安全规则：
#   - 只操作测试自己新建的窗口：启动前后对比窗口列表认定，发每个键前都核对前台就是这个窗口，否则立即中止；
#   - 浏览器用独立的临时配置启动单独的实例，打开本地测试页，和日常用的浏览器完全隔离；
#   - 记事本、Word、Excel 若测试前已经在运行，整项跳过，不碰用户的文档；
#   - 测试前保存剪贴板文本，结束后恢复；收尾只关测试自己的窗口/进程，不保存。
[CmdletBinding()]
param(
    [string[]]$Apps = @('notepad', 'chrome', 'edge', 'word', 'excel', 'terminal', 'startsearch'),
    [string]$OutDir = 'E:\yantai-wt\compat',
    [string]$Only = '',
    [switch]$Trace,   # 用例里的 Snap 调用逐步截取整个测试窗口
    [string]$Scratch = 'E:\yantai-wt\compat-scratch'
)
# pwsh -File 传不了数组，逗号分隔的一个字符串在这里拆开。
$Apps = @($Apps | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class U {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint f);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out int v, int s);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static uint Pid(IntPtr h) { uint p; GetWindowThreadProcessId(h, out p); return p; }
  public static List<long> Visible() { var o = new List<long>(); EnumWindows((h,l) => { if (IsWindowVisible(h)) o.Add(h.ToInt64()); return true; }, IntPtr.Zero); return o; }
}
"@

New-Item -ItemType Directory -Force -Path $OutDir, $Scratch | Out-Null
$results = New-Object System.Collections.Generic.List[object]
$script:hwnd = [IntPtr]::Zero      # 本项测试允许的前台窗口
$script:allowPid = 0              # 同一进程的其他窗口（启动闪屏后的主窗口、对话框）也算测试窗口
$script:loose = $null              # 开始菜单搜索这类没法按窗口认定时，按进程名放行
$VK = @{ Space = 0x20; Enter = 0x0D; Esc = 0x1B; Back = 0x08; Shift = 0x10; Ctrl = 0x11; Alt = 0x12; A = 0x41; C = 0x43; Del = 0x2E; Win = 0x5B }

function FgName { $p = [U]::Pid([U]::GetForegroundWindow()); try { (Get-Process -Id $p).ProcessName } catch { '?' } }
function Guard {
    $fg = [U]::GetAncestor([U]::GetForegroundWindow(), 3)   # GA_ROOTOWNER
    if ($script:hwnd -ne [IntPtr]::Zero -and $fg -ne $script:hwnd -and [U]::Pid($fg) -ne $script:allowPid) { throw "ABORT: 前台不是测试窗口（现在是 $(FgName)）" }
    if ($script:loose -and (FgName) -notmatch $script:loose) { throw "ABORT: 前台不是测试窗口（现在是 $(FgName)）" }
}
function Down([byte]$vk) { [U]::keybd_event($vk, [byte][U]::MapVirtualKey($vk, 0), 0, [UIntPtr]::Zero) }
function Up([byte]$vk) { [U]::keybd_event($vk, [byte][U]::MapVirtualKey($vk, 0), 2, [UIntPtr]::Zero) }
function Tap([byte]$vk) { Guard; Down $vk; Start-Sleep -Milliseconds 35; Up $vk; Start-Sleep -Milliseconds 70 }
function Chord([byte[]]$mods, [byte]$vk) { Guard; try { foreach ($m in $mods) { Down $m }; Down $vk; Start-Sleep -Milliseconds 35; Up $vk } finally { foreach ($m in $mods) { Up $m } }; Start-Sleep -Milliseconds 120 }
function TypeKeys([string]$s) { foreach ($c in $s.ToCharArray()) { Tap ([byte][char]([string]$c).ToUpper()) } }

function Cand {
    $srv = Get-Process MetasequoiaImeServer -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $srv) { return [pscustomobject]@{ ok = $false; why = 'Server 没在运行'; rect = $null } }
    $h = [IntPtr]::Zero; $found = [IntPtr]::Zero
    while (($h = [U]::FindWindowEx([IntPtr]::Zero, $h, 'metasequoiaime_windows', 'metaseuqoiaimecandwnd')) -ne [IntPtr]::Zero) {
        if ([U]::Pid($h) -eq $srv.Id) { $found = $h; break }
    }
    if ($found -eq [IntPtr]::Zero) { return [pscustomobject]@{ ok = $false; why = '找不到候选窗'; rect = $null } }
    $r = New-Object U+RECT; [void][U]::GetWindowRect($found, [ref]$r)
    $cloaked = 0; [void][U]::DwmGetWindowAttribute($found, 14, [ref]$cloaked, 4)
    $onScreen = [System.Windows.Forms.Screen]::AllScreens | Where-Object { $_.Bounds.IntersectsWith((New-Object System.Drawing.Rectangle $r.L, $r.T, ($r.R - $r.L), ($r.B - $r.T))) }
    $ok = [U]::IsWindowVisible($found) -and $cloaked -eq 0 -and $onScreen
    $why = if ($ok) { 'OK' } elseif ($cloaked) { '未显示' } else { "在屏幕外($($r.L),$($r.T))" }
    return [pscustomobject]@{ ok = [bool]$ok; why = $why; rect = $r }
}
function Shot($name, $rect) {
    try {
        $bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $rect.R - $rect.L)), ([Math]::Max(1, $rect.B - $rect.T))
        $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($rect.L, $rect.T, 0, 0, $bmp.Size); $g.Dispose()
        $path = Join-Path $OutDir "$name.png"; $bmp.Save($path); $bmp.Dispose(); return $path
    } catch { return '' }
}
$script:snapN = 0
function Snap([string]$label) {
    if (-not $Trace -or $script:hwnd -eq [IntPtr]::Zero) { return }
    $r = New-Object U+RECT; [void][U]::GetWindowRect($script:hwnd, [ref]$r)
    $script:snapN++; [void](Shot ('trace-{0:d2}-{1}' -f $script:snapN, $label) $r)
}
function ReadText {
    # 先清空剪贴板：选区为空时 Ctrl+C 不覆盖剪贴板，会读到上一次的内容。
    [System.Windows.Forms.Clipboard]::Clear(); Start-Sleep -Milliseconds 100
    Chord @($VK.Ctrl) $VK.A; Chord @($VK.Ctrl) $VK.C; Start-Sleep -Milliseconds 250
    $t = [System.Windows.Forms.Clipboard]::GetText(); if ($null -eq $t) { $t = '' }; return $t.Trim()
}
function Clear-Field { Chord @($VK.Ctrl) $VK.A; Tap $VK.Del; Start-Sleep -Milliseconds 150 }

$today = Get-Date
# 用例不选普通词：数字选词、空格选首选都会触发调频，改动用户的词频。日期候选与英文上屏不参与调频。
$cases = @(
    @{ n = 'nihao+空格'; k = { TypeKeys 'nihao' }; f = { Tap $VK.Space }; c = { param($t) $t -eq '你好' }; cand = $true },
    @{ n = 'zaosh+Ctrl+Enter(英文)'; k = { TypeKeys 'zaosh' }; f = { Chord @($VK.Ctrl) $VK.Enter }; c = { param($t) $t -match '^[A-Za-z][A-Za-z '' -]*$' }; cand = $true },
    @{ n = 'rq+数字2(日期)'; k = { TypeKeys 'rq' }; f = { Tap 0x32 }; c = { param($t) $t -match [string]$today.Year }; cand = $true },
    @{ n = 'nihaoa+退格+空格'; k = { TypeKeys 'nihaoa'; Tap $VK.Back }; f = { Tap $VK.Space }; c = { param($t) $t -eq '你好' }; cand = $true },
    @{ n = 'nihao+Esc'; k = { TypeKeys 'nihao' }; f = { Tap $VK.Esc }; c = { param($t) $t -eq '' }; cand = $true },
    @{ n = 'yi截图(键帽emoji)+Esc'; k = { TypeKeys 'yi' }; f = { Tap $VK.Esc }; c = { param($t) $t -eq '' }; cand = $true },
    @{ n = 'meiguo截图(国旗)+Esc'; k = { TypeKeys 'meiguo' }; f = { Tap $VK.Esc }; c = { param($t) $t -eq '' }; cand = $true },
    @{ n = '翻译:Ctrl+T草稿+Enter(英文)'; k = { TypeKeys 'jintiandehuiyi'; Snap 'py1'; Chord @($VK.Ctrl) 0x54; Start-Sleep -Milliseconds 400; Snap 'ctrlT'; TypeKeys 'gaidaoxiawusandian'; Snap 'py2'; Tap $VK.Space; Start-Sleep -Milliseconds 1500; Snap 'space' }; f = { Tap $VK.Enter; Start-Sleep -Milliseconds 500; Snap 'enter'; Chord @($VK.Ctrl) 0x54; Start-Sleep -Milliseconds 300; Snap 'exit' }; c = { param($t) $t -match 'meeting' -and $t -notmatch '[\u4e00-\u9fff]' }; cand = $true },
    @{ n = '翻译:Shift+Enter(中文)'; k = { TypeKeys 'nihao'; Chord @($VK.Ctrl) 0x54; Start-Sleep -Milliseconds 800 }; f = { Chord @($VK.Shift) $VK.Enter; Start-Sleep -Milliseconds 300; Chord @($VK.Ctrl) 0x54 }; c = { param($t) $t -eq '你好' }; cand = $true },
    @{ n = 'Shift切英文abc'; k = { Tap $VK.Shift; Start-Sleep -Milliseconds 200; TypeKeys 'abc'; Tap $VK.Shift }; f = { }; c = { param($t) $t -eq 'abc' }; cand = $false }
)

function Run-Suite($app, [bool]$canRead) {
    foreach ($case in $cases) {
        if ($Only -and $case.n -notlike "*$Only*") { continue }
        $row = [ordered]@{ app = $app; case = $case.n; text = ''; pass = $null; cand = ''; note = '' }
        try {
            if ($canRead) { Clear-Field }
            & $case.k
            Start-Sleep -Milliseconds 350
            if ($case.cand) { $c = Cand; $row.cand = $c.why; if ($c.rect -and $c.ok) { [void](Shot "$app-$($case.n)" $c.rect) } }
            & $case.f
            Start-Sleep -Milliseconds 350
            if ($canRead) { $row.text = ReadText; $row.pass = [bool](& $case.c $row.text) } else { Tap $VK.Esc }
        } catch {
            $row.note = "$_"
            $results.Add([pscustomobject]$row)
            if ("$_" -like 'ABORT*') { throw }
            continue
        }
        $results.Add([pscustomobject]$row)
    }
}

# 启动程序并认出它新建的那个顶层窗口
function Launch([scriptblock]$start, [string]$procRegex, [int]$timeoutSec = 25) {
    $before = [System.Collections.Generic.HashSet[long]]::new([long[]]([U]::Visible()))
    & $start
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        foreach ($h in [U]::Visible()) {
            if ($before.Contains($h)) { continue }
            $ptr = [IntPtr]$h
            $name = try { (Get-Process -Id ([U]::Pid($ptr))).ProcessName } catch { '' }
            $r = New-Object U+RECT; [void][U]::GetWindowRect($ptr, [ref]$r)
            if ($name -match $procRegex -and ($r.R - $r.L) -gt 300 -and ($r.B - $r.T) -gt 200) { return $ptr }
        }
    }
    return [IntPtr]::Zero
}
function Focus($h) {
    for ($i = 0; $i -lt 10; $i++) {
        $fg = [U]::GetAncestor([U]::GetForegroundWindow(), 3)
        if ($fg -eq $h -or [U]::Pid($fg) -eq [U]::Pid($h)) { return $true }
        Down $VK.Alt; [void][U]::SetForegroundWindow($h); Up $VK.Alt; Start-Sleep -Milliseconds 400
    }
    return $false
}

function Close-Owned($o) {
        try {
        if ($o.kill -and $o.kill -match '\\') {
            Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($o.kill) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        } elseif ($o.kill) {
            Stop-Process -Id $o.pid -Force -ErrorAction SilentlyContinue
        } else {
            if ($o.app -eq 'notepad' -and (Focus $o.hwnd)) { $script:hwnd = $o.hwnd; Clear-Field; $script:hwnd = [IntPtr]::Zero }
            [void][U]::PostMessage($o.hwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
        }
    } catch {}
}

$page = Join-Path $Scratch 'ime-test.html'
'<!doctype html><meta charset="utf-8"><title>IME-TEST</title><textarea autofocus style="width:90vw;height:60vh;font-size:22px"></textarea>' | Set-Content -LiteralPath $page -Encoding utf8
$pageUrl = 'file:///' + ($page -replace '\\', '/')
$savedClip = try { [System.Windows.Forms.Clipboard]::GetText() } catch { '' }
$owned = New-Object System.Collections.Generic.List[object]

try {
    foreach ($app in $Apps) {
        Write-Host "== $app"
        $script:hwnd = [IntPtr]::Zero; $script:loose = $null; $script:allowPid = 0
        $skip = $null; $read = $true; $post = { }; $h = [IntPtr]::Zero; $kill = $null
        switch ($app) {
            'notepad' {
                if (Get-Process Notepad -ErrorAction SilentlyContinue) { $skip = '测试前记事本已在运行，跳过以免碰到你的文档' }
                else { $h = Launch { & "$env:windir\System32\notepad.exe" } '^Notepad$' }
            }
            'chrome' {
                $prof = Join-Path $Scratch 'chrome-profile'; $kill = $prof
                $h = Launch { Start-Process 'C:\Program Files\Google\Chrome\Application\chrome.exe' -ArgumentList "--user-data-dir=$prof", '--no-first-run', '--no-default-browser-check', '--new-window', $pageUrl } '^chrome$'
                $post = { Start-Sleep 1 }
            }
            'edge' {
                $prof = Join-Path $Scratch 'edge-profile'; $kill = $prof
                $h = Launch { Start-Process 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe' -ArgumentList "--user-data-dir=$prof", '--inprivate', '--no-first-run', '--no-default-browser-check', '--new-window', $pageUrl } '^msedge$'
                $post = { Start-Sleep 1 }
            }
            'word' {
                if (Get-Process WINWORD -ErrorAction SilentlyContinue) { $skip = '测试前 Word 已在运行，跳过' }
                else { $h = Launch { Start-Process 'C:\Program Files\Microsoft Office\root\Office16\WINWORD.EXE' -ArgumentList '/q', '/w' } '^WINWORD$' 40; $kill = 'WINWORD' }
            }
            'excel' {
                if (Get-Process EXCEL -ErrorAction SilentlyContinue) { $skip = '测试前 Excel 已在运行，跳过' }
                else { $csv = Join-Path $Scratch 'ime-test.csv'; '' | Set-Content -LiteralPath $csv; $h = Launch { Start-Process 'C:\Program Files\Microsoft Office\root\Office16\EXCEL.EXE' -ArgumentList "`"$csv`"" } '^EXCEL$' 40; $kill = 'EXCEL'; $read = $false; $post = { Start-Sleep 1 } }
            }
            'terminal' {
                $h = Launch { Start-Process wt -ArgumentList '-w', 'new', 'pwsh', '-NoProfile', '-NoLogo' } '^WindowsTerminal$'; $read = $false; $post = { Start-Sleep 2 }
            }
            'startsearch' {
                $script:loose = '^(SearchHost|StartMenuExperienceHost)$'; Tap $VK.Win; Start-Sleep 2; $read = $false
                if ((FgName) -notmatch $script:loose) { $skip = '开始菜单没有打开' }
            }
            default { $skip = '未知程序' }
        }
        if ($app -ne 'startsearch' -and -not $skip -and $h -eq [IntPtr]::Zero) { $skip = '没等到它的窗口出现' }
        if ($h -ne [IntPtr]::Zero) { $owned.Add([pscustomobject]@{ app = $app; hwnd = $h; kill = $kill; pid = [U]::Pid($h) }) }
        if ($skip) { $results.Add([pscustomobject]@{ app = $app; case = '-'; text = ''; pass = $null; cand = ''; note = $skip }); continue }
        if ($app -ne 'startsearch') {
            if (-not (Focus $h)) { $results.Add([pscustomobject]@{ app = $app; case = '-'; text = ''; pass = $null; cand = ''; note = '切不到前台' }); continue }
            $script:hwnd = $h; $script:allowPid = [U]::Pid($h)
        }
        & $post
        Run-Suite $app $read
        if ($app -eq 'startsearch') { Tap $VK.Esc }
        $script:hwnd = [IntPtr]::Zero; $script:loose = $null
        # 测完马上关，免得它在测下一个程序时跳回前台
        foreach ($o in @($owned | Where-Object app -eq $app)) { Close-Owned $o; [void]$owned.Remove($o) }
        Start-Sleep -Seconds 1
    }
}
catch { Write-Host "$_" }
finally {
    $script:hwnd = [IntPtr]::Zero; $script:loose = $null
    foreach ($o in $owned) { Close-Owned $o }
    try { if ($savedClip) { [System.Windows.Forms.Clipboard]::SetText($savedClip) } } catch {}
    $md = @('| 程序 | 用例 | 结果 | 上屏 | 候选窗 | 备注 |', '|---|---|---|---|---|---|')
    foreach ($r in $results) {
        $res = if ($null -eq $r.pass) { '—' } elseif ($r.pass) { '✓' } else { '✗' }
        $md += "| $($r.app) | $($r.case) | $res | $(($r.text -replace '\|','/') -replace '\r?\n',' ') | $($r.cand) | $($r.note -replace '\|','/') |"
    }
    $md | Set-Content -LiteralPath (Join-Path $OutDir 'report.md') -Encoding utf8
    Write-Host "报告：$(Join-Path $OutDir 'report.md')"
}
exit 0
