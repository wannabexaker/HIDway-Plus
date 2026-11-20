#requires -Version 5.1
<#
Update every part of a HIDway setup from one place.

  1. HIDway on this PC, with the same verified steps as the Updates window
     (check, download, digest and manifest checks, install, previous version
     kept for Rollback).
  2. HIDway on a second Windows PC, over SSH, with the same steps.
  3. The relay on the Raspberry Pi, over SSH (sudo hidway-update).

Nothing is closed, stopped or armed. A PC on which HIDway, the Monitor, Probe or
Setup is running is skipped and named; the relay waits by itself while a session
is live. The Pico firmware is not touched: flashing needs the BOOTSEL button.

Hosts are names your own ssh already knows (~/.ssh/config). Put them once in
update-all.local.ini beside this script (see update-all.example.ini) or pass
them: update-all.ps1 -RemotePC gamingpc -Relay pi
#>
param(
    [string]$Dir = '',        # installation folder on this PC; default: the registered installation
    [string]$RemotePC = '',   # ssh host of the second Windows PC
    [string]$RemoteDir = '',  # its installation folder; default: its registered installation
    [string]$Relay = '',      # ssh host of the Raspberry Pi
    [switch]$CheckOnly,       # report what would be done; download and install nothing
    [switch]$AllowPreview,    # also replace a preview build, whose version cannot be compared
    [switch]$ThisPCOnly,      # internal: the part that runs on each Windows PC
    [string]$Module = ''      # internal: updater module to use
)
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Say([string]$Text = '') { Write-Host $Text }

# Returns 0 (up to date or updated), 1 (failed) or 2 (skipped, nothing changed).
function Update-ThisPC([string]$Folder, [string]$ModulePath) {
    if (-not $Folder) {
        $entry = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HIDway' -ErrorAction SilentlyContinue
        if ($entry -and $entry.PSObject.Properties['InstallLocation']) { $Folder = [string]$entry.InstallLocation }
    }
    if (-not $Folder -or -not (Test-Path -LiteralPath (Join-Path $Folder 'VERSION') -PathType Leaf)) {
        Say '  result     NOT UPDATED: HIDway is not installed here. Install it once with HIDway-Setup.exe from a release package.'
        return 1
    }
    try {
        Import-Module $ModulePath -Force
        $current = Get-HidwayInstalledVersion $Folder
        Say "  folder     $Folder"
        Say "  installed  $current"
        $offer = Get-HidwayLatestUpdate $current
        if ($offer.PSObject.Properties['Tag']) { Say "  latest     $($offer.Tag)" }
        if ($offer.State -eq 'Current') { Say '  result     up to date'; return 0 }
        if ($offer.State -eq 'Preview' -and -not $AllowPreview) {
            Say '  result     SKIPPED: a preview build is installed and cannot be compared with a release. Add -AllowPreview to replace it.'
            return 2
        }
        if ($offer.State -notin @('Available', 'Preview')) { Say "  result     NOT UPDATED: $($offer.Message)"; return 1 }
        if ($CheckOnly) { Say "  result     update available: $($offer.Tag) (check only, nothing installed)"; return 0 }
        $running = @(Get-Process -Name hidway-client, hidway-probe, hidway-input-monitor, HIDway-Setup -ErrorAction SilentlyContinue)
        if ($running.Count) {
            Say "  result     SKIPPED: $(($running | ForEach-Object { $_.Name } | Sort-Object -Unique) -join ', ') is running. Close it and run this again. Nothing was stopped or changed."
            return 2
        }
        $settings = Join-Path $Folder 'hidway.ini'
        $before = if (Test-Path -LiteralPath $settings) { Get-HidwaySHA256 $settings } else { '' }
        $cache = Join-Path $env:LOCALAPPDATA 'HIDway\Updates'
        $null = New-Item -ItemType Directory -Path $cache -Force
        $prepared = Save-HidwayUpdate $offer $cache
        Say '  download   verified (size, SHA-256, archive paths, manifest)'
        $null = Install-HidwayUpdate $prepared $Folder -AllowPreview:($offer.State -eq 'Preview')
        $after = Get-HidwayInstalledVersion $Folder
        $kept = if ($before) { (Get-HidwaySHA256 $settings) -ceq $before } else { $true }
        $rollback = Get-HidwayRollback $Folder
        Say "  result     UPDATED to $after; settings unchanged: $kept; previous version kept for Rollback: $($rollback.Version)"
        if (-not $kept) { return 1 }
        return 0
    } catch { Say "  result     NOT UPDATED: $($_.Exception.Message)"; return 1 }
}

if (-not $Module) {
    $Module = Join-Path $PSScriptRoot 'hidway-updater.psm1'
    if (-not (Test-Path -LiteralPath $Module)) { $Module = Join-Path $PSScriptRoot 'setup\hidway-updater.psm1' }
}
$Module = (Resolve-Path -LiteralPath $Module).Path
if ($ThisPCOnly) { exit (Update-ThisPC $Dir $Module) }

