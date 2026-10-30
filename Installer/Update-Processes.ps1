function Test-AutomaticUpdateSetting([object]$Settings) {
    return $null -ne $Settings -and $Settings.Enabled -eq 1
}

function Get-InstalledUpdateProcesses([string]$InstallDirectory) {
    $directory = [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\')
    $session = [Diagnostics.Process]::GetCurrentProcess().SessionId
    Get-Process -Name '7zFM', '7zG', '7z', '7zz' -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            if ($_.SessionId -eq $session -and -not $_.HasExited -and $_.Path -and
                [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($_.Path)).TrimEnd('\') -ieq $directory) {
                $_
            }
        } catch {
            # Do not act on another user's or an inaccessible process.
        }
    }
}

function Close-UpdateFileManagers {
    param([Parameter(Mandatory=$true)][string]$InstallDirectory, [int]$TimeoutMilliseconds = 15000)
    $processes = @(Get-InstalledUpdateProcesses $InstallDirectory)
    if (@($processes | Where-Object ProcessName -ine '7zFM').Count -gt 0) {
        return [pscustomobject]@{ Ready=$false; Reason='busy' }
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    foreach ($process in $processes) {
        if (-not $process.HasExited -and -not $process.CloseMainWindow()) {
            return [pscustomobject]@{ Ready=$false; Reason='not-closed' }
        }
    }
    foreach ($process in $processes) {
        $remaining = [Math]::Max(0, $TimeoutMilliseconds - [int]$timer.ElapsedMilliseconds)
        if (-not $process.HasExited -and -not $process.WaitForExit($remaining)) {
            return [pscustomobject]@{ Ready=$false; Reason='not-closed' }
        }
    }
    $remainingProcesses = @(Get-InstalledUpdateProcesses $InstallDirectory)
    if ($remainingProcesses.Count -gt 0) {
        $reason = if (@($remainingProcesses | Where-Object ProcessName -ine '7zFM').Count -gt 0) { 'busy' } else { 'not-closed' }
        return [pscustomobject]@{ Ready=$false; Reason=$reason }
    }
    return [pscustomobject]@{ Ready=$true; Reason='ready' }
}
