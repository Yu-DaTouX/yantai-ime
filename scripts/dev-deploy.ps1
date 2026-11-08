# 把本地编译好的 Server / TSF 直接换进已安装的输入法，用来在本机快速验证改动，不走云端打包。
#
# 需要管理员权限（写 Program Files 与 System32\IME），由 E:\yantai-wt\yantai-deploy.sh 提权调用。
# 签名用当前用户证书库里的固定代码签名证书（CN=YuDaTou IME Code Signing），它的公钥必须已导入
# 本机受信任根与受信任发布者：Server 带 uiAccess=true，没有可信签名 Windows 不让它启动。
#
#   pwsh -File scripts\dev-deploy.ps1 -RepoRoot E:\水杉输入法 -Server -Tsf
#
# 只替换已安装目录里已有的文件，不新增也不删除；被替换的文件留一份 .prev 备份，部署失败时可手动换回。
# 结束后不启动 Server：uiAccess 程序不能从管理员进程拉起（会以管理员身份运行），由调用方以普通用户身份启动。
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$RepoRoot,
    [switch]$Server,
    [switch]$Tsf,
    [string]$CertificateSubject = 'CN=YuDaTou IME Code Signing',
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($LogPath) { Start-Transcript -LiteralPath $LogPath -Force | Out-Null }

try {
    $identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw '需要管理员权限运行。'
    }

    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq $CertificateSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) { throw "当前用户证书库里没有可用的签名证书：$CertificateSubject" }

    $signTool = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $signTool) { throw '找不到 signtool.exe（Windows SDK）。' }

    $staging = Join-Path ([IO.Path]::GetTempPath()) ('msime-deploy-' + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $staging | Out-Null

    # 本地编译的 Server 不带 uiAccess 清单（云端由 scripts/ci/embed-server-manifest.ps1 嵌入）。
    # 漏了它 Server 照样能跑，但候选窗拿不到 uiAccess 的 Z 序，在 Chromium 系宿主里会被压在下面看不见。
    $mt = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin\*\x64\mt.exe') |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    $serverManifest = Join-Path $RepoRoot 'server\MetasequoiaImeServer.manifest'

    function Sign-Copy([string]$source) {
        if (-not (Test-Path -LiteralPath $source)) { throw "编译产物不存在：$source" }
        $target = Join-Path $staging ([IO.Path]::GetFileName($source))
        Copy-Item -LiteralPath $source -Destination $target -Force
        if ([IO.Path]::GetFileName($source) -eq 'MetasequoiaImeServer.exe') {
            if (-not $mt) { throw '找不到 mt.exe（Windows SDK），无法嵌入 uiAccess 清单。' }
            & $mt -nologo -manifest $serverManifest "-outputresource:$target;1" | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "嵌入 uiAccess 清单失败：$LASTEXITCODE" }
            $probe = Join-Path $staging 'server-manifest.xml'
            & $mt -nologo "-inputresource:$target;#1" "-out:$probe" | Out-Null
            if ((Get-Content -LiteralPath $probe -Raw) -notmatch 'uiAccess\s*=\s*["'']true["'']') { throw '嵌入后读回的清单没有 uiAccess=true。' }
            Write-Host '已嵌入 uiAccess 清单'
        }
        & $signTool sign /fd SHA256 /sha1 $cert.Thumbprint /s My $target | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "签名失败：$source" }
        return $target
    }

    function Replace-File([string]$signed, [string]$installed) {
        $backup = "$installed.prev"
        if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue }
        # 正在被别的进程加载的 DLL 不能覆盖，但可以改名；改名后新进程加载新文件，老进程继续用旧的。
        if (Test-Path -LiteralPath $backup) { $backup = "$installed.prev-" + (Get-Date -Format 'HHmmss') }
        Rename-Item -LiteralPath $installed -NewName ([IO.Path]::GetFileName($backup))
        Copy-Item -LiteralPath $signed -Destination $installed
        Write-Host "已替换 $installed"
    }

    if ($Server) {
        $serverDir = Join-Path $env:ProgramFiles 'metasequoiaime\server'
        $buildDir = Join-Path $RepoRoot 'server\build\bin\Release'
        $names = Get-ChildItem -LiteralPath $serverDir -Filter '*.exe' -File | ForEach-Object Name
        $signed = @{}
        foreach ($name in $names) { $signed[$name] = Sign-Copy (Join-Path $buildDir $name) }

        # 先停看门狗，免得它在替换中途把 Server 拉起来；再停 Server 与设置页。
        foreach ($proc in 'MetasequoiaImeWatchdog', 'MetasequoiaImeServer', 'MetasequoiaImeSettings') {
            Get-Process -Name $proc -ErrorAction SilentlyContinue | Stop-Process -Force
        }
        Start-Sleep -Milliseconds 800
        foreach ($name in $names) { Replace-File $signed[$name] (Join-Path $serverDir $name) }
    }

    if ($Tsf) {
        $versionDir = (Get-ItemProperty 'HKLM:\SOFTWARE\Metasequoia\MetasequoiaIME' -Name VersionDir).VersionDir
        if (-not $versionDir) { throw '注册表里没有 VersionDir，输入法可能没装好。' }
        $pairs = @(
            @{ Build = 'windows\build64\Release\MetasequoiaImeTsf.dll'; Root = Join-Path $env:windir 'System32\IME\MetasequoiaIME' },
            @{ Build = 'windows\build32\Release\MetasequoiaImeTsf.dll'; Root = Join-Path $env:windir 'SysWOW64\IME\MetasequoiaIME' }
        )
        foreach ($pair in $pairs) {
            $source = Join-Path $RepoRoot $pair.Build
            $installed = Join-Path (Join-Path $pair.Root $versionDir) 'MetasequoiaImeTsf.dll'
            if (-not (Test-Path -LiteralPath $source)) { Write-Host "跳过（没有编译产物）：$source"; continue }
            if (-not (Test-Path -LiteralPath $installed)) { Write-Host "跳过（未安装）：$installed"; continue }
            Replace-File (Sign-Copy $source) $installed
        }
    }

    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host 'DEPLOY OK'
}
catch {
    Write-Host "DEPLOY FAILED: $_"
    exit 1
}
finally {
    if ($LogPath) { Stop-Transcript | Out-Null }
}
