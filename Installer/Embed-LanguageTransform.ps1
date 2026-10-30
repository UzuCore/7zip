param(
    [Parameter(Mandatory=$true)][string]$MsiPath,
    [Parameter(Mandatory=$true)][string]$TransformPath
)
$ErrorActionPreference = 'Stop'
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $null
$view = $null
$record = $null
try {
    $database = $installer.OpenDatabase((Resolve-Path -LiteralPath $MsiPath).Path, 1)
    $record = $installer.CreateRecord(2)
    $record.StringData(1) = 'ko-KR'
    $record.SetStream(2, (Resolve-Path -LiteralPath $TransformPath).Path)
    $view = $database.OpenView('INSERT INTO `_Storages` (`Name`, `Data`) VALUES (?, ?)')
    $view.Execute($record)
    $database.Commit()
} finally {
    foreach ($obj in @($record, $view, $database, $installer)) {
        if ($null -ne $obj) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($obj) }
    }
}
