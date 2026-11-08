# Stable releases require the fixed PFX secrets; never fall back to unsigned or SignPath.
$ErrorActionPreference = 'Stop'
if ($env:SIGNING_PROVIDER -ne 'certificate') {
    throw 'WINDOWS_SIGNING_PROVIDER must be certificate.'
}
if (-not $env:CERTIFICATE_BASE64 -or -not $env:CERTIFICATE_PASSWORD) {
    throw 'WINDOWS_SIGNING_CERTIFICATE_BASE64 / WINDOWS_SIGNING_CERTIFICATE_PASSWORD are required.'
}
"signing_enabled=true" >> $env:GITHUB_OUTPUT
"signing_provider=certificate" >> $env:GITHUB_OUTPUT
"asset_suffix=" >> $env:GITHUB_OUTPUT
Write-Host 'Using the fixed repository certificate for payload and installer signing.'
