param([Parameter(Mandatory=$true)][string]$SignedInstaller)
$ErrorActionPreference = 'Stop'
$source = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('7zip-update-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
foreach ($name in @('Check-Updates.ps1', 'Update-Verification.ps1')) {
    [IO.File]::WriteAllText((Join-Path $fixture $name), [IO.File]::ReadAllText((Join-Path $source $name)), (New-Object Text.UTF8Encoding($true)))
}
$thumbprint = (Get-AuthenticodeSignature -LiteralPath $SignedInstaller).SignerCertificate.Thumbprint
@{ schema=1; buildId='100'; arch='x64'; publisherThumbprint=$thumbprint } | ConvertTo-Json | Set-Content (Join-Path $fixture 'UpdateConfig.json') -Encoding UTF8
$global:SevenZipUpdateTesttag = 'build-2026.10.30'
$global:SevenZipUpdateTesturl = 'https://github.com/UzuCore/7zip/releases/download/' + $global:SevenZipUpdateTesttag + '/update-windows.json'
$global:SevenZipUpdateTestbuild = '101'
$global:SevenZipUpdateTestattempt = 0
$global:SevenZipUpdateTestpublisher = $thumbprint
$global:SevenZipUpdateTestmissing = $false
$global:SevenZipUpdateTestraw = $false
function Invoke-RestMethod {
    param($Uri, $Headers, $TimeoutSec)
    if ($Uri -like '*api.github.com*') {
        $assets = @()
        if (-not $global:SevenZipUpdateTestmissing) { $assets = @(@{ name='update-windows.json'; browser_download_url=$global:SevenZipUpdateTesturl }) }
        return [pscustomobject]@{ draft=$false; prerelease=$false; tag_name=$global:SevenZipUpdateTesttag; assets=$assets }
    }
    $manifest = [pscustomobject]@{ schema=1; releaseTag=$global:SevenZipUpdateTesttag; buildId=$global:SevenZipUpdateTestbuild; buildAttempt=$global:SevenZipUpdateTestattempt; windows=@{ x64=@{ name='7zip-windows-x64.exe'; sha256=('a'*64); publisherThumbprint=$global:SevenZipUpdateTestpublisher } } }
    if ($global:SevenZipUpdateTestraw) { return ($manifest | ConvertTo-Json -Depth 5) }
    return $manifest
}
function Expect-Rejection([scriptblock]$Action, [string]$Name) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (-not $rejected) { throw "Expected rejection: $Name" }
    Write-Output "PASS: $Name"
}
$checker = Join-Path $fixture 'Check-Updates.ps1'
if ((& $checker -CheckOnly).Status -ne 'available') { throw 'New build was not detected.' }
Write-Output 'PASS: detects newer build on the same date'
$global:SevenZipUpdateTestraw = $true
if ((& $checker -CheckOnly).Status -ne 'available') { throw 'Raw JSON could not be read.' }
Write-Output 'PASS: raw JSON download response'
$global:SevenZipUpdateTestraw = $false
$global:SevenZipUpdateTestbuild = '100'
if ((& $checker -CheckOnly).Status -ne 'current') { throw 'Current build was not detected.' }
Write-Output 'PASS: current build'
$global:SevenZipUpdateTestattempt = 1
if ((& $checker -CheckOnly).Status -ne 'available') { throw 'A newer CI attempt was not detected.' }
Write-Output 'PASS: detects a newer attempt of the same CI run'
$global:SevenZipUpdateTestattempt = 0
$global:SevenZipUpdateTestbuild = '99'
if ((& $checker -CheckOnly).Status -ne 'current') { throw 'Downgrade must not be offered.' }
Write-Output 'PASS: older build is not offered'
$global:SevenZipUpdateTestmissing = $true
if ((& $checker -CheckOnly).Status -ne 'unavailable') { throw 'Legacy release fallback failed.' }
Write-Output 'PASS: legacy release without a manifest'
$global:SevenZipUpdateTestmissing = $false
$global:SevenZipUpdateTestbuild = '101'
$global:SevenZipUpdateTestpublisher = '0'*40
Expect-Rejection { & $checker -CheckOnly } 'unexpected publisher'
$global:SevenZipUpdateTestpublisher = $thumbprint
$global:SevenZipUpdateTesturl = 'https://example.com/update-windows.json'
Expect-Rejection { & $checker -CheckOnly } 'unexpected download host'
$global:SevenZipUpdateTesturl = 'https://github.com/UzuCore/7zip/releases/download/' + $global:SevenZipUpdateTesttag + '/update-windows.json'
$global:SevenZipUpdateTesttag = '../../bad'
Expect-Rejection { & $checker -CheckOnly } 'invalid release tag'

. (Join-Path $fixture 'Update-Verification.ps1')
$hash = (Get-FileHash -LiteralPath $SignedInstaller -Algorithm SHA256).Hash
Assert-UpdateInstaller -Path $SignedInstaller -Sha256 $hash -PublisherThumbprint $thumbprint
Write-Output 'PASS: real signed installer with the pinned publisher'
Expect-Rejection { Assert-UpdateInstaller -Path $SignedInstaller -Sha256 ('0'*64) -PublisherThumbprint $thumbprint } 'incorrect SHA-256'
Expect-Rejection { Assert-UpdateInstaller -Path $SignedInstaller -Sha256 $hash -PublisherThumbprint ('0'*40) } 'incorrect signing certificate'
$tampered = Join-Path $fixture 'tampered.exe'
$bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $SignedInstaller))
$bytes[512] = $bytes[512] -bxor 1
[IO.File]::WriteAllBytes($tampered, $bytes)
$tamperedHash = (Get-FileHash -LiteralPath $tampered -Algorithm SHA256).Hash
Expect-Rejection { Assert-UpdateInstaller -Path $tampered -Sha256 $tamperedHash -PublisherThumbprint $thumbprint } 'modified executable even with its matching SHA-256'
Write-Output "Update checks and Authenticode validation passed. Fixtures: $fixture"
