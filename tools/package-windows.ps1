param([Parameter(Mandatory=$true)][string]$BuildDir,[Parameter(Mandatory=$true)][string]$FirmwareDir,[Parameter(Mandatory=$true)][string]$OutDir,[Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Version)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$stage=Join-Path ([IO.Path]::GetFullPath($OutDir)) "HIDway-$Version"
if(Test-Path -LiteralPath $stage){throw "Package folder already exists: $stage. Choose a new output folder/version."}
New-Item -ItemType Directory -Path (Join-Path $stage 'firmware'),(Join-Path $stage 'docs\images') -Force | Out-Null
foreach($item in @(@('client','hidway-client.exe'),@('client','hidway-probe.exe'),@('tools\input-monitor','hidway-input-monitor.exe'),@('tools\setup','HIDway-Setup.exe'))){
    $path=Join-Path (Join-Path $BuildDir $item[0]) $item[1]
    if(-not(Test-Path -LiteralPath $path)){$path=Join-Path (Join-Path (Join-Path $BuildDir $item[0]) 'Release') $item[1]}
    Copy-Item -LiteralPath $path -Destination $stage
}
Copy-Item (Join-Path $root 'tools\setup\*.ps1') $stage
Copy-Item (Join-Path $root 'tools\setup\hidway-updater.psm1') $stage
Copy-Item -LiteralPath (Join-Path $root 'tools\setup\Setup.cmd'),(Join-Path $root 'tools\input-monitor\Start-Input-Monitor.cmd'),(Join-Path $root 'client\hidway.ini.example'),(Join-Path $root 'LICENSE'),(Join-Path $root 'README.md'),(Join-Path $root 'SECURITY.md'),(Join-Path $root 'docs\INPUT_MONITOR.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $root 'relay\deploy\install.sh') -Destination (Join-Path $stage 'relay-install.sh')
Copy-Item (Join-Path $root 'docs\*.md') (Join-Path $stage 'docs')
Copy-Item -LiteralPath (Join-Path $root 'docs\images\client.png') -Destination (Join-Path $stage 'docs\images')
foreach($board in @('pico','pico_w','pico2','pico2_w')){Copy-Item -LiteralPath (Join-Path $FirmwareDir "hidway_fw_$board.uf2") -Destination (Join-Path $stage 'firmware')}
[IO.File]::WriteAllText((Join-Path $stage 'VERSION'),$Version+[Environment]::NewLine)
$lines=@();foreach($file in Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName){$relative=$file.FullName.Substring($stage.Length+1).Replace('\','/');$lines+=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLower()+'  '+$relative}
[IO.File]::WriteAllLines((Join-Path $stage 'PAYLOAD.sha256'),$lines)
. (Join-Path $root 'tools\setup\hidway-setup-core.ps1');$null=Get-HidwayPayload $stage
$zip=$stage+'-windows.zip';Compress-Archive -LiteralPath $stage -DestinationPath $zip
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText(($zip+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($zip)+[Environment]::NewLine)
Write-Output "Package: $zip`nSetup: $(Join-Path $stage 'HIDway-Setup.exe')`nSHA256: $hash"
