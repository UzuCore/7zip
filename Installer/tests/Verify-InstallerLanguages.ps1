param(
    [Parameter(Mandatory=$true)][string]$MsiPath,
    [Parameter(Mandatory=$true)][string]$TransformPath,
    [ValidateSet(0,1)][int]$ModernSelfSigned = 1
)
$ErrorActionPreference = 'Stop'
$copy = Join-Path ([IO.Path]::GetTempPath()) ('7zip-language-test-' + [guid]::NewGuid().ToString('N') + '.msi')
Copy-Item -LiteralPath $MsiPath -Destination $copy
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($copy, 1)
function Read-Value([string]$Sql) {
    $view = $database.OpenView($Sql)
    try {
        [void]$view.Execute()
        $record = $view.Fetch()
        if ($null -eq $record) { throw "Missing row: $Sql" }
        try { return $record.StringData(1) }
        finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) }
    } finally {
        [void]$view.Close()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
    }
}
try {
    if (Read-Value 'SELECT `Language` FROM `Upgrade` WHERE `ActionProperty`=''WIX_UPGRADE_DETECTED''') { throw 'English upgrade rules must detect every installed language.' }
    if ((Read-Value 'SELECT `Value` FROM `Property` WHERE `Property`=''ProductLanguage''') -ne '1033') { throw 'The default MSI language must be English.' }
    if ($ModernSelfSigned -eq 1 -and (Read-Value 'SELECT `Text` FROM `Control` WHERE `Dialog_`=''SelfSigningConsentDlg'' AND `Control`=''Install''') -ne 'Install') { throw 'English consent text is missing.' }
    if ((Read-Value 'SELECT `Name` FROM `_Storages` WHERE `Name`=''ko-KR''') -ne 'ko-KR') { throw 'The Korean transform is not embedded.' }
    Write-Output 'PASS: English default and embedded Korean transform'
    [void]$database.ApplyTransform((Resolve-Path -LiteralPath $TransformPath).Path, 0)
    if (Read-Value 'SELECT `Language` FROM `Upgrade` WHERE `ActionProperty`=''WIX_UPGRADE_DETECTED''') { throw 'Korean upgrade rules must detect every installed language.' }
    if ((Read-Value 'SELECT `Value` FROM `Property` WHERE `Property`=''ProductLanguage''') -ne '1042') { throw 'The Korean transform did not set the product language.' }
    $install = [string][char]0xC124 + [char]0xCE58
    if ($ModernSelfSigned -eq 1 -and (Read-Value 'SELECT `Text` FROM `Control` WHERE `Dialog_`=''SelfSigningConsentDlg'' AND `Control`=''Install''') -ne $install) { throw 'Korean consent text is missing or corrupted.' }
    $welcome = Read-Value 'SELECT `Text` FROM `Control` WHERE `Dialog_`=''WelcomeDlg'' AND `Control`=''Description'''
    if ($welcome -notmatch '[\uAC00-\uD7A3]') { throw 'The standard MSI dialogs were not translated.' }
    Write-Output 'PASS: Korean standard dialogs and certificate consent text'
} finally {
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
