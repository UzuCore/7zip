param([Parameter(Mandatory=$true)][string[]]$Files, [switch]$TrustForBuild)
$ErrorActionPreference = 'Stop'
$config = Get-Content (Join-Path $PSScriptRoot 'ModernShell\self-signing.json') -Raw | ConvertFrom-Json
$cert = Get-Item "Cert:\CurrentUser\My\$($config.thumbprint)" -ErrorAction SilentlyContinue
if ($null -eq $cert) {
    if (-not $env:WINDOWS_SELF_SIGN_PFX -or -not $env:WINDOWS_SELF_SIGN_PASSWORD) { throw 'Persistent self-signing secrets are missing.' }
    $flags = [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::PersistKeySet -bor [Security.Cryptography.X509Certificates.X509KeyStorageFlags]::UserKeySet
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new([Convert]::FromBase64String($env:WINDOWS_SELF_SIGN_PFX), $env:WINDOWS_SELF_SIGN_PASSWORD, $flags)
    if ($cert.Thumbprint -ne $config.thumbprint) { throw 'Signing secret does not match the pinned public certificate.' }
    $store = New-Object Security.Cryptography.X509Certificates.X509Store('My', 'CurrentUser')
    try { $store.Open('ReadWrite'); $store.Add($cert) } finally { $store.Close() }
}
if ($cert.Thumbprint -ne $config.thumbprint -or -not $cert.HasPrivateKey -or $cert.NotAfter -le (Get-Date)) { throw 'Invalid or expired signing identity.' }
if ($TrustForBuild) {
    if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Build trust is restricted to ephemeral GitHub Actions runners.' }
    Import-Certificate -FilePath (Join-Path $PSScriptRoot 'ModernShell\7zip-alpha-signing.cer') -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null
    # Ephemeral CI only: allow generic Authenticode verification as well as MSIX.
    # The shipped installer never imports into Root.
    Import-Certificate -FilePath (Join-Path $PSScriptRoot 'ModernShell\7zip-alpha-signing.cer') -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
}
$sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Filter signtool.exe -Recurse | Where-Object FullName -match '\\x64\\signtool.exe$' | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $sdk) { throw 'Windows SDK SignTool was not found.' }
foreach ($file in $Files) {
    & $sdk.FullName sign /fd SHA256 /sha1 $config.thumbprint /s My /tr http://timestamp.digicert.com /td SHA256 $file
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $file" }
    $signature = Get-AuthenticodeSignature -LiteralPath $file
    if ($signature.SignerCertificate.Thumbprint -ne $config.thumbprint) { throw "Signature identity verification failed: $file" }
    if ($TrustForBuild -and $signature.Status -ne 'Valid') { throw "CI signature trust failed: $file" }
}
