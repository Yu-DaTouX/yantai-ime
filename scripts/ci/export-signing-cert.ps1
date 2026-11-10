# Export the public half of the release signing certificate so the installer can trust it.
#
# A self-signed code-signing certificate does not chain to any root Windows already trusts, and
# the Server's uiAccess=true manifest makes Windows refuse to start the process in that case. The
# installer therefore imports this certificate into the machine's Root and TrustedPublisher stores
# (see TrustSigningCertificate in installer/msime_setup.iss). Only the public key is exported.
#
# A certificate issued by a public CA already chains to a trusted root, and importing a leaf into
# Root would be wrong, so nothing is exported for it and the installer skips the step.
[CmdletBinding()]
param(
    [string]$OutputPath = 'signing/ime-signing.cer',
    [string]$ExpectedSubject = 'CN=YuDaTou IME Code Signing'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $env:CERTIFICATE_BASE64 -or -not $env:CERTIFICATE_PASSWORD) {
    throw 'CERTIFICATE_BASE64 / CERTIFICATE_PASSWORD are required.'
}

$bytes = [Convert]::FromBase64String($env:CERTIFICATE_BASE64)
$certificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($bytes, $env:CERTIFICATE_PASSWORD)

if ($certificate.Subject -ne $certificate.Issuer) {
    Write-Host "Certificate is not self-signed ($($certificate.Issuer)); not exporting it for the installer."
    exit 0
}
if ($certificate.Subject -ne $ExpectedSubject) {
    # The uninstaller removes the trust by this subject name, so a different one would leave it behind.
    throw "Unexpected certificate subject '$($certificate.Subject)', expected '$ExpectedSubject'."
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
[IO.File]::WriteAllBytes($OutputPath, $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
Write-Host "Exported public certificate $($certificate.Thumbprint) to $OutputPath"
