param([Parameter(Mandatory=$true)][string]$Work)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\tools\setup\hidway-setup-core.ps1')
$script:checks=0
function Check([bool]$Condition,[string]$Name){$script:checks++;if(-not $Condition){throw "FAIL: $Name"}}
function Reject([scriptblock]$Action,[string]$Name){$rejected=$false;try{& $Action | Out-Null}catch{$rejected=$true};Check $rejected $Name}
foreach($file in Get-ChildItem (Join-Path $PSScriptRoot '..\tools\setup') -Filter '*.ps1'){
    $errors=$null;$null=[Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$null,[ref]$errors);Check (-not $errors) "parse $($file.Name)"
}
$base=Join-Path ([IO.Path]::GetFullPath($Work)) ([guid]::NewGuid().ToString('N'))
$source=Join-Path $base 'source';$install=Join-Path $base 'installed';New-Item -ItemType Directory -Path $source -Force | Out-Null
$required=@('hidway-client.exe','hidway-input-monitor.exe','hidway-probe.exe','hidway.ini.example','HIDway-Setup.exe','hidway-setup-gui.ps1','hidway-setup-core.ps1','hidway-uninstall.ps1','hidway-pi-setup.ps1','hidway-updates.ps1','hidway-update-core.ps1','hidway-updater.psm1','LICENSE')
foreach($name in $required){[IO.File]::WriteAllText((Join-Path $source $name),"fixture $name")}
[IO.File]::WriteAllText((Join-Path $source 'hidway.ini.example'),"target = 100.100.100.100`nsource_physical = 1`nsource_virtual = 0`nloopback_mode = 0`n")
function Manifest { $lines=@();foreach($file in Get-ChildItem -LiteralPath $source -File | Where-Object Name -ne 'PAYLOAD.sha256'){$lines+=((Get-HidwaySHA256 $file.FullName)+'  '+$file.Name)};[IO.File]::WriteAllLines((Join-Path $source 'PAYLOAD.sha256'),$lines) }
Manifest
Check (@(Get-HidwayPayload $source).Count -eq $required.Count) 'complete manifest'
$null=Install-HidwayPackage $source $install -NoIntegration
Check ((Test-Path (Join-Path $install 'hidway-client.exe')) -and (Test-Path (Join-Path $install 'hidway-input-monitor.exe'))) 'both tools installed'
Check (Initialize-HidwayConfig $install '192.0.2.10' $true) 'Gaming PC initial settings'
$initialized=[IO.File]::ReadAllText((Join-Path $install 'hidway.ini'))
Check ($initialized.Contains('target = 192.0.2.10') -and $initialized.Contains('loopback_mode = 1') -and $initialized.Contains('source_virtual = 1') -and $initialized.Contains('source_physical = 0')) 'Gaming PC virtual-only route and Pi target'
Check (-not (Initialize-HidwayConfig $install '192.0.2.11' $false)) 'edited configuration is not reinitialized'
[IO.File]::WriteAllText((Join-Path $install 'hidway.ini'),'retain user settings')
[IO.File]::WriteAllText((Join-Path $install 'hidway-input-monitor.ini'),'retain labels')
$null=Install-HidwayPackage $source $install -NoIntegration
Check ([IO.File]::ReadAllText((Join-Path $install 'hidway.ini')) -eq 'retain user settings') 'settings retained'
Check ([IO.File]::ReadAllText((Join-Path $install 'hidway-input-monitor.ini')) -eq 'retain labels') 'labels retained'
$locked=[IO.File]::Open((Join-Path $install 'hidway-input-monitor.exe'),'Open','Read','None')
try{Reject {Install-HidwayPackage $source $install -NoIntegration} 'locked monitor update refused'}finally{$locked.Dispose()}
[IO.File]::AppendAllText((Join-Path $source 'hidway-client.exe'),'tampered')
Reject {Get-HidwayPayload $source} 'tamper refused';Manifest
$manifest=Join-Path $source 'PAYLOAD.sha256';$original=[IO.File]::ReadAllText($manifest)
[IO.File]::AppendAllText($manifest,('0'*64)+'  ..\outside.exe'+[Environment]::NewLine);Reject {Get-HidwayPayload $source} 'path traversal refused';[IO.File]::WriteAllText($manifest,$original)
[IO.File]::WriteAllText((Join-Path $source 'hidway.ini'),'not distributable');Manifest;Reject {Get-HidwayPayload $source} 'live config payload refused'
Remove-Item -LiteralPath (Join-Path $source 'hidway.ini');Manifest
$original=[IO.File]::ReadAllText($manifest)
[IO.File]::AppendAllText($manifest,(Get-HidwaySHA256 (Join-Path $source 'hidway-client.exe'))+'  HIDWAY-CLIENT.EXE'+[Environment]::NewLine)
Reject {Get-HidwayPayload $source} 'case-insensitive duplicate path refused';[IO.File]::WriteAllText($manifest,$original)
# Lock the last commit target to force a failure after binaries were replaced.
# The installer must restore prior files before reporting the failed update.
$previousHash=Get-HidwaySHA256 (Join-Path $install 'hidway-client.exe')
$lockedManifest=[IO.File]::Open((Join-Path $install 'PAYLOAD.sha256'),'Open','Read','None')
try{Reject {Install-HidwayPackage $source $install -NoIntegration} 'mid-update failure refused'}finally{$lockedManifest.Dispose()}
Check ((Get-HidwaySHA256 (Join-Path $install 'hidway-client.exe')) -eq $previousHash) 'failed update rolled binaries back'
Check ([IO.File]::ReadAllText((Join-Path $install 'hidway.ini')) -eq 'retain user settings') 'failed update retained settings'
$null=Install-HidwayPackage $source $install -NoIntegration
Check ((Get-HidwaySHA256 (Join-Path $install 'hidway-client.exe')) -eq (Get-HidwaySHA256 (Join-Path $source 'hidway-client.exe'))) 'retry after unlock succeeds'
Reject {Assert-HidwayInstallPath 'C:\'} 'root refused'
Reject {Get-HidwaySshTarget '-oProxyCommand=bad' 'pi'} 'SSH option injection refused'
Reject {Get-HidwaySshTarget 'example.com' 'pi;sudo'} 'SSH user injection refused'
Check ((Get-HidwaySshTarget 'raspberrypi.local' 'pi') -eq 'pi@raspberrypi.local') 'SSH normal target'
Check (Test-HidwayIPv4 '192.0.2.20') 'IPv4 valid';Check (-not (Test-HidwayIPv4 '999.1.1.1')) 'IPv4 invalid'
Check (-not (Test-HidwayIPv4 '127.1')) 'IPv4 shorthand refused';Check (-not (Test-HidwayIPv4 '192.0.2.1;id')) 'IPv4 shell suffix refused'
Check (-not (Test-HidwayIPv4 '010.0.0.1')) 'IPv4 leading zero refused'
Check (Test-HidwayPicoInfo "UF2 Bootloader`nBoard-ID: RP2350" 'pico2') 'Pico 2 family'
Check (-not (Test-HidwayPicoInfo "Board-ID: RP2040" 'pico2')) 'wrong firmware family refused'
Check (-not (Test-HidwayPicoInfo "Board-ID: unknown`nRP2350" 'pico2')) 'Pico family requires board line'
# Exercise the real shortcut/registration helpers under an isolated folder/key.
$menu=Join-Path $base 'start-menu';$desktopLink=Join-Path $base 'desktop-test.lnk'
$testKey='HKCU:\Software\HIDwaySetupTests\'+[guid]::NewGuid().ToString('N')
$testRegistryParentExisted=Test-Path 'HKCU:\Software\HidwaySetupTests'
Register-HidwayInstall $install $menu $testKey $desktopLink
Check ((Test-Path (Join-Path $menu 'HIDway.lnk')) -and (Test-Path (Join-Path $menu 'Input Monitor.lnk')) -and (Test-Path $desktopLink)) 'Start menu and desktop shortcuts'
$shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut((Join-Path $menu 'Input Monitor.lnk'))
Check ($link.TargetPath -eq (Join-Path $install 'hidway-input-monitor.exe') -and $link.WorkingDirectory -eq $install) 'monitor shortcut target/cwd'
$registration=Get-ItemProperty -LiteralPath $testKey
Check ((Get-HidwayInstallationFolder $testKey) -eq $install) 'existing installation discovered for update wizard'
$updateLink=$shell.CreateShortcut((Join-Path $menu 'Check for updates.lnk'))
Check ($updateLink.Arguments -eq '--updates' -and $updateLink.TargetPath -eq (Join-Path $install 'HIDway-Setup.exe')) 'updates shortcut uses shared bootstrap'
Check ($registration.InstallLocation -eq $install -and $registration.UninstallString.EndsWith('HIDway-Setup.exe" --uninstall')) 'uninstall registration'
Set-ItemProperty -LiteralPath $testKey -Name HIDway -Value '"C:\another-copy\hidway-client.exe"'
Remove-HidwayAutorun $install $testKey
Check ((Get-ItemProperty -LiteralPath $testKey).HIDway -eq '"C:\another-copy\hidway-client.exe"') 'other-copy autorun preserved'
Set-ItemProperty -LiteralPath $testKey -Name HIDway -Value ('"'+(Join-Path $install 'hidway-client.exe')+'"')
Remove-HidwayAutorun $install $testKey
Check ($null -eq (Get-ItemProperty -LiteralPath $testKey).PSObject.Properties['HIDway']) 'owned legacy autorun removed'
New-HidwayShortcut (Join-Path $base 'HIDway.lnk') (Join-Path $install 'hidway-client.exe') $install
Unregister-HidwayInstall $install $menu $testKey $desktopLink
Check (-not(Test-Path -LiteralPath $testKey) -and -not(Test-Path -LiteralPath $desktopLink) -and -not(Test-Path -LiteralPath $menu) -and -not(Test-Path -LiteralPath (Join-Path $base 'HIDway.lnk'))) 'owned integration and legacy link removed'
if(-not $testRegistryParentExisted){try{[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey('Software\HidwaySetupTests',$false)}catch{}}
[IO.File]::WriteAllText((Join-Path $install 'user-recording.hidrec'),'retain recording')
$locked=[IO.File]::Open((Join-Path $install 'hidway-input-monitor.exe'),'Open','Read','None')
try{Reject {Uninstall-HidwayPackage $install -NoIntegration} 'uninstall locked file refused'}finally{$locked.Dispose()}
Check (Test-Path (Join-Path $install 'hidway-client.exe')) 'refused uninstall retains other binaries'
Uninstall-HidwayPackage $install -NoIntegration
Check (-not(Test-Path (Join-Path $install 'hidway-client.exe')) -and -not(Test-Path (Join-Path $install 'hidway-input-monitor.exe'))) 'both tools uninstalled'
Check ([IO.File]::ReadAllText((Join-Path $install 'hidway.ini')) -eq 'retain user settings') 'uninstall retains settings'
Check ([IO.File]::ReadAllText((Join-Path $install 'hidway-input-monitor.ini')) -eq 'retain labels') 'uninstall retains labels'
Check ([IO.File]::ReadAllText((Join-Path $install 'user-recording.hidrec')) -eq 'retain recording') 'uninstall retains recordings'
# Input hardening: a trailing newline must not slip past '$' anchors.
Reject {Get-HidwaySshTarget "raspberrypi.local`n" 'pi'} 'SSH host trailing newline refused'
Reject {Get-HidwaySshTarget 'raspberrypi.local' "pi`n"} 'SSH user trailing newline refused'
Check (-not (Test-HidwayIPv4 "192.0.2.20`n")) 'IPv4 trailing newline refused'
foreach($name in @('hidway-setup-gui.ps1','hidway-pi-setup.ps1')){
    $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot "..\tools\setup\$name"))
    Check ($text.Contains('[a-zA-Z0-9_./-]+\z') -and -not $text.Contains('[a-zA-Z0-9_./-]+$''')) "serial check anchors with \z in $name"
}
$piText=[IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\tools\setup\hidway-pi-setup.ps1'))
Check ($piText.Contains('--serial-default') -and $piText.Contains('rm -rf -- ')) 'Pi setup passes --serial-default and removes its remote cache directory'
# OneDrive is refused with a clear notice; real junctions keep their own message; short paths say what is required.
$savedOneDrive=$env:OneDrive;$fakeOneDrive=Join-Path $base 'OneDriveFake';$env:OneDrive=$fakeOneDrive
try{
    $message='';try{$null=Assert-HidwayInstallPath (Join-Path $fakeOneDrive 'Tools\HIDway')}catch{$message=$_.Exception.Message}
    Check ($message -match 'OneDrive' -and $message -match 'outside OneDrive') 'OneDrive install folder refused with a clear message'
    Check ((Get-HidwayOneDriveProblem (Join-Path $fakeOneDrive 'Download')) -match 'OneDrive') 'OneDrive package folder detected'
}finally{$env:OneDrive=$savedOneDrive}
Check ((Get-HidwayOneDriveProblem $install) -eq '') 'normal folder is not reported as OneDrive'
$junction=Join-Path $base 'junction-link'
New-Item -ItemType Junction -Path $junction -Target $install | Out-Null
try{
    $message='';try{$null=Assert-HidwayInstallPath (Join-Path $junction 'sub')}catch{$message=$_.Exception.Message}
    Check ($message -match 'junction or symbolic link' -and $message -notmatch 'OneDrive') 'real junction keeps its own message'
}finally{[IO.Directory]::Delete($junction)}
Check (Test-Path (Join-Path $install 'hidway.ini')) 'junction removal left the target intact'
$message='';try{$null=Assert-HidwayInstallPath 'D:\HID'}catch{$message=$_.Exception.Message}
Check ($message -match 'at least 8 characters' -and $message -notmatch 'drive root') 'short path says what is required'
$message='';try{$null=Assert-HidwayInstallPath 'D:\'}catch{$message=$_.Exception.Message}
Check ($message -match 'drive root') 'drive root still named as such'
# Wizard started from the installed folder: only the registered folder it runs from counts.
Check (Test-HidwayInstalledHere $source $source $source) 'installed folder recognised'
Check (-not (Test-HidwayInstalledHere $source $source '')) 'unregistered folder is not "installed here"'
Check (-not (Test-HidwayInstalledHere $source $install $source)) 'a different folder is not "installed here"'
Check (-not (Test-HidwayInstalledHere (Join-Path $base 'nowhere') (Join-Path $base 'nowhere') (Join-Path $base 'nowhere'))) 'folder without tools is not "installed here"'
Check (-not (Test-HidwayInstalledHere '' $source $source)) 'empty folder text is handled'
# Serial defaults, BOOTSEL detection, Tailscale lookup, file association.
Check ((Get-HidwayDefaultSerial 'usb') -eq '/dev/ttyUSB0' -and (Get-HidwayDefaultSerial 'gpio') -eq '/dev/hidway-serial') 'serial default per UART choice'
$d1=Join-Path $base 'drive1';$d2=Join-Path $base 'drive2';New-Item -ItemType Directory -Path $d1,$d2 | Out-Null
[IO.File]::WriteAllText((Join-Path $d1 'INFO_UF2.TXT'),'Board-ID: RP2040')
Check ((@(Get-HidwayBootselDrives @($d1,$d2)).Count -eq 1) -and (@(Get-HidwayBootselDrives @($d1,$d2))[0] -eq $d1) -and (@(Get-HidwayBootselDrives @($d2)).Count -eq 0)) 'BOOTSEL drive detection by INFO_UF2.TXT'
$fakeTs=Join-Path $base 'fake-tailscale.cmd';[IO.File]::WriteAllText($fakeTs,"@echo off`r`necho 100.64.1.5`r`n")
Check ((Get-HidwayTailscaleIPv4 $fakeTs) -eq '100.64.1.5') 'Tailscale address read from the local CLI'
Check ((Get-HidwayTailscaleIPv4 (Join-Path $base 'missing.exe')) -eq '') 'missing Tailscale is silent'
[IO.File]::WriteAllText($fakeTs,"@echo off`r`necho not-an-address`r`n")
Check ((Get-HidwayTailscaleIPv4 $fakeTs) -eq '') 'garbage Tailscale output is ignored'
Check (-not (Test-HidwayFileAssociation '.hidwaynoassociation')) 'unassociated extension detected'
# Sunshine: existing settings survive, the backup holds the original, nothing is truncated.
$sun=Join-Path $base 'sunshine.conf';$original="sunshine_name = Gaming`r`nkeyboard = enabled`r`nmouse=enabled`r`nport = 47989`r`n"
[IO.File]::WriteAllText($sun,$original)
Set-HidwaySunshineInputOff $sun
$after=[IO.File]::ReadAllText($sun)
Check ($after.Contains('sunshine_name = Gaming') -and $after.Contains('port = 47989') -and $after.Contains('keyboard = disabled') -and $after.Contains('mouse = disabled') -and $after.Contains('controller = disabled') -and $after -notmatch 'enabled') 'Sunshine keeps unrelated lines and disables input'
$backups=@(Get-ChildItem -LiteralPath $base -Filter 'sunshine.conf.bak-*')
Check ($backups.Count -eq 1 -and [IO.File]::ReadAllText($backups[0].FullName) -eq $original) 'Sunshine backup is the original file'
Set-HidwaySunshineInputOff $sun
Check (([regex]::Matches([IO.File]::ReadAllText($sun),'keyboard = disabled')).Count -eq 1) 'Sunshine edit is idempotent'
$sunNew=Join-Path $base 'sun-new\config\sunshine.conf';Set-HidwaySunshineInputOff $sunNew
Check ((Test-Path $sunNew) -and -not (Test-Path ($sunNew+'.bak-*')) -and [IO.File]::ReadAllText($sunNew).Contains('mouse = disabled')) 'Sunshine without a config creates one'
Write-Output "Setup: $script:checks checks PASS (isolated files/shortcuts/test registry key; real installed apps, SSH and firmware unchanged)"
