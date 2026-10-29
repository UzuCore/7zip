param([string]$Repository = 'UzuCore/7zip')
$ErrorActionPreference = 'Stop'
$configPath = Join-Path $PSScriptRoot 'ModernShell\self-signing.json'
if (Test-Path -LiteralPath $configPath) {
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $cert = Get-Item "Cert:\CurrentUser\My\$($config.thumbprint)"
    if (-not $cert.HasPrivateKey) { throw 'Existing signing key is unavailable. Do not rotate it automatically.' }
} else {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=7zipAlpha UzuCore' -FriendlyName '7zip+alpha release signing' -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyUsage DigitalSignature -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3) -TextExtension @('2.5.29.19={critical}{text}ca=0')
    $config = [ordered]@{ subject = $cert.Subject; thumbprint = $cert.Thumbprint; expires = $cert.NotAfter.ToUniversalTime().ToString('o') }
    [IO.File]::WriteAllText($configPath, ($config | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
}
Export-Certificate -Cert $cert -FilePath (Join-Path $PSScriptRoot 'ModernShell\7zip-alpha-signing.cer') | Out-Null
function Set-GitHubSecret([string]$Name, [string]$Value) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = (Get-Command gh).Source
    $start.Arguments = "secret set $Name --repo $Repository"
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($start)
    try {
        $process.StandardInput.Write($Value)
        $process.StandardInput.Close()
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "Could not save GitHub secret: $Name" }
    } finally { $process.Dispose() }
}
$random = New-Object byte[] 48
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($random) } finally { $rng.Dispose() }
$password = [Convert]::ToBase64String($random)
try {
    # Keep export and password in memory; no private-key files or command-line secrets.
    $pfx = $cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $password)
    Set-GitHubSecret 'WINDOWS_SELF_SIGN_PFX' ([Convert]::ToBase64String($pfx))
    Set-GitHubSecret 'WINDOWS_SELF_SIGN_PASSWORD' $password
} finally {
    if ($pfx) { [Array]::Clear($pfx, 0, $pfx.Length) }
    $password = $null
}
& gh variable set WINDOWS_SIGNING_MODE --repo $Repository --body self-signed
if ($LASTEXITCODE -ne 0) { throw 'Could not configure signing mode.' }
Write-Output "Configured persistent self-signing identity: $($cert.Thumbprint)"
Write-Output 'The private key remains in this account''s Windows personal certificate store and encrypted GitHub Actions secrets. No trust store was changed.'
