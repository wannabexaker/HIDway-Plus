param([Parameter(Mandatory=$true)][string]$Work)
$ErrorActionPreference='Stop'
$module=Import-Module (Join-Path $PSScriptRoot '..\tools\setup\hidway-updater.psm1') -Force -PassThru
$script:checks=0
function Check([bool]$Value,[string]$Name){$script:checks++;if(!$Value){throw "FAIL: $Name"}}
function Reject([scriptblock]$Action,[string]$Name){$failed=$false;try{& $Action | Out-Null}catch{$failed=$true};Check $failed $Name}
$base=Join-Path ([IO.Path]::GetFullPath($Work)) ([guid]::NewGuid().ToString('N'))
$null=[IO.Directory]::CreateDirectory($base)
function Release {
 [pscustomobject]@{tag_name='v1.2.3';draft=$false;prerelease=$false;assets=@([pscustomobject]@{
  name='HIDway-v1.2.3-windows.zip';browser_download_url=((Get-HidwayUpdateSource).Web+'/releases/download/v1.2.3/HIDway-v1.2.3-windows.zip')
  state='uploaded';size=12;digest=('sha256:'+('0'*64))})}
}
Check ((Get-HidwayUpdateSource).Repo -ceq 'HIDway-') 'exact repository with trailing hyphen'
Check ((Get-HidwayReleaseOffer 'v1.0.0' (Release)).State -eq 'Available') 'newer stable'
Check ((Get-HidwayReleaseOffer '1.2.3' (Release)).State -eq 'Current') 'installed semver without v prefix'
Check ((Get-HidwayReleaseOffer 'v2.0.0' (Release)).State -eq 'Older') 'downgrade not offered'
Check ((Get-HidwayReleaseOffer 'dev' (Release)).State -eq 'Preview') 'uncomparable version explicit choice'
$r=Release;$r.prerelease=$true;Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'prerelease rejected'
$r=Release;$r.draft=$true;Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'draft rejected'
$r=Release;$r.tag_name='1.2.3';Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'v prefix required'
$r=Release;$r.tag_name='v1.2.3-rc1';Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'nonstable suffix rejected'
$r=Release;$r.assets[0].digest='sha256:'+('A'*64);Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'uppercase digest rejected'
$r=Release;$r.assets[0].size=1;Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'one byte rejected'
$r=Release;$r.assets[0].size=134217729;Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'oversized rejected'
$r=Release;$r.assets[0].browser_download_url=$r.assets[0].browser_download_url.Replace('/'+(Get-HidwayUpdateSource).Repo+'/','/HIDway/');Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'legacy wrong repository rejected'
$r=Release;$r.assets+=@($r.assets[0]);Reject {Get-HidwayReleaseOffer 'v1.0.0' $r} 'duplicate asset rejected'
# Mock only the private HTTP boundary, never application files or transactions.
& $module {
 $script:httpStatus=404;$script:httpBody='';$script:requests=0;$script:fixtureZip='';$script:running=$false
 function script:Invoke-HidwayRequest($Kind,$Tag,$OutFile,$Size){
  $script:requests++
  if($script:httpStatus -eq -1){throw 'offline'}
  if($Kind -eq 'Asset' -and $script:httpStatus -eq 200){[IO.File]::Copy($script:fixtureZip,$OutFile)}
  [pscustomobject]@{Status=$script:httpStatus;Body=$script:httpBody}
 }
 function script:Get-Process {param($Name,$ErrorAction) if($script:running){[pscustomobject]@{ProcessName='hidway-probe'}}}
 function script:Get-HidwayOwnerGuards {} # Never reserve live application ownership in tests.
}
Check ((Get-HidwayLatestUpdate 'v1.0.0').State -eq 'None') '404 nonfatal'
& $module {$script:httpStatus=403};Check ((Get-HidwayLatestUpdate 'v1.0.0').State -eq 'RateLimited') '403 nonfatal'
& $module {$script:httpStatus=-1};Check ((Get-HidwayLatestUpdate 'v1.0.0').State -eq 'Error') 'network failure nonfatal'
Check ((& $module {$script:requests}) -eq 3) 'one request per check and no retry storm'
& $module {$script:httpStatus=200;$script:httpBody='{broken'};Check ((Get-HidwayLatestUpdate 'v1.0.0').State -eq 'Error') 'invalid API JSON nonfatal'
$payload=Join-Path $base 'HIDway-v1.2.3';$null=[IO.Directory]::CreateDirectory($payload)
foreach($name in @('hidway-client.exe','hidway-probe.exe','hidway-updater.psm1','hidway-updates.ps1')){[IO.File]::WriteAllText((Join-Path $payload $name),"release $name")}
[IO.File]::WriteAllText((Join-Path $payload 'VERSION'),'v1.2.3')
$lines=@(Get-ChildItem -LiteralPath $payload -File | ForEach-Object {(Get-HidwaySHA256 $_.FullName)+'  '+$_.Name})
[IO.File]::WriteAllLines((Join-Path $payload 'PAYLOAD.sha256'),$lines,[Text.Encoding]::ASCII)
$zip=Join-Path $base 'release.zip';Compress-Archive -LiteralPath $payload -DestinationPath $zip
$r=Release;$r.assets[0].size=(Get-Item -LiteralPath $zip).Length;$r.assets[0].digest='sha256:'+(Get-HidwaySHA256 $zip)
& $module {param($path,$json)$script:fixtureZip=$path;$script:httpBody=$json} $zip ($r | ConvertTo-Json -Depth 5 -Compress)
Check ((Get-HidwayLatestUpdate 'v1.0.0').State -eq 'Available') 'mock API happy path'
$offer=Get-HidwayReleaseOffer 'v1.0.0' $r
$bad=Get-HidwayReleaseOffer 'v1.0.0' $r;$bad.Hash='f'*64
Reject {Save-HidwayUpdate $bad (Join-Path $base 'cache')} 'hash mismatch refuses preparation'
$ready=Save-HidwayUpdate $offer (Join-Path $base 'cache')
Check (Test-Path -LiteralPath $ready.Archive) 'happy path download and archive validation'
$dest=Join-Path $base 'installed';$null=[IO.Directory]::CreateDirectory($dest)
foreach($name in @('hidway-client.exe','hidway-probe.exe')){[IO.File]::WriteAllText((Join-Path $dest $name),"old $name")}
[IO.File]::WriteAllText((Join-Path $dest 'VERSION'),'v1.0.0')
[IO.File]::WriteAllText((Join-Path $dest 'hidway.ini'),'user configuration')
[IO.File]::WriteAllText((Join-Path $dest 'hidway-input-monitor.ini'),'user aliases')
[IO.File]::WriteAllText((Join-Path $dest 'session.hidrec'),'user recording')
Check (!(Get-HidwayRollback $dest).Available) 'rollback disabled without previous'
& $module {$script:running=$true}
Reject {Install-HidwayUpdate $ready $dest} 'running probe prevents install'
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.0.0') 'running installation unchanged'
& $module {$script:running=$false}
Install-HidwayUpdate $ready $dest
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.2.3') 'happy path real directory install'
Check ((Get-HidwaySHA256 (Join-Path $dest 'hidway-probe.exe')) -eq (Get-HidwaySHA256 (Join-Path $payload 'hidway-probe.exe'))) 'probe and client installed together'
Check ([IO.File]::ReadAllText((Join-Path $dest 'hidway.ini')) -eq 'user configuration') 'INI retained'
Check ([IO.File]::ReadAllText((Join-Path $dest 'session.hidrec')) -eq 'user recording') 'recording retained'
Check ((Get-HidwayRollback $dest).Available -and (Get-HidwayRollback $dest).Version -eq 'v1.0.0') 'previous version retained and verified'
[IO.File]::WriteAllText((Join-Path $dest 'VERSION'),'v2.0.0')
Reject {Install-HidwayUpdate $ready $dest} 'stale prepared download cannot downgrade actual installed version'
[IO.File]::WriteAllText((Join-Path $dest 'VERSION'),'v1.2.3')
[IO.File]::WriteAllText((Join-Path $dest 'hidway.ini'),'settings changed after update')
Restore-HidwayUpdate $dest
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.0.0') 'explicit local rollback'
Check ([IO.File]::ReadAllText((Join-Path $dest 'hidway.ini')) -eq 'settings changed after update') 'rollback preserves newest INI'
Check ([IO.File]::ReadAllText((Join-Path $dest 'hidway-probe.exe')) -eq 'old hidway-probe.exe') 'rollback restores probe'
Check ((Get-HidwayRollback $dest).Version -eq 'v1.2.3') 'rollback retains version being replaced'
[IO.File]::WriteAllText((Join-Path $dest 'VERSION'),'dev')
Reject {Install-HidwayUpdate $ready $dest} 'preview requires explicit stable install flag'
Install-HidwayUpdate $ready $dest -AllowPreview
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.2.3') 'explicit preview to stable install'
# Fault after first rename: original directory must be put back.
Restore-HidwayUpdate $dest
[IO.File]::WriteAllText((Join-Path $dest 'VERSION'),'v1.0.0')
& $module {
 $script:moves=0
 function script:Move-HidwayDirectory($Source,$Destination){$script:moves++;if($script:moves -eq 2){throw 'injected rename failure'};[IO.Directory]::Move($Source,$Destination)}
}
Reject {Install-HidwayUpdate $ready $dest} 'second rename failure reported'
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.0.0') 'original restored after failed swap'
& $module {Remove-Item Function:script:Move-HidwayDirectory; function script:Move-HidwayDirectory($Source,$Destination){[IO.Directory]::Move($Source,$Destination)}}
Reject {Install-HidwayUpdate $ready '\\example\share\HIDway'} 'remote PC paths rejected'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$unsafe=Join-Path $base 'unsafe.zip';$z=[IO.Compression.ZipFile]::Open($unsafe,'Create');$null=$z.CreateEntry('HIDway-v1.2.3/../outside.exe');$z.Dispose()
Reject {Expand-HidwayUpdate $unsafe (Join-Path $base 'unsafe') 'v1.2.3'} 'ZIP traversal rejected'
& $module {
 param($directory)
 $store=Get-HidwayStore $directory
 $record=[pscustomobject]@{Destination=$store.Destination;Candidate=('candidate-'+[guid]::NewGuid().ToString('N'));Previous=('previous-'+[guid]::NewGuid().ToString('N'));Files=@(Get-HidwaySnapshot $directory)}
 Write-HidwayRecord $store.Journal $record
 [IO.Directory]::Move($directory,(Join-Path $store.Root $record.Previous))
} $dest
Check (!(Test-Path -LiteralPath $dest)) 'simulate interruption between directory renames'
Repair-HidwayUpdate $dest
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.0.0') 'durable journal restores interrupted installation'
$rollbackDir=& $module {param($directory)$store=Get-HidwayStore $directory;$record=Read-HidwayRecord $store.State;Join-Path $store.Root $record.Previous} $dest
[IO.File]::AppendAllText((Join-Path $rollbackDir 'hidway-probe.exe'),'tampered')
Reject {Restore-HidwayUpdate $dest} 'tampered previous version refuses rollback'
Check ((Get-HidwayInstalledVersion $dest) -eq 'v1.0.0') 'failed rollback leaves active version unchanged'
Reject {& $module {Set-Variable -Scope Script -Name Repo -Value 'foreign' -Force -ErrorAction Stop}} 'repository constant cannot be configured'
foreach($path in @((Join-Path $PSScriptRoot '..\tools\setup\hidway-updater.psm1'),(Join-Path $PSScriptRoot '..\tools\setup\hidway-updates.ps1'))){
 Check (@([IO.File]::ReadAllBytes($path) | Where-Object {$_ -gt 127}).Count -eq 0) 'pure ASCII updater source'
 $errors=$null;$null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$null,[ref]$errors);Check (!$errors) 'PowerShell parser accepts source'
}
Write-Output "Stable updater: $checks checks PASS; mocked HTTP, real isolated install/rollback, live apps untouched."
