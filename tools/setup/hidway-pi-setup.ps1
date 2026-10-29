param([string]$PiHost,[string]$PiUser,[string]$RelayIP,[string]$SenderIP,[ValidateSet('gpio','usb')][string]$Uart='usb',[string]$Serial='/dev/ttyUSB0',[string]$ResultFile)
. (Join-Path $PSScriptRoot 'hidway-setup-core.ps1')
$rc=1
try {
    $target=Get-HidwaySshTarget $PiHost $PiUser
    if (-not (Test-HidwayIPv4 $SenderIP) -or -not (Test-HidwayIPv4 $RelayIP)) { throw 'Enter both Pi relay and sending Windows PC IPv4.' }
    if ($Serial -notmatch '^/dev/[a-zA-Z0-9_./-]+$' -or $Serial.Contains('..')) { throw 'Invalid serial device.' }
    $ssh=Join-Path $env:WINDIR 'System32\OpenSSH\ssh.exe';$scp=Join-Path $env:WINDIR 'System32\OpenSSH\scp.exe'
    if (-not (Test-Path -LiteralPath $ssh) -or -not (Test-Path -LiteralPath $scp)) { throw 'Enable Windows Settings > Optional features > OpenSSH Client, then retry.' }
    $installer=Join-Path $PSScriptRoot 'relay-install.sh'
    if (-not (Test-Path -LiteralPath $installer)) { throw 'The relay installer is missing from this package.' }
    $remote='.cache/hidway-setup-'+[guid]::NewGuid().ToString('N')
    Write-Host 'Connect to your Pi. Verify its SSH fingerprint against the Pi before accepting it.' -ForegroundColor Cyan
    Write-Host 'Passwords and sudo prompts belong to SSH/Linux; HIDway does not store them.'
    & $ssh -t -o StrictHostKeyChecking=ask -- $target "umask 077; mkdir -p $remote"
    if ($LASTEXITCODE -ne 0) { throw 'SSH connection/host verification failed.' }
    & $scp -o StrictHostKeyChecking=yes -- $installer "${target}:$remote/install.sh"
    if ($LASTEXITCODE -ne 0) { throw 'Could not transfer the relay installer.' }
    Write-Host 'Installing/updating the relay from the public main branch. Configuration is backed up; this sender is added without removing the other PC.' -ForegroundColor Yellow
    & $ssh -t -o StrictHostKeyChecking=yes -- $target "sudo bash $remote/install.sh --repo https://github.com/wannabexaker/HIDway-.git --ref main --bind $RelayIP --allow-add $SenderIP --uart $Uart --serial $Serial"
    if ($LASTEXITCODE -ne 0) { throw 'Relay installation failed. Read the message above; existing relay backups remain on the Pi.' }
    & $ssh -t -o StrictHostKeyChecking=yes -- $target 'systemctl is-active hidwayd; systemctl --no-pager status hidwayd'
    if ($LASTEXITCODE -ne 0) { throw 'Relay was installed but its service is not healthy. Check serial wiring/device and the service status above.' }
    $rc=0;Write-Host 'Relay service is active. Continue with the Pico and test the connection in HIDway.' -ForegroundColor Green
} catch { Write-Host $_.Exception.Message -ForegroundColor Red }
finally {
    if ($ResultFile) { [IO.File]::WriteAllText($ResultFile,[string]$rc) }
    $null=Read-Host 'Press Enter to close this setup terminal'
}
exit $rc