# Saved choices: key = value lines; parameters given on the command line win.
$saved = Join-Path $PSScriptRoot 'update-all.local.ini'
if (Test-Path -LiteralPath $saved) {
    foreach ($line in [IO.File]::ReadAllLines($saved)) {
        if ($line -match '^\s*(dir|remote_pc|remote_dir|relay)\s*=\s*(.*?)\s*$') {
            switch ($Matches[1]) {
                'dir' { if (-not $Dir) { $Dir = $Matches[2] } }
                'remote_pc' { if (-not $RemotePC) { $RemotePC = $Matches[2] } }
                'remote_dir' { if (-not $RemoteDir) { $RemoteDir = $Matches[2] } }
                'relay' { if (-not $Relay) { $Relay = $Matches[2] } }
            }
        }
    }
}
# ssh joins its arguments with spaces for the remote shell, so only plain values are passed on.
foreach ($name in @($RemotePC, $Relay)) {
    if ($name -and $name -notmatch '^[A-Za-z0-9][A-Za-z0-9._@-]*\z') { throw "Not a plain ssh host name: $name" }
}
if ($RemoteDir -and $RemoteDir -notmatch '^[A-Za-z]:\\[A-Za-z0-9_.\\-]+\z') {
    throw 'RemoteDir must be a plain local path without spaces. Leave it empty to use the registered installation of that PC.'
}
$ssh = Join-Path $env:SystemRoot 'System32\OpenSSH\ssh.exe'
$scp = Join-Path $env:SystemRoot 'System32\OpenSSH\scp.exe'
$worst = 0
function Add-Result([int]$Code) { if ($Code -ne 0 -and ($script:worst -eq 0 -or $Code -eq 1)) { $script:worst = $Code } }
# Runs a native program without its error stream ending the script; returns its exit code.
function Invoke-Remote([string]$Program, [string[]]$Arguments, [string]$Prefix = '') {
    $ErrorActionPreference = 'Continue'
    & $Program @Arguments 2>$null | ForEach-Object { Say "$Prefix$_" }
    return $LASTEXITCODE
}

Say ('HIDway: update everything' + $(if ($CheckOnly) { ' (check only)' } else { '' }))
Say
Say "[1] This PC ($env:COMPUTERNAME)"
Add-Result (Update-ThisPC $Dir $Module)

Say
if ($RemotePC) {
    Say "[2] Second PC ($RemotePC, over SSH)"
    $stage = 'hidway-update-' + [guid]::NewGuid().ToString('N')
    $base = @('-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15', '--', $RemotePC)
    $code = 1
    # A folder in the remote home directory, made and removed with plain commands that need no quoting.
    if (Invoke-Remote $ssh ($base + @('cmd', '/c', 'mkdir', $stage))) {
        Say "  result     NOT UPDATED: cannot reach $RemotePC over SSH"
    } else {
        try {
            if (Invoke-Remote $scp @('-q', '-o', 'BatchMode=yes', '--', $PSCommandPath, $Module, "${RemotePC}:$stage/")) {
                Say "  result     NOT UPDATED: cannot copy the updater to $RemotePC"
            } else {
                $remote = @('powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$stage/update-all.ps1", '-ThisPCOnly', '-Module', "$stage/hidway-updater.psm1")
                if ($RemoteDir) { $remote += @('-Dir', $RemoteDir) }
                if ($CheckOnly) { $remote += '-CheckOnly' }
                if ($AllowPreview) { $remote += '-AllowPreview' }
                $code = Invoke-Remote $ssh ($base + $remote)
            }
        } finally {
            $null = Invoke-Remote $ssh ($base + @('cmd', '/c', 'rmdir', '/s', '/q', $stage))
        }
    }
    Add-Result $code
} else { Say '[2] Second PC: not configured (remote_pc in update-all.local.ini)' }

Say
if ($Relay) {
    Say "[3] Relay on the Raspberry Pi ($Relay, over SSH)"
    if ($CheckOnly) {
        $code = Invoke-Remote $ssh @('-o', 'ConnectTimeout=15', '--', $Relay, 'sudo', 'hidway-update', '--check') '  '
        $code = if ($code -in 0, 10) { 0 } else { 1 }
    } else {
        $code = [int][bool](Invoke-Remote $ssh @('-t', '-o', 'ConnectTimeout=15', '--', $Relay, 'sudo', 'hidway-update') '  ')
    }
    if ($code) { Say '  result     FAILED: ssh or hidway-update reported an error' }
    $null = Invoke-Remote $ssh @('-o', 'ConnectTimeout=15', '--', $Relay, 'hidway-status', '--short') '  status     '
    Add-Result $code
} else { Say '[3] Relay: not configured (relay in update-all.local.ini)' }

Say
Say '[4] Pico firmware'
Say '  Not updated from here: the Pico has to be put in BOOTSEL mode by hand (hold BOOTSEL while plugging it in).'
Say '  Only needed when the release notes say the firmware changed; then run HIDway-Setup.exe and use its Pico step.'
Say
if ($worst -eq 0) { Say $(if ($CheckOnly) { 'Check finished.' } else { 'Done: everything is up to date.' }) }
elseif ($worst -eq 2) { Say 'Done, with one or more parts skipped (see above). Nothing was stopped.' }
else { Say 'Finished with a failure (see above).' }
exit $worst
