param(
    [Parameter(Mandatory = $true)]
    [string]$MsiPath,
    [ValidateSet(0, 1)]
    [int]$ModernShellEnabled = 0,
    [ValidateSet(0, 1)]
    [int]$ModernSelfSigned = 0
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
    function Read-InstallerRow([string]$Sql, [int]$Columns) {
        $query = $null
        $item = $null
        try {
            $query = $database.OpenView($Sql)
            [void]$query.Execute()
            $item = $query.Fetch()
            if ($null -eq $item) { return $null }
            $values = @(for ($n = 1; $n -le $Columns; $n++) { $item.StringData($n) })
            return ,$values
        }
        finally {
            if ($null -ne $query) { [void]$query.Close() }
            foreach ($obj in @($item, $query)) {
                if ($null -ne $obj) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($obj) }
            }
        }
    }
    foreach ($property in @('WIX_UPGRADE_DETECTED', 'WIX_DOWNGRADE_DETECTED')) {
        $upgrade = Read-InstallerRow ("SELECT ``Language`` FROM ``Upgrade`` WHERE ``ActionProperty`` = '$property'") 1
        if ($null -eq $upgrade -or $upgrade[0]) { throw 'Upgrade detection must include all product languages.' }
    }
    $directory = Read-InstallerRow 'SELECT `DefaultDir` FROM `Directory` WHERE `Directory` = ''INSTALLFOLDER''' 1
    if ($null -eq $directory -or ($directory[0] -split '\|')[-1] -cne '7z alpha') { throw 'The default installation directory must be 7z alpha.' }
    $initialize = Read-InstallerRow 'SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action` = ''InstallInitialize''' 1
    $removeProducts = Read-InstallerRow 'SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action` = ''RemoveExistingProducts''' 1
    $install = Read-InstallerRow 'SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action` = ''InstallFiles''' 1
    if ($null -eq $removeProducts -or [int]$removeProducts[0] -le [int]$initialize[0] -or [int]$removeProducts[0] -ge [int]$install[0]) {
        throw 'Previous products must be removed inside the rollback transaction before installing files.'
    }
    $remove = Read-InstallerRow 'SELECT `Sequence` FROM `InstallExecuteSequence` WHERE `Action` = ''RemoveFiles''' 1
    foreach ($action in @('RegisterModernShell', 'UnregisterModernShell', 'RollbackModernShell')) {
        $row = Read-InstallerRow ("SELECT ``Type``,``Source``,``Target`` FROM ``CustomAction`` WHERE ``Action`` = '$action'") 3
        if ($ModernShellEnabled -eq 0) {
            if ($null -ne $row) { throw 'Unsigned packages must not automatically register the modern shell identity.' }
            continue
        }
        $expectedType = if ($action -eq 'RollbackModernShell') { 3426 } else { 3106 }
        if ($null -eq $row -or [int]$row[0] -ne $expectedType -or $row[1] -ne 'INSTALLFOLDER' -or $row[2] -ne "[$action]") {
            throw "Incorrect elevated deferred action: $action"
        }
        $launcher = Read-InstallerRow ("SELECT ``Target`` FROM ``CustomAction`` WHERE ``Action`` = 'Set$action'") 1
        if ($null -eq $launcher -or $launcher[0] -notmatch '-ExecutionPolicy RemoteSigned' -or $launcher[0] -notmatch '-LogPath') {
            throw 'SYSTEM shell actions must use process-scoped RemoteSigned and preserve diagnostic logs.'
        }
        $seq = Read-InstallerRow ("SELECT ``Condition``,``Sequence`` FROM ``InstallExecuteSequence`` WHERE ``Action`` = '$action'") 2
        if ($null -eq $seq) { throw "Missing installer sequence: $action" }
        if ($ModernSelfSigned -eq 1 -and $action -ne 'UnregisterModernShell' -and $seq[0] -notmatch 'SELF_SIGNED_MENU_CONSENT="1"') {
            throw 'Self-signed registration must require explicit consent.'
        }
        if ($action -eq 'UnregisterModernShell') {
            if ([int]$seq[1] -ge [int]$remove[0] -or $seq[0] -notmatch 'NOT UPGRADINGPRODUCTCODE') {
                throw 'Modern shell removal must precede RemoveFiles and preserve major upgrades.'
            }
        } elseif ([int]$seq[1] -le [int]$install[0]) {
            throw 'Modern shell registration and rollback scheduling must follow InstallFiles.'
        }
    }
    if ($ModernSelfSigned -eq 1) {
        $defaultConsent = Read-InstallerRow 'SELECT `Value` FROM `Property` WHERE `Property` = ''SELF_SIGNED_MENU_CONSENT''' 1
        if ($null -ne $defaultConsent -and $defaultConsent[0]) { throw 'Certificate consent must be unchecked by default.' }
        $checkbox = Read-InstallerRow 'SELECT `Property`,`Value` FROM `CheckBox` WHERE `Property` = ''SELF_SIGNED_MENU_CONSENT''' 2
        if ($null -eq $checkbox -or $checkbox[1] -ne '1') { throw 'Missing certificate consent checkbox.' }
        $consentUi = Read-InstallerRow 'SELECT `Sequence` FROM `InstallUISequence` WHERE `Action` = ''SelfSigningConsentDlg''' 1
        $executeUi = Read-InstallerRow 'SELECT `Sequence` FROM `InstallUISequence` WHERE `Action` = ''ExecuteAction''' 1
        if ($null -eq $consentUi -or [int]$consentUi[0] -ge [int]$executeUi[0]) { throw 'Consent must precede elevated installation.' }
        $target = Read-InstallerRow 'SELECT `Target` FROM `CustomAction` WHERE `Action` = ''SetRegisterModernShell''' 1
        if ($target[0] -notmatch '-SelfSignedConsent') { throw 'Missing explicit helper consent argument.' }
    }
    Write-Output "Verified modern shell installer gate: $ModernShellEnabled"
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
