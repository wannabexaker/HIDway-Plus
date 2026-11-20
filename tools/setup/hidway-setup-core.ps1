Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

function Get-HidwaySHA256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()}
}
function Get-HidwayPayload([string]$Source,[switch]$ManifestOnly) {
    $manifest = Join-Path $Source 'PAYLOAD.sha256'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Incomplete package: PAYLOAD.sha256 is missing. Extract the complete release ZIP.' }
    if ((Get-Item -LiteralPath $manifest).Length -gt 65536) { throw 'Package manifest is too large.' }
    $top = @('hidway-client.exe','hidway-input-monitor.exe','hidway-probe.exe','HIDway-Setup.exe','Start-Input-Monitor.cmd','INPUT_MONITOR.md','SECURITY.md','Setup.cmd','hidway-setup.ps1','hidway-setup-gui.ps1','hidway-setup-core.ps1','hidway-uninstall.ps1','hidway-pi-setup.ps1','hidway-updates.ps1','hidway-update-core.ps1','relay-install.sh','hidway.ini.example','LICENSE','README.md','VERSION')
    $top += 'hidway-updater.psm1'
    $files = @()
    foreach ($line in [IO.File]::ReadAllLines($manifest)) {
        if ($line -notmatch '^([0-9a-fA-F]{64})  ([A-Za-z0-9_. /-]+)$') { throw 'Invalid package manifest.' }
        $hash = $Matches[1]; $relative = $Matches[2].Replace('/', '\')
        if ($relative -match '(^|\\)\.\.?($|\\)' -or [IO.Path]::IsPathRooted($relative)) { throw 'Unsafe package path.' }
        if ($relative -notin $top -and $relative -notmatch '^docs\\[A-Za-z0-9_-]+\.md$' -and $relative -ne 'docs\images\client.png' -and $relative -notmatch '^firmware\\hidway_fw_(pico|pico_w|pico2|pico2_w)\.uf2$') { throw "Unexpected payload file: $relative" }
        $path = Join-Path $Source $relative
        $null=Assert-HidwayInstallPath $path
        if (-not $ManifestOnly) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Missing or unsafe package file: $relative" }
            if ((Get-HidwaySHA256 $path) -ne $hash) { throw "Package integrity check failed: $relative. Download/extract it again." }
        }
        $files += $relative
    }
    foreach ($required in @('hidway-client.exe','hidway-input-monitor.exe','hidway-probe.exe','hidway.ini.example','HIDway-Setup.exe','hidway-setup-gui.ps1','hidway-setup-core.ps1','hidway-uninstall.ps1','hidway-pi-setup.ps1','hidway-updates.ps1','hidway-update-core.ps1','LICENSE')) {
        if ($files -notcontains $required) { throw "Incomplete package: $required is not in the manifest." }
    }
    if ($files -notcontains 'hidway-updater.psm1') { throw 'Incomplete package: hidway-updater.psm1 is not in the manifest.' }
    if (@($files | ForEach-Object {$_.ToLowerInvariant()} | Select-Object -Unique).Count -ne $files.Count) { throw 'Duplicate package paths.' }
    return $files
}
function Get-HidwayOneDriveProblem([string]$Path) {
    # OneDrive is refused on purpose: its sync folders are reparse points that are not junctions or symbolic links.
    $message = 'HIDway does not work from a OneDrive or other cloud-synced folder (sync and Files On-Demand are not supported). Use a normal local folder outside OneDrive, for example C:\HIDway.'
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    foreach ($root in @($env:OneDrive,$env:OneDriveConsumer,$env:OneDriveCommercial)) {
        if (-not $root) { continue }
        $root = [IO.Path]::GetFullPath($root).TrimEnd('\')
        if ($root.Length -gt 3 -and ($full -eq $root -or $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase))) { return $message }
    }
    for ($part = $full; $part -and $part.Length -gt 2; $part = [IO.Path]::GetDirectoryName($part)) {
        if (-not (Test-Path -LiteralPath $part)) { continue }
        $item = Get-Item -LiteralPath $part -Force
        if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        $link = $item.PSObject.Properties['LinkType']
        if (-not $link -or [string]$link.Value -notin @('Junction','SymbolicLink')) { return $message }
    }
    return ''
}
function Assert-HidwayInstallPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $oneDrive = Get-HidwayOneDriveProblem $full
    if ($oneDrive) { throw $oneDrive }
    if ($full -eq [IO.Path]::GetPathRoot($full).TrimEnd('\')) { throw 'Choose a dedicated HIDway folder, not a drive root.' }
    if ($full.Length -lt 8) { throw "Choose a folder whose full path has at least 8 characters, such as C:\HIDway or D:\Apps\HIDway ('$full' is too short)." }
    foreach ($protected in @($env:WINDIR,$env:USERPROFILE,$env:LOCALAPPDATA,$env:ProgramFiles,${env:ProgramFiles(x86)})) {
        if ($protected -and $full -eq [IO.Path]::GetFullPath($protected).TrimEnd('\')) { throw 'Choose a dedicated HIDway subfolder.' }
    }
    $part = $full
    while ($part) {
        if ((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Install through a normal folder, not a junction or symbolic link.' }
        $part = [IO.Path]::GetDirectoryName($part)
    }
    return $full
}
function Test-HidwayFileAssociation([string]$Extension) {
    # True when Windows has a default program for the extension (otherwise it would show the "how to open" prompt).
    try {
        $key = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey($Extension)
        if ($key) { try { if ([string]$key.GetValue('')) { return $true } } finally { $key.Dispose() } }
        $choice = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\'+$Extension+'\UserChoice')
        if ($choice) { try { return [bool][string]$choice.GetValue('ProgId') } finally { $choice.Dispose() } }
    } catch {}
    return $false
}
function Test-HidwayInstalledHere([string]$Folder,[string]$PackageRoot,[string]$Registered) {
    # True when the chosen folder is the registered installation, the folder the wizard itself runs from, and the tools are there.
    try {
        $chosen = [IO.Path]::GetFullPath($Folder).TrimEnd('\'); $here = [IO.Path]::GetFullPath($PackageRoot).TrimEnd('\')
        return $Registered -and $chosen -ieq $here -and $Registered.TrimEnd('\') -ieq $here -and (Test-Path -LiteralPath (Join-Path $here 'hidway-client.exe') -PathType Leaf)
    } catch { return $false }
}
function Get-HidwayDefaultSerial([string]$Uart) {
    # /dev/hidway-serial is the stable udev name of the Pi 4 GPIO UART3 (relay/deploy/99-hidway.rules).
    if ($Uart -eq 'gpio') { return '/dev/hidway-serial' }
    return '/dev/ttyUSB0'
}
function Get-HidwayBootselDrives([string[]]$Roots) {
    if (-not $Roots) { $Roots = @([IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.DriveType -in @([IO.DriveType]::Removable,[IO.DriveType]::Fixed) } | ForEach-Object { $_.Name }) }
    return @($Roots | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'INFO_UF2.TXT') -PathType Leaf })
}
function Get-HidwayTailscaleIPv4([string]$Exe,[int]$TimeoutMs=3000) {
    # Local CLI only; any failure is silent and gives ''.
    try {
        if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { return '' }
        $info = New-Object Diagnostics.ProcessStartInfo $Exe,'ip -4'
        $info.UseShellExecute = $false; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true; $info.CreateNoWindow = $true
        $process = [Diagnostics.Process]::Start($info)
        try {
            $read = $process.StandardOutput.ReadToEndAsync()
            if (-not $process.WaitForExit($TimeoutMs)) { try { $process.Kill() } catch {}; return '' }
            $first = ([string]$read.Result -split '\r?\n' | Select-Object -First 1)
            if ($first -and (Test-HidwayIPv4 $first.Trim())) { return $first.Trim() }
        } finally { $process.Dispose() }
    } catch {}
    return ''
}
function Set-HidwaySunshineInputOff([string]$Conf) {
    # Sunshine's own keyboard/mouse/controller off. Other lines are kept; an existing file is backed up first.
    if (Test-Path -LiteralPath $Conf -PathType Leaf) {
        Copy-Item -LiteralPath $Conf -Destination ($Conf+'.bak-'+(Get-Date -Format yyyyMMdd-HHmmss)) -ErrorAction Stop
        $lines = @([IO.File]::ReadAllLines($Conf) | Where-Object { $_ -notmatch '^\s*(keyboard|mouse|controller)\s*=' })
    } else {
        $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Conf)) -Force; $lines = @()
    }
    $lines += 'keyboard = disabled','mouse = disabled','controller = disabled'
    [IO.File]::WriteAllLines($Conf,[string[]]$lines,(New-Object Text.UTF8Encoding($false)))
}
function Get-HidwayInstallationFolder([string]$RegistryKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HIDway') {
    $registration=Get-ItemProperty -LiteralPath $RegistryKey -ErrorAction SilentlyContinue
    if(-not $registration -or -not $registration.PSObject.Properties['InstallLocation']){return ''}
    try{
        $folder=Assert-HidwayInstallPath ([string]$registration.InstallLocation)
        if(Test-Path -LiteralPath (Join-Path $folder 'hidway-client.exe') -PathType Leaf){return $folder}
    }catch{}
    return ''
}
function New-HidwayShortcut([string]$Path,[string]$Target,[string]$Folder) {
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut($Path); $link.TargetPath = $Target; $link.WorkingDirectory = $Folder; $link.Save()
}
function Register-HidwayInstall([string]$Destination,[string]$Menu,[string]$RegistryKey,[string]$DesktopLink='') {
    New-Item -ItemType Directory -Path $Menu -Force | Out-Null
    foreach($item in @(@('HIDway','hidway-client.exe'),@('Input Monitor','hidway-input-monitor.exe'),@('Setup guide','HIDway-Setup.exe'),@('Check for updates','HIDway-Setup.exe'))){
        $linkPath=Join-Path $Menu ($item[0]+'.lnk');New-HidwayShortcut $linkPath (Join-Path $Destination $item[1]) $Destination
        if($item[0] -eq 'Check for updates'){$shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($linkPath);$link.Arguments='--updates';$link.Save()}
    }
    if($DesktopLink){New-HidwayShortcut $DesktopLink (Join-Path $Destination 'hidway-client.exe') $Destination}
    New-Item -Path $RegistryKey -Force | Out-Null
    $versionFile=Join-Path $Destination 'VERSION';$version=if(Test-Path -LiteralPath $versionFile){[IO.File]::ReadAllText($versionFile).Trim()}else{'preview'}
    $bootstrap=Join-Path $Destination 'HIDway-Setup.exe'
    $values=@{DisplayName='HIDway + Input Monitor';DisplayVersion=$version;Publisher='HIDway contributors';InstallLocation=$Destination;DisplayIcon=(Join-Path $Destination 'hidway-client.exe');UninstallString="`"$bootstrap`" --uninstall"}
    foreach($name in $values.Keys){New-ItemProperty -Path $RegistryKey -Name $name -Value $values[$name] -PropertyType String -Force | Out-Null}
    foreach($name in @('NoModify','NoRepair')){New-ItemProperty -Path $RegistryKey -Name $name -Value 1 -PropertyType DWord -Force | Out-Null}
}
function Unregister-HidwayInstall([string]$Destination,[string]$Menu,[string]$RegistryKey,[string]$DesktopLink='') {
    $registration=Get-ItemProperty -LiteralPath $RegistryKey -ErrorAction SilentlyContinue
    if($registration -and $registration.InstallLocation -eq $Destination){Remove-Item -LiteralPath $RegistryKey -Recurse}
    $shell=New-Object -ComObject WScript.Shell;$links=@()
    if($DesktopLink){$links+=$DesktopLink}
    $links+=Join-Path ([IO.Path]::GetDirectoryName($Menu)) 'HIDway.lnk' # legacy CLI shortcut
    foreach($name in @('HIDway','Input Monitor','Setup guide','Check for updates')){$links+=Join-Path $Menu ($name+'.lnk')}
    foreach($link in $links){if((Test-Path -LiteralPath $link) -and [IO.Path]::GetDirectoryName($shell.CreateShortcut($link).TargetPath) -eq $Destination){Remove-Item -LiteralPath $link}}
    try{[IO.Directory]::Delete($Menu,$false)}catch{} # only if empty; retain custom shortcuts
}
function Remove-HidwayAutorun([string]$Destination,[string]$RunKey) {
    $entry=Get-ItemProperty -LiteralPath $RunKey -ErrorAction SilentlyContinue
    if(-not $entry){return};$property=$entry.PSObject.Properties['HIDway'];if(-not $property){return}
    $value=[string]$property.Value;$exe=Join-Path $Destination 'hidway-client.exe'
    if($value -eq $exe -or $value.StartsWith(('"'+$exe+'"'),[StringComparison]::OrdinalIgnoreCase)){Remove-ItemProperty -LiteralPath $RunKey -Name HIDway}
}
function Install-HidwayPackage([string]$Source,[string]$Destination,[switch]$Desktop,[switch]$NoIntegration) {
    $files = @(Get-HidwayPayload $Source); $Destination = Assert-HidwayInstallPath $Destination
    if ([IO.Path]::GetFullPath($Source).TrimEnd('\') -eq $Destination) { throw 'Choose an installation folder separate from the extracted download.' }
    # Do not terminate an input session or another portable copy by process name.
    foreach ($process in @(Get-Process -Name hidway-client,hidway-input-monitor,HIDway-Setup -ErrorAction SilentlyContinue)) {
        if ($process.Path -and [IO.Path]::GetDirectoryName($process.Path) -eq $Destination) { throw 'Close HIDway and Input Monitor in the installation folder before updating. Your running session was left untouched.' }
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    # Lock/permission checks precede replacing any installed file.
    $locks = @()
    try {
        foreach ($relative in $files) {
            $target = Join-Path $Destination $relative
            $null=Assert-HidwayInstallPath $target
            if(Test-Path -LiteralPath $target -PathType Container){throw "A folder occupies the application file path: $relative"}
            if (Test-Path -LiteralPath $target -PathType Leaf) { $locks += [IO.File]::Open($target,'Open','ReadWrite','None') }
        }
    } finally { foreach ($stream in $locks) { $stream.Dispose() } }
    # Stage before replacement; retain previous files for rollback on failure.
    # Live configuration and recordings are never members of this transaction.
    $transaction=Join-Path $Destination ('.setup-transaction-'+[guid]::NewGuid().ToString('N'))
    $staged=Join-Path $transaction 'new';$backup=Join-Path $transaction 'old';$committed=@();$all=@($files)+@('PAYLOAD.sha256')
    try {
        foreach($relative in $all){$new=Join-Path $staged $relative;New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($new)) -Force | Out-Null;Copy-Item -LiteralPath (Join-Path $Source $relative) -Destination $new}
        foreach($relative in $all){
            $target=Join-Path $Destination $relative;$new=Join-Path $staged $relative;$old=Join-Path $backup $relative
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)),([IO.Path]::GetDirectoryName($old)) -Force | Out-Null
            if(Test-Path -LiteralPath $target){[IO.File]::Replace($new,$target,$old)}else{[IO.File]::Move($new,$target)}
            $committed+=$relative
        }
    } catch {
        $problem=$_.Exception.Message
        [array]::Reverse($committed)
        foreach($relative in $committed){$target=Join-Path $Destination $relative;$old=Join-Path $backup $relative;try{if(Test-Path -LiteralPath $old){Copy-Item -LiteralPath $old -Destination $target -Force}else{Remove-Item -LiteralPath $target -Force}}catch{}}
        throw "Install failed: $problem. Recovery files retained at $transaction. Close both apps and retry."
    }
    $checkedTransaction=[IO.Path]::GetFullPath($transaction)
    if(-not $checkedTransaction.StartsWith($Destination+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid transaction cleanup path.'}
    Remove-Item -LiteralPath $checkedTransaction -Recurse -Force
    $ini = Join-Path $Destination 'hidway.ini'
    if (-not (Test-Path -LiteralPath $ini)) { Copy-Item -LiteralPath (Join-Path $Destination 'hidway.ini.example') -Destination $ini }
    if (-not $NoIntegration) {
        $menu = Join-Path ([Environment]::GetFolderPath('Programs')) 'HIDway'
        $desktopLink=if($Desktop){Join-Path ([Environment]::GetFolderPath('Desktop')) 'HIDway.lnk'}else{''}
        Register-HidwayInstall $Destination $menu 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HIDway' $desktopLink
    }
    return $Destination
}
function Uninstall-HidwayPackage([string]$Destination,[switch]$NoIntegration) {
    $Destination=Assert-HidwayInstallPath $Destination;$files=@(Get-HidwayPayload $Destination -ManifestOnly)
    foreach($p in @(Get-Process hidway-client,hidway-input-monitor -ErrorAction SilentlyContinue)){if($p.Path -and [IO.Path]::GetDirectoryName($p.Path) -eq $Destination){throw 'Close HIDway and Input Monitor, then uninstall again.'}}
    $locks=@()
    try{foreach($relative in $files){$path=Join-Path $Destination $relative;$null=Assert-HidwayInstallPath $path;if(Test-Path -LiteralPath $path -PathType Leaf){$locks+=[IO.File]::Open($path,'Open','Read','None')}}}finally{foreach($stream in $locks){$stream.Dispose()}}
    foreach($relative in $files){$path=Join-Path $Destination $relative;if(Test-Path -LiteralPath $path -PathType Leaf){Remove-Item -LiteralPath $path -Force}}
    Remove-Item -LiteralPath (Join-Path $Destination 'PAYLOAD.sha256')
    if(-not $NoIntegration){Unregister-HidwayInstall $Destination (Join-Path ([Environment]::GetFolderPath('Programs')) 'HIDway') 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HIDway' (Join-Path ([Environment]::GetFolderPath('Desktop')) 'HIDway.lnk');Remove-HidwayAutorun $Destination 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'}
}
function Initialize-HidwayConfig([string]$Destination,[string]$RelayIP,[bool]$GamingPC) {
    $ini=Join-Path $Destination 'hidway.ini';$example=Join-Path $Destination 'hidway.ini.example'
    $text=[IO.File]::ReadAllText($ini)
    if($text -ne [IO.File]::ReadAllText($example)){return $false}
    if(Test-HidwayIPv4 $RelayIP){$text=$text -replace '(?m)^target\s*=.*$',('target = '+$RelayIP)}
    if($GamingPC){$text=$text -replace '(?m)^source_physical\s*=.*$','source_physical = 0';$text=$text -replace '(?m)^source_virtual\s*=.*$','source_virtual = 1';$text=$text -replace '(?m)^loopback_mode\s*=.*$','loopback_mode = 1'}
    [IO.File]::WriteAllText($ini,$text,(New-Object Text.UTF8Encoding($false)));return $true
}
function Test-HidwayIPv4([string]$Value) {
    $ip = $null
    return $Value -match '^\d{1,3}(\.\d{1,3}){3}\z' -and [Net.IPAddress]::TryParse($Value,[ref]$ip) -and $ip.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and $ip.ToString() -eq $Value -and $Value -ne '0.0.0.0'
}
function Get-HidwaySshTarget([string]$PiHost,[string]$PiUser) {
    if ($PiHost -notmatch '^[a-zA-Z0-9][a-zA-Z0-9.-]{0,252}\z' -or $PiHost.Contains('..') -or $PiUser -notmatch '^[a-z_][a-z0-9_-]{0,31}\z') { throw 'Enter a hostname/IPv4 and a Linux username. Spaces and shell commands are not allowed.' }
    return "$PiUser@$PiHost"
}
function Get-HidwayPicoFile([string]$Source,[string]$Board,[string]$Drive) {
    if ($Board -notin @('pico','pico_w','pico2','pico2_w')) { throw 'Choose the exact Pico board.' }
    $root = [IO.Path]::GetFullPath($Drive)
    if ($root -notmatch '^[A-Za-z]:\\$') { throw 'Choose the BOOTSEL drive root.' }
    $info = Join-Path $root 'INFO_UF2.TXT'
    if (-not (Test-Path -LiteralPath $info -PathType Leaf)) { throw 'No BOOTSEL INFO_UF2.TXT on this drive. Hold BOOTSEL while reconnecting the Pico USB.' }
    $text = [IO.File]::ReadAllText($info)
    if (-not (Test-HidwayPicoInfo $text $Board)) { throw 'The BOOTSEL drive does not match this Pico family. Check the board model.' }
    $file = Join-Path $Source "firmware\hidway_fw_$Board.uf2"
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Firmware is missing from the package.' }
    return $file
}
function Test-HidwayPicoInfo([string]$Text,[string]$Board) {
    if ($Board -notin @('pico','pico_w','pico2','pico2_w')) { return $false }
    $family = if ($Board.StartsWith('pico2')) { 'RP2350' } else { 'RP2040' }
    return $Text -match "(?im)^Board-ID:[^\r\n]*$family"
}
