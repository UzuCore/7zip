param(
    [Parameter(Mandatory=$true)][string]$MsiPath,
    [Parameter(Mandatory=$true)][string]$TransformPath
)
# Read-only MSI actions: do not install, uninstall, or repair any product.
$ErrorActionPreference = 'Stop'
$installer = New-Object -ComObject WindowsInstaller.Installer
$installer.UILevel = 2
try {
    foreach ($culture in @('en-US', 'ko-KR')) {
        $copy = Join-Path ([IO.Path]::GetTempPath()) ('7zip-upgrade-detection-' + [guid]::NewGuid().ToString('N') + '.msi')
        Copy-Item -LiteralPath $MsiPath -Destination $copy
        $database = $installer.OpenDatabase($copy, 1)
        try {
            if ($culture -eq 'ko-KR') {
                [void]$database.ApplyTransform((Resolve-Path -LiteralPath $TransformPath).Path, 0)
                [void]$database.Commit()
            }
        } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) }
        $session = $installer.OpenPackage($copy, 0)
        try {
            $expected = @()
            $upgradeCode = $session.Property('UpgradeCode')
            $version = [Version]$session.Property('ProductVersion')
            foreach ($code in @($installer.RelatedProducts($upgradeCode))) {
                if ($code -ne $session.Property('ProductCode') -and [Version]$installer.ProductInfo($code, 'VersionString') -le $version) {
                    $expected += $code
                }
            }
            if ($session.DoAction('FindRelatedProducts') -ne 1) { throw 'FindRelatedProducts failed.' }
            $detected = @($session.Property('WIX_UPGRADE_DETECTED') -split ';' | Where-Object { $_ })
            foreach ($code in $expected) {
                if ($code -notin $detected) { throw "$culture missed installed product $code" }
            }
            Write-Output "PASS: $culture detects $($expected.Count) existing related product(s): $($expected -join ', ')"
        } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($session) }
    }
} finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer) }
