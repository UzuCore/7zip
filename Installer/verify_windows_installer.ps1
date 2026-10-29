param(
    [Parameter(Mandatory = $true)]
    [string]$MsiPath
)

$ErrorActionPreference = 'Stop'
$resolvedPath = (Resolve-Path -LiteralPath $MsiPath).ProviderPath
$installer = $null
$database = $null
$view = $null
$record = $null
try {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase($resolvedPath, 0)
    $view = $database.OpenView("SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = 'MSIRESTARTMANAGERCONTROL'")
    $view.Execute()
    $record = $view.Fetch()
    if ($null -eq $record -or $record.StringData(1) -cne 'DisableShutdown') {
        throw 'Installer must set MSIRESTARTMANAGERCONTROL=DisableShutdown to avoid automatically closing Explorer and other file managers.'
    }
    Write-Output "Verified installer policy: DisableShutdown ($resolvedPath)"
}
finally {
    if ($null -ne $view) { $view.Close() }
    foreach ($comObject in @($record, $view, $database, $installer)) {
        if ($null -ne $comObject) {
            [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($comObject)
        }
    }
}
