$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Update-Processes.ps1')
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    Write-Output "PASS: $Name"
}
Check (-not (Test-AutomaticUpdateSetting $null)) 'automatic checks default off without settings'
Check (-not (Test-AutomaticUpdateSetting @{})) 'automatic checks default off without Enabled'
Check (-not (Test-AutomaticUpdateSetting @{Enabled=0})) 'explicitly disabled'
Check (-not (Test-AutomaticUpdateSetting @{Enabled=2})) 'invalid setting stays disabled'
Check (Test-AutomaticUpdateSetting @{Enabled=1}) 'explicit opt-in enables checks'

function New-FakeProcess([string]$Name, [string]$Path, [bool]$CloseAllowed=$true, [bool]$AllowExit=$true) {
    $p = [pscustomobject]@{ ProcessName=$Name; Path=$Path; SessionId=[Diagnostics.Process]::GetCurrentProcess().SessionId; HasExited=$false; CloseCalls=0; WaitCalls=0; CloseAllowed=$CloseAllowed; AllowExit=$AllowExit }
    $p | Add-Member ScriptMethod CloseMainWindow { $this.CloseCalls++; return $this.CloseAllowed }
    $p | Add-Member ScriptMethod WaitForExit { param($Timeout) $this.WaitCalls++; if ($this.AllowExit) { $this.HasExited=$true }; return $this.AllowExit }
    return $p
}
function Get-Process {
    param([string[]]$Name, $ErrorAction)
    return @($global:SevenZipUpdateProcessFixtures | Where-Object { $_.ProcessName -in $Name })
}
$directory = 'C:\7zip-update-process-test'
$manager = New-FakeProcess '7zFM' "$directory\7zFM.exe"
$worker = New-FakeProcess '7zG' "$directory\7zG.exe"
$global:SevenZipUpdateProcessFixtures = @($manager, $worker)
$result = Close-UpdateFileManagers $directory
Check (-not $result.Ready -and $result.Reason -eq 'busy' -and $manager.CloseCalls -eq 0) 'active archive job blocks setup before closing any windows'

$otherManager = New-FakeProcess '7zFM' 'C:\other-installation\7zFM.exe'
$otherWorker = New-FakeProcess '7zz' 'C:\other-installation\7zz.exe'
$explorer = New-FakeProcess 'explorer' 'C:\Windows\explorer.exe'
$global:SevenZipUpdateProcessFixtures = @($manager, $otherManager, $otherWorker, $explorer)
$result = Close-UpdateFileManagers $directory
Check ($result.Ready -and $manager.CloseCalls -eq 1 -and $manager.HasExited) 'matching file manager closes normally before setup'
Check ($otherManager.CloseCalls -eq 0 -and $otherWorker.CloseCalls -eq 0 -and $explorer.CloseCalls -eq 0) 'other installations and Windows Explorer remain running'

$manager = New-FakeProcess '7zFM' "$directory\7zFM.exe" $false
$global:SevenZipUpdateProcessFixtures = @($manager)
Check (-not (Close-UpdateFileManagers $directory).Ready) 'refused close blocks setup'
$manager = New-FakeProcess '7zFM' "$directory\7zFM.exe" $true $false
$global:SevenZipUpdateProcessFixtures = @($manager)
Check (-not (Close-UpdateFileManagers $directory -TimeoutMilliseconds 1).Ready) 'close timeout blocks setup without force termination'

$manager = New-FakeProcess '7zFM' "$directory\7zFM.exe"
$manager.SessionId++
$global:SevenZipUpdateProcessFixtures = @($manager)
Check ((Close-UpdateFileManagers $directory).Ready -and $manager.CloseCalls -eq 0) 'another user session is not closed'
