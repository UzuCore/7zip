param(
    [string]$InstallDirectory = (Split-Path -Parent $PSScriptRoot),
    [switch]$Development,
    [switch]$Unregister,
    [switch]$AllUsers,
    [switch]$SelfSignedConsent,
    [string]$LogPath
)
$ErrorActionPreference = 'Stop'
if ($LogPath) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force | Out-Null
    Start-Transcript -Path $LogPath -Force | Out-Null
}
if ([Environment]::OSVersion.Version.Build -lt 22000) { return }
$manifestPath = Join-Path $PSScriptRoot 'AppxManifest.xml'
[xml]$manifest = [IO.File]::ReadAllText($manifestPath)
$name = [string]$manifest.Package.Identity.Name
if ($name -notin @('UzuCore.7zipAlpha.ContextMenu', 'UzuCore.7zipAlpha.ContextMenu.Preview')) {
    throw 'Unexpected package identity.'
}
if ($AllUsers -and $Development) { throw 'Development registration is current-user only.' }
$selfSigningPath = Join-Path $PSScriptRoot 'self-signing.json'
$selfSigning = if (Test-Path -LiteralPath $selfSigningPath) { Get-Content -LiteralPath $selfSigningPath -Raw | ConvertFrom-Json } else { $null }
# MSIX deployment requires machine certificate trust, even for a per-user app.
$storeScope = 'LocalMachine'
$registryScope = 'HKLM:'
if ($selfSigning) {
    if (-not $Unregister -and -not $SelfSignedConsent) {
        throw 'Explicit consent is required to trust the self-signed publisher.'
    }
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Self-signed menu registration or removal requires administrator privileges.'
    }
}
$ownershipPath = if ($selfSigning) { "$registryScope\Software\UzuCore\7zipAlpha\SelfSignedTrust\$($selfSigning.thumbprint)" } else { $null }
function Remove-OwnedTrust {
    if (-not $ownershipPath) { return }
    $owner = Get-ItemProperty -LiteralPath $ownershipPath -ErrorAction SilentlyContinue
    if ($owner.AddedByInstaller -eq 1) {
        $certificatePath = "Cert:\$storeScope\TrustedPeople\$($selfSigning.thumbprint)"
        if (Test-Path -LiteralPath $certificatePath) { Remove-Item -LiteralPath $certificatePath }
        Remove-Item -LiteralPath $ownershipPath
    }
}
if ($Unregister) {
    if ($AllUsers) {
        Get-AppxProvisionedPackage -Online | Where-Object DisplayName -eq $name | ForEach-Object {
            Remove-AppxProvisionedPackage -Online -PackageName $_.PackageName | Out-Null
        }
        Get-AppxPackage -AllUsers -Name $name | ForEach-Object {
            Remove-AppxPackage -Package $_.PackageFullName -AllUsers
        }
    }
    else {
        Get-AppxPackage -Name $name | ForEach-Object { Remove-AppxPackage -Package $_.PackageFullName }
    }
    Remove-OwnedTrust
    return
}
$location = (Resolve-Path -LiteralPath $InstallDirectory).ProviderPath
if (-not (Test-Path -LiteralPath (Join-Path $location '7-zip.dll'))) {
    throw 'The native shell extension is missing from the installation directory.'
}
if ($Development) {
    if (-not $name.EndsWith('.Preview')) { throw 'Use the separate Preview identity for development.' }
    $mode = Get-ItemPropertyValue 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' 'AllowDevelopmentWithoutDevLicense' -ErrorAction SilentlyContinue
    if ($mode -ne 1) { throw 'Developer Mode must already be enabled for loose development registration.' }
    Add-AppxPackage -Register $manifestPath -ExternalLocation $location
}
else {
    $package = Join-Path $PSScriptRoot '7zip-alpha-shell.msix'
    $addedTrust = $false
    try {
        if ($selfSigning) {
            $publicCertificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2((Join-Path $PSScriptRoot '7zip-alpha-signing.cer'))
            $signature = Get-AuthenticodeSignature -LiteralPath $package
            if ($selfSigning.thumbprint -notmatch '^[A-Fa-f0-9]{40}$' -or $publicCertificate.Thumbprint -ne $selfSigning.thumbprint -or $signature.SignerCertificate.Thumbprint -ne $selfSigning.thumbprint -or $publicCertificate.Subject -ne $manifest.Package.Identity.Publisher) {
                throw 'The package signer does not match the pinned self-signing certificate.'
            }
            $eku = $publicCertificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' }
            $constraints = $publicCertificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.19' }
            if ($publicCertificate.HasPrivateKey -or $constraints.CertificateAuthority -or $eku.EnhancedKeyUsages.Count -ne 1 -or $eku.EnhancedKeyUsages[0].Value -ne '1.3.6.1.5.5.7.3.3' -or $publicCertificate.NotAfter -le (Get-Date)) {
                throw 'Only the pinned, unexpired end-entity code-signing certificate may be trusted.'
            }
            $certificatePath = "Cert:\$storeScope\TrustedPeople\$($selfSigning.thumbprint)"
            if (-not (Test-Path -LiteralPath $certificatePath)) {
                # Record ownership before importing, so failed installs can clean up precisely.
                New-Item -Path $ownershipPath -Force | Out-Null
                New-ItemProperty -LiteralPath $ownershipPath -Name AddedByInstaller -Value 1 -PropertyType DWord -Force | Out-Null
                Import-Certificate -FilePath (Join-Path $PSScriptRoot '7zip-alpha-signing.cer') -CertStoreLocation "Cert:\$storeScope\TrustedPeople" | Out-Null
                $addedTrust = $true
            }
        }
        # MSIX deployment trusts an end-entity certificate in TrustedPeople.
        # Generic Authenticode chain policy may still report an untrusted root;
        # do not widen user trust to Root just to satisfy that different policy.
        # Appx deployment below validates the entire signed package and its hashes.
        $packageSignature = Get-AuthenticodeSignature -LiteralPath $package
        if (($selfSigning -and $packageSignature.Status -notin @('Valid', 'NotTrusted', 'UnknownError')) -or (-not $selfSigning -and $packageSignature.Status -ne 'Valid')) {
            throw 'A trusted, signed shell identity package is required for production registration.'
        }
        if ($AllUsers) {
            Add-AppxPackage -Stage $package -ExternalLocation $location
            Add-AppxProvisionedPackage -Online -PackagePath $package -SkipLicense | Out-Null
        }
        else {
            Add-AppxPackage -Path $package -ExternalLocation $location -DeferRegistrationWhenPackagesAreInUse
        }
    } catch {
        if ($addedTrust) { Remove-OwnedTrust }
        throw
    }
}
Write-Output "Registered Windows 11 context menu: $name"
