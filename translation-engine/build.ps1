param(
    [Parameter(Mandatory = $true)][string]$VcpkgRoot,
    [Parameter(Mandatory = $true)][string]$DependenciesRoot,
    [string]$CMake = "cmake"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$deps = [IO.Path]::GetFullPath($DependenciesRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $root) "translate-deps"))
if (-not $deps.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
    $deps -ne $allowed) {
    throw "DependenciesRoot must be inside this worktree or its sibling translate-deps"
}
New-Item -ItemType Directory -Force $deps | Out-Null
$source = Join-Path $deps "CTranslate2"
if (-not (Test-Path (Join-Path $source ".git"))) {
    & git clone --filter=blob:none --no-checkout https://github.com/OpenNMT/CTranslate2.git $source
    if ($LASTEXITCODE) { throw "CTranslate2 clone failed" }
}
& git -C $source fetch --depth 1 origin 0d8bcd362ac75ef860ef161d6f0efad0ae439ff0
if ($LASTEXITCODE) { throw "CTranslate2 source fetch failed" }
& git -C $source checkout --detach 0d8bcd362ac75ef860ef161d6f0efad0ae439ff0
if ($LASTEXITCODE) { throw "CTranslate2 checkout failed" }
# GPU-only thrust has a removed upstream commit in v4.8.1; do not fetch GPU/test submodules.
& git -C $source submodule update --init --depth 1 third_party/cpu_features third_party/spdlog
if ($LASTEXITCODE) { throw "CPU submodules failed" }
& (Join-Path $VcpkgRoot "vcpkg.exe") install onednn:x64-windows sentencepiece:x64-windows --classic --x-wait-for-lock `
    "--x-install-root=$deps/installed" "--x-buildtrees-root=$deps/buildtrees" `
    "--x-packages-root=$deps/packages" "--downloads-root=$deps/downloads"
if ($LASTEXITCODE) { throw "CPU dependencies failed" }
& $CMake -S $PSScriptRoot -B "$deps/engine-build" -G "Visual Studio 18 2026" -A x64 `
    "-DCTRANSLATE2_SOURCE_DIR=$source" "-DCMAKE_PREFIX_PATH=$deps/installed/x64-windows" `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
if ($LASTEXITCODE) { throw "Engine configure failed" }
& $CMake --build "$deps/engine-build" --config Release --parallel 8
if ($LASTEXITCODE) { throw "Engine build failed" }
Write-Host "BUILD OK"
