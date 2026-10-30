param([switch]$Manual, [switch]$CheckOnly, [ValidateSet('en','ko')][string]$Language = 'en')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Update-Verification.ps1')
Add-Type -AssemblyName System.Windows.Forms
function Text([string]$English, [string]$Korean) { if ($Language -eq 'ko') { $Korean } else { $English } }
function Notice([string]$Message) { [void][Windows.Forms.MessageBox]::Show($Message, '7zip+α', 'OK', 'Information') }
$mutex = New-Object Threading.Mutex($false, ('Local\7zipAlphaUpdate-' + [Security.Principal.WindowsIdentity]::GetCurrent().User.Value))
$locked = $false
$download = $null
$downloadDir = $null
$installationRequested = $false
try {
    $locked = $mutex.WaitOne(0)
    if (-not $locked) { return }
    $configPath = Join-Path $PSScriptRoot 'UpdateConfig.json'
    if (-not (Test-Path -LiteralPath $configPath)) { throw (Text 'This build does not include update information.' '이 빌드에는 업데이트 정보가 없습니다.') }
    $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    $settingsPath = 'HKCU:\Software\7-Zip UzuCore\Updates'
    if (-not ($Manual -or $CheckOnly)) {
        New-Item -Path $settingsPath -Force | Out-Null
        $settings = Get-ItemProperty -LiteralPath $settingsPath
        if ($settings.Enabled -eq 0) { return }
        if ($settings.LastCheckUtc) {
            $last = [DateTime]::Parse($settings.LastCheckUtc).ToUniversalTime()
            if (([DateTime]::UtcNow - $last).TotalHours -lt 24) { return }
        }
        Set-ItemProperty -LiteralPath $settingsPath -Name LastCheckUtc -Value ([DateTime]::UtcNow.ToString('o'))
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $headers = @{ 'User-Agent' = '7zip-alpha-updater'; 'Accept' = 'application/vnd.github+json' }
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/UzuCore/7zip/releases/latest' -Headers $headers -TimeoutSec 30
    if ($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^build-\d{4}\.\d{2}\.\d{2}(?:-\d+)?$') { throw 'Invalid release information.' }
    $baseUrl = 'https://github.com/UzuCore/7zip/releases/download/' + $release.tag_name + '/'
    $manifestAsset = @($release.assets | Where-Object name -eq 'update-windows.json')
    if ($manifestAsset.Count -ne 1) {
        if ($CheckOnly) { [pscustomobject]@{ Status = 'unavailable' }; return }
        if ($Manual) { Notice (Text 'This release does not provide automatic updates yet.' '최신 릴리즈에는 아직 자동 업데이트 정보가 없습니다.') }
        return
    }
    if ($manifestAsset[0].browser_download_url -cne ($baseUrl + 'update-windows.json')) { throw 'Invalid manifest URL.' }
    $manifest = Invoke-RestMethod -Uri ($baseUrl + 'update-windows.json') -Headers $headers -TimeoutSec 30
    if ($manifest -is [string]) { $manifest = $manifest | ConvertFrom-Json }
    if ($manifest.schema -ne 1 -or $manifest.releaseTag -cne $release.tag_name) { throw 'Invalid update manifest.' }
    if ([UInt64]$manifest.buildId -le [UInt64]$config.buildId) {
        if ($CheckOnly) { [pscustomobject]@{ Status = 'current' }; return }
        if ($Manual) { Notice (Text 'You are using the latest version.' '최신 버전을 사용하고 있습니다.') }
        return
    }
    if ($config.arch -notin @('x64', 'arm64')) { throw 'Unsupported update architecture.' }
    $asset = $manifest.windows.($config.arch)
    $fileName = '7zip-windows-' + $config.arch + '.exe'
    if ($asset.name -cne $fileName -or $asset.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Invalid installer metadata.' }
    if (-not $config.publisherThumbprint -or $asset.publisherThumbprint -cne $config.publisherThumbprint) { throw (Text 'The update publisher does not match this installation.' '업데이트 게시자 인증서가 현재 설치본과 다릅니다.') }
    if ($CheckOnly) { [pscustomobject]@{ Status = 'available'; BuildId = $manifest.buildId; Name = $fileName }; return }
    $prompt = Text ("A new version ($($release.tag_name)) is available. Download and install it? Windows may request administrator permission.") ("새 버전($($release.tag_name))이 있습니다. 다운로드하고 설치하시겠습니까? Windows에서 관리자 권한을 요청할 수 있습니다.")
    if ([Windows.Forms.MessageBox]::Show($prompt, '7zip+α', 'YesNo', 'Question') -ne 'Yes') { return }
    $installationRequested = $true
    $downloadDir = Join-Path ([IO.Path]::GetTempPath()) ('7zip-alpha-update-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $downloadDir | Out-Null
    $download = Join-Path $downloadDir $fileName
    Invoke-WebRequest -Uri ($baseUrl + $fileName) -OutFile $download -Headers $headers -UseBasicParsing -TimeoutSec 300
    Assert-UpdateInstaller -Path $download -Sha256 $asset.sha256 -PublisherThumbprint $config.publisherThumbprint
    $process = Start-Process -FilePath $download -PassThru -Wait
    if ($process.ExitCode -notin @(0, 3010, 1641, 1602)) { throw (Text "Setup failed (code $($process.ExitCode))." "설치하지 못했습니다(오류 코드 $($process.ExitCode)).") }
} catch {
    if ($CheckOnly) { throw }
    if ($Manual -or $installationRequested) { Notice ((Text 'Could not check for or install an update.' '업데이트를 확인하거나 설치하지 못했습니다.') + "`n" + $_.Exception.Message) }
} finally {
    if ($download -and (Test-Path -LiteralPath $download)) { Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue }
    if ($downloadDir -and (Test-Path -LiteralPath $downloadDir)) { Remove-Item -LiteralPath $downloadDir -ErrorAction SilentlyContinue }
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
