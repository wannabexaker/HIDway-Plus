#requires -Version 5.1
# Fixed-project Windows updater. No external modules, credentials or remote PC.
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
Set-Variable -Name Owner -Value 'wannabexaker' -Option Constant -Scope Script
Set-Variable -Name Repo -Value 'HIDway' -Option Constant -Scope Script
$script:Limit = 128MB

function Get-HidwayUpdateSource {
    [pscustomobject]@{ Owner = $script:Owner; Repo = $script:Repo
        Web = "https://github.com/$script:Owner/$script:Repo"
        Latest = "https://api.github.com/repos/$script:Owner/$script:Repo/releases/latest" }
}
function Get-HidwayParts([string]$Version) {
    if ($Version.Length -gt 128 -or $Version -cnotmatch '^v?([0-9]+)\.([0-9]+)\.([0-9]+)$') { return $null }
    $parts = @($Matches[1], $Matches[2], $Matches[3])
    for ($i = 0; $i -lt 3; $i++) { $parts[$i] = $parts[$i].TrimStart('0'); if (!$parts[$i]) { $parts[$i] = '0' } }
    return $parts
}
function Compare-HidwayVersions([string]$Left, [string]$Right) {
    $a = Get-HidwayParts $Left; $b = Get-HidwayParts $Right
    if (!$a -or !$b) { throw 'Versions are not comparable stable versions.' }
    for ($i = 0; $i -lt 3; $i++) {
        if ($a[$i].Length -ne $b[$i].Length) { return [Math]::Sign($a[$i].Length - $b[$i].Length) }
        $order = [string]::CompareOrdinal($a[$i], $b[$i]); if ($order) { return [Math]::Sign($order) }
    }
    return 0
}
function Assert-HidwayLocalPath([string]$Path) {
    if (!$Path -or $Path -notmatch '^[A-Za-z]:[\\/]') { throw 'Choose a dedicated local Windows folder. Network paths are not supported.' }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($full))
    if ($drive.DriveType -ne [IO.DriveType]::Fixed -or $full.Length -lt 8) { throw 'The update folder must be on a local fixed drive.' }
    foreach ($protected in @($env:WINDIR, $env:USERPROFILE, $env:LOCALAPPDATA, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ($protected -and $full -eq [IO.Path]::GetFullPath($protected).TrimEnd('\')) { throw 'Choose a dedicated application subfolder.' }
    }
    for ($part = $full; $part; $part = [IO.Path]::GetDirectoryName($part)) {
        if ((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Junctions and symbolic links are not allowed in update paths.' }
    }
    return $full
}
function Get-HidwayInstalledVersion([string]$Directory) {
    $path = Join-Path (Assert-HidwayLocalPath $Directory) 'VERSION'
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { return 'unknown' }
    if ((Get-Item -LiteralPath $path).Length -gt 256) { throw 'Installed VERSION file is too large.' }
    return [IO.File]::ReadAllText($path).Trim()
}
function Get-HidwaySHA256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path); $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function Get-HidwayReleaseOffer([string]$CurrentVersion, $Release) {
    if (!$Release) { return [pscustomobject]@{ State = 'None'; Current = $CurrentVersion; Message = 'No stable release is published, or the fixed repository is unavailable (404).' } }
    if ($Release.draft -isnot [bool] -or $Release.prerelease -isnot [bool] -or $Release.draft -or $Release.prerelease -or
        $Release.tag_name -isnot [string] -or $Release.tag_name.Length -gt 128 -or $Release.tag_name -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Rejected release: expected a non-draft, non-prerelease vX.Y.Z tag.' }
    $tag = $Release.tag_name; $name = "HIDway-$tag-windows.zip"
    $assets = @($Release.assets | Where-Object { $_.name -ceq $name })
    if ($assets.Count -ne 1) { throw "Rejected release: expected exactly one $name asset." }
    $asset = $assets[0]; $url = (Get-HidwayUpdateSource).Web + "/releases/download/$tag/$name"
    if ($asset.browser_download_url -isnot [string] -or $asset.browser_download_url -cne $url -or $asset.state -cne 'uploaded' -or
        ($asset.size -isnot [int] -and $asset.size -isnot [long]) -or $asset.size -le 1 -or $asset.size -gt $script:Limit -or
        $asset.digest -isnot [string] -or $asset.digest -cnotmatch '^sha256:[0-9a-f]{64}$') { throw 'Rejected asset: URL, uploaded state, size or lowercase SHA-256 digest is invalid.' }
    $state = 'Preview'
    if ($null -ne (Get-HidwayParts $CurrentVersion)) {
        $order = Compare-HidwayVersions $tag $CurrentVersion
        $state = if ($order -gt 0) { 'Available' } elseif (!$order) { 'Current' } else { 'Older' }
    }
    $messages = @{ Available = 'A newer stable release is available.'; Current = 'Up to date.'
        Older = 'Published release is older than installed. Downgrade refused.'
        Preview = 'Installed version is not comparable. Installing a stable release requires your explicit choice.' }
    [pscustomobject]@{ State = $state; Current = $CurrentVersion; Tag = $tag; Name = $name; Url = $url; Hash = $asset.digest.Substring(7); Size = [long]$asset.size; Message = $messages[$state] }
}
function Invoke-HidwayRequest([ValidateSet('Api','Asset')][string]$Kind, [string]$Tag = '', [string]$OutFile = '', [long]$Size = 0) {
    # No caller-supplied URL, token or repository parameter exists.
    $source = Get-HidwayUpdateSource; $url = $source.Latest; $limit = 2MB
    if ($Kind -eq 'Asset') {
        if ($Tag.Length -gt 128 -or $Tag -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+$' -or $Size -le 1 -or $Size -gt $script:Limit) { throw 'Invalid download parameters.' }
        $null = Assert-HidwayLocalPath $OutFile
        $url = $source.Web + "/releases/download/$Tag/HIDway-$Tag-windows.zip"; $limit = $Size
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $clock = [Diagnostics.Stopwatch]::StartNew()
    for ($hop = 0; $hop -lt 6; $hop++) {
        $request = [Net.HttpWebRequest]::Create($url)
        $request.Method = 'GET'; $request.AllowAutoRedirect = $false
        $request.UserAgent = 'HIDway-Windows-Updater/1.0 (stable releases; no telemetry)'
        $request.Accept = if ($Kind -eq 'Api') { 'application/vnd.github+json' } else { 'application/octet-stream' }
        $request.Timeout = 15000; $request.ReadWriteTimeout = 15000; $request.UseDefaultCredentials = $false
        $response = $null
        try { $response = $request.GetResponse() }
        catch [Net.WebException] { if (!$_.Exception.Response) { throw 'Network request failed. Check the connection and try again later.' }; $response = $_.Exception.Response }
        try {
            $code = [int]$response.StatusCode
            if ($code -in @(301,302,303,307,308)) {
                if ($Kind -eq 'Api') { throw 'Repository API redirected. Refusing a renamed or different repository.' }
                $next = $null
                if (![Uri]::TryCreate($response.Headers['Location'], [UriKind]::Absolute, [ref]$next) -or $next.Scheme -cne 'https' -or !$next.IsDefaultPort -or $next.UserInfo -or
                    $next.Host -notin @('release-assets.githubusercontent.com','objects.githubusercontent.com')) { throw 'Download redirected outside GitHub release storage.' }
                $url = $next.AbsoluteUri; continue
            }
            if ($code -ne 200) { return [pscustomobject]@{ Status = $code; Body = '' } }
            if ($response.ContentLength -gt $limit) { throw 'Response exceeds the permitted size.' }
            $inputStream = $response.GetResponseStream()
            $outputStream = if ($Kind -eq 'Asset') { [IO.File]::Open($OutFile, 'CreateNew', 'Write', 'None') } else { New-Object IO.MemoryStream }
            try {
                $buffer = New-Object byte[] 65536; $total = 0L
                while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $total += $count
                    if ($total -gt $limit -or $clock.Elapsed.TotalSeconds -gt 120) { throw 'Download exceeded its size or time limit.' }
                    $outputStream.Write($buffer, 0, $count)
                }
                if ($Kind -eq 'Asset' -and $total -ne $Size) { throw 'Downloaded size differs from release metadata.' }
                $body = if ($Kind -eq 'Api') { [Text.Encoding]::UTF8.GetString($outputStream.ToArray()) } else { '' }
            } finally { $outputStream.Dispose(); $inputStream.Dispose() }
            return [pscustomobject]@{ Status = 200; Body = $body }
        } finally { if ($response) { $response.Close() } }
    }
    throw 'Too many GitHub release download redirects.'
}
function Get-HidwayLatestUpdate([string]$CurrentVersion) {
    try {
        $reply = Invoke-HidwayRequest -Kind Api
        if ($reply.Status -eq 404) { return Get-HidwayReleaseOffer $CurrentVersion $null }
        if ($reply.Status -in @(403,429)) { return [pscustomobject]@{ State = 'RateLimited'; Message = 'GitHub rate limit or access restriction. Try again later; no automatic retry is scheduled.' } }
        if ($reply.Status -ne 200) { throw "GitHub returned HTTP $($reply.Status). Try again later." }
        return Get-HidwayReleaseOffer $CurrentVersion ($reply.Body | ConvertFrom-Json)
    } catch { return [pscustomobject]@{ State = 'Error'; Message = "Update check failed: $($_.Exception.Message)" } }
}
function Test-HidwayPayloadName([string]$Name) {
    $top = @('hidway-client.exe','hidway-probe.exe','hidway-input-monitor.exe','HIDway-Setup.exe','hidway-updater.psm1','hidway-updates.ps1','hidway-update-core.ps1',
        'hidway-setup.ps1','hidway-setup-core.ps1','hidway-setup-gui.ps1','hidway-uninstall.ps1','hidway-pi-setup.ps1','Setup.cmd','Start-Input-Monitor.cmd',
        'hidway.ini.example','VERSION','README.md','LICENSE','SECURITY.md','INPUT_MONITOR.md','relay-install.sh')
    return $Name -in $top -or $Name -match '^docs/[A-Za-z0-9_-]+\.md$' -or $Name -eq 'docs/images/client.png' -or $Name -match '^firmware/hidway_fw_(pico|pico_w|pico2|pico2_w)\.uf2$'
}
function Read-HidwayPayload([string]$Source) {
    $null = Assert-HidwayLocalPath $Source; $manifest = Join-Path $Source 'PAYLOAD.sha256'
    if (!(Test-Path -LiteralPath $manifest -PathType Leaf) -or (Get-Item -LiteralPath $manifest).Length -gt 65536) { throw 'Missing or oversized PAYLOAD.sha256.' }
    $names = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase); $files = @()
    foreach ($line in [IO.File]::ReadAllLines($manifest)) {
        if ($line -cnotmatch '^([0-9a-f]{64})  ([A-Za-z0-9_./-]+)$') { throw 'Invalid payload manifest line.' }
        $hash = $Matches[1]; $name = $Matches[2]
        if (!(Test-HidwayPayloadName $name) -or !$names.Add($name)) { throw 'Unexpected or duplicate payload path.' }
        $path = Join-Path $Source $name; $null = Assert-HidwayLocalPath $path
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-HidwaySHA256 $path) -cne $hash) { throw "Payload integrity check failed: $name" }
        $files += $name
    }
    foreach ($required in @('hidway-client.exe','hidway-probe.exe','VERSION','hidway-updater.psm1','hidway-updates.ps1')) { if (!$names.Contains($required)) { throw "Required payload missing: $required" } }
    return $files
}
function Expand-HidwayUpdate([string]$Archive, [string]$Directory, [string]$Tag) {
    $null = Assert-HidwayLocalPath $Archive
    if ($Tag.Length -gt 128 -or $Tag -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Invalid package tag.' }
    $null = Assert-HidwayLocalPath $Directory
    if (Test-Path -LiteralPath $Directory) { throw 'Extraction requires a new directory.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if ($zip.Entries.Count -lt 1 -or $zip.Entries.Count -gt 256) { throw 'Invalid archive entry count.' }
        $prefix = "HIDway-$Tag/"; $total = 0L; $archiveFiles = @()
        $names = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if (!$name.StartsWith($prefix,[StringComparison]::Ordinal) -or $name -match '(^|/)\.\.?(/|$)|:|//|[. ](/|$)' -or !$names.Add($name) -or
                (($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960) { throw 'Unsafe or duplicate archive path.' }
            $relative = $name.Substring($prefix.Length)
            if (!$name.EndsWith('/')) {
                if ($relative -ne 'PAYLOAD.sha256' -and !(Test-HidwayPayloadName $relative)) { throw 'Unexpected archive file.' }
                $archiveFiles += $relative
            }
            $total += $entry.Length
            if ($entry.Length -gt $script:Limit -or $total -gt $script:Limit) { throw 'Expanded archive exceeds 128 MiB.' }
        }
    } finally { $zip.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive,$Directory)
    $source = Join-Path $Directory "HIDway-$Tag"; $files = @(Read-HidwayPayload $source)
    if ($archiveFiles.Count -ne $files.Count + 1) { throw 'Archive contains files outside its manifest.' }
    if ((Get-HidwayInstalledVersion $source) -cne $Tag) { throw 'Package VERSION does not match release tag.' }
    return $source
}
function Confirm-HidwayOffer($Offer, [string]$Current) {
    $release = [pscustomobject]@{ tag_name = $Offer.Tag; draft = $false; prerelease = $false; assets = @([pscustomobject]@{
        name = $Offer.Name; browser_download_url = $Offer.Url; state = 'uploaded'; size = $Offer.Size; digest = ('sha256:' + $Offer.Hash) }) }
    $checked = Get-HidwayReleaseOffer $Current $release
    if ($checked.State -notin @('Available','Preview')) { throw $checked.Message }; return $checked
}
function Save-HidwayUpdate($Offer, [string]$Cache) {
    $checked = Confirm-HidwayOffer $Offer $Offer.Current; $cachePath = Assert-HidwayLocalPath $Cache
    $work = Join-Path $cachePath ([guid]::NewGuid().ToString('N')); $null = [IO.Directory]::CreateDirectory($work)
    $archive = Join-Path $work $checked.Name
    $reply = Invoke-HidwayRequest -Kind Asset -Tag $checked.Tag -OutFile $archive -Size $checked.Size
    if ($reply.Status -ne 200) { throw "GitHub download failed (HTTP $($reply.Status)). Nothing installed." }
    if ((Get-Item -LiteralPath $archive).Length -ne $checked.Size -or (Get-HidwaySHA256 $archive) -cne $checked.Hash) { throw 'Downloaded SHA-256 or size mismatch. Nothing installed.' }
    $null = Expand-HidwayUpdate $archive (Join-Path $work 'validated') $checked.Tag
    return [pscustomobject]@{ Archive = $archive; Offer = $checked; Tag = $checked.Tag }
}
function Get-HidwayTree([string]$Root) {
    $null = Assert-HidwayLocalPath $Root; $queue = New-Object 'Collections.Generic.Queue[string]'; $queue.Enqueue($Root)
    while ($queue.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Application tree contains a junction or symbolic link.' }
            if ($item.PSIsContainer) { $queue.Enqueue($item.FullName) } else { $item }
        }
    }
}
function Copy-HidwayTree([string]$Source,[string]$Destination) {
    $null = Assert-HidwayLocalPath $Destination; $null = [IO.Directory]::CreateDirectory($Destination)
    foreach ($file in Get-HidwayTree $Source) {
        $target = Join-Path $Destination $file.FullName.Substring($Source.Length + 1)
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)); [IO.File]::Copy($file.FullName,$target,$true)
    }
}
function Get-HidwayStore([string]$Destination) {
    $dest = Assert-HidwayLocalPath $Destination
    $store = Join-Path ([IO.Path]::GetDirectoryName($dest)) ('.' + [IO.Path]::GetFileName($dest) + '.hidway-updates')
    $null = Assert-HidwayLocalPath $store
    [pscustomobject]@{ Destination = $dest; Root = $store; State = (Join-Path $store 'state.json'); Journal = (Join-Path $store 'transaction.json') }
}
function Write-HidwayRecord([string]$Path,$Value) {
    $temp = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    $bytes = [Text.Encoding]::UTF8.GetBytes(($Value | ConvertTo-Json -Depth 8 -Compress))
    $stream = [IO.File]::Open($temp,'CreateNew','Write','None')
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    if ([IO.File]::Exists($Path)) {
        $backup = $Path + '.' + [guid]::NewGuid().ToString('N') + '.bak'
        [IO.File]::Replace($temp,$Path,$backup); [IO.File]::Delete($backup)
    } else { [IO.File]::Move($temp,$Path) }
}
function Read-HidwayRecord([string]$Path) {
    $null = Assert-HidwayLocalPath $Path
    if ((Get-Item -LiteralPath $Path).Length -gt 1MB) { throw 'Update state file is too large.' }
    return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json)
}
function Assert-HidwayClosed {
    if (@(Get-Process -Name hidway-client,hidway-probe,hidway-input-monitor,HIDway-Setup -ErrorAction SilentlyContinue).Count) { throw 'Close HIDway, Probe, Monitor and Setup before installing or rolling back. No running instance was stopped.' }
}
function Get-HidwayOwnerGuards {
    $guards = @()
    try {
        foreach ($name in @('Local\HIDwayClientInputOwner','Local\HIDwayInputMonitorOwner')) {
            $created = $false; $guard = New-Object Threading.Mutex($true,$name,[ref]$created)
            if (!$created) { $guard.Dispose(); throw 'An input application still owns its session. Close it before updating.' }
            $guards += $guard
        }
        return $guards
    } catch { foreach ($guard in $guards) { $guard.ReleaseMutex(); $guard.Dispose() }; throw }
}
function Get-HidwaySnapshot([string]$Directory) {
    $files = @()
    foreach ($file in Get-HidwayTree $Directory) {
        $name = $file.FullName.Substring($Directory.Length + 1).Replace('\','/')
        if ((Test-HidwayPayloadName $name) -or $name -eq 'PAYLOAD.sha256') { $files += [pscustomobject]@{ Name = $name; Hash = (Get-HidwaySHA256 $file.FullName) } }
    }
    return $files
}
function Assert-HidwayCheckpoint($Record,$Store) {
    if ($Record.Destination -cne $Store.Destination -or $Record.Previous -cnotmatch '^previous-[0-9a-f]{32}$' -or @($Record.Files).Count -gt 256) { throw 'Invalid rollback checkpoint.' }
    $folder = Join-Path $Store.Root $Record.Previous; $null = Assert-HidwayLocalPath $folder
    foreach ($entry in $Record.Files) {
        if ((!(Test-HidwayPayloadName $entry.Name) -and $entry.Name -ne 'PAYLOAD.sha256') -or $entry.Hash -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid checkpoint file.' }
        $path = Join-Path $folder $entry.Name; $null = Assert-HidwayLocalPath $path
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-HidwaySHA256 $path) -cne $entry.Hash) { throw 'Previous version has changed. Rollback refused.' }
    }
    foreach ($required in @('hidway-client.exe','hidway-probe.exe','VERSION')) { if ($Record.Files.Name -notcontains $required) { throw 'Rollback checkpoint is incomplete.' } }
    return $folder
}
function Get-HidwayRollback([string]$Destination) {
    $store = Get-HidwayStore $Destination
    if (!(Test-Path -LiteralPath $store.State)) { return [pscustomobject]@{ Available = $false; Version = ''; Message = 'No previous version retained yet.' } }
    $record = Read-HidwayRecord $store.State; $folder = Assert-HidwayCheckpoint $record $store
    return [pscustomobject]@{ Available = $true; Version = (Get-HidwayInstalledVersion $folder); Message = 'Previous local version is verified.' }
}
function Move-HidwayDirectory([string]$Source,[string]$Destination) { [IO.Directory]::Move($Source,$Destination) }
function Repair-HidwayTransaction($Store) {
    if (!(Test-Path -LiteralPath $Store.Journal)) { return }
    Assert-HidwayClosed; $record = Read-HidwayRecord $Store.Journal
    if ($record.Destination -cne $Store.Destination -or $record.Candidate -cnotmatch '^candidate-[0-9a-f]{32}$' -or $record.Previous -cnotmatch '^previous-[0-9a-f]{32}$') { throw 'Invalid recovery journal. Manual inspection required.' }
    $previous = Join-Path $Store.Root $record.Previous; $candidate = Join-Path $Store.Root $record.Candidate
    $null = Assert-HidwayLocalPath $previous; $null = Assert-HidwayLocalPath $candidate
    if ((Test-Path -LiteralPath $previous) -and !(Test-Path -LiteralPath $Store.Destination)) {
        $null = Assert-HidwayCheckpoint $record $Store; Move-HidwayDirectory $previous $Store.Destination
    } elseif ((Test-Path -LiteralPath $previous) -and !(Test-Path -LiteralPath $candidate)) {
        $null = Assert-HidwayCheckpoint $record $Store; Write-HidwayRecord $Store.State $record
    } elseif (Test-Path -LiteralPath $previous) { throw 'Ambiguous update recovery state. Manual inspection required.' }
    [IO.File]::Delete($Store.Journal)
}
function Invoke-HidwaySwitch($Store,[string]$Candidate) {
    Assert-HidwayClosed
    $record = [pscustomobject]@{ Destination = $Store.Destination; Candidate = [IO.Path]::GetFileName($Candidate); Previous = ('previous-' + [guid]::NewGuid().ToString('N')); Files = @(Get-HidwaySnapshot $Store.Destination) }
    $previous = Join-Path $Store.Root $record.Previous; $locks = @()
    try {
        # Check file locks before either directory is moved. Owner mutexes
        # prevent client/Monitor restarts throughout this transaction.
        foreach ($folder in @($Store.Destination,$Candidate)) {
            foreach ($name in @('hidway-client.exe','hidway-probe.exe','hidway-input-monitor.exe','HIDway-Setup.exe')) {
                $path = Join-Path $folder $name
                if (Test-Path -LiteralPath $path) { $locks += [IO.File]::Open($path,'Open','Read',[IO.FileShare]::Delete) }
            }
        }
        Assert-HidwayClosed
        # Windows directory renames require child handles to be closed too.
        foreach ($stream in $locks) { $stream.Dispose() }; $locks = @()
        Write-HidwayRecord $Store.Journal $record
        # Two same-volume atomic renames; durable journal covers the gap.
        Move-HidwayDirectory $Store.Destination $previous
        Move-HidwayDirectory $Candidate $Store.Destination
        Write-HidwayRecord $Store.State $record; [IO.File]::Delete($Store.Journal)
    } catch {
        $problem = $_.Exception.Message
        if (!(Test-Path -LiteralPath $Store.Destination) -and (Test-Path -LiteralPath $previous)) { Move-HidwayDirectory $previous $Store.Destination; [IO.File]::Delete($Store.Journal) }
        throw "Update was not completed: $problem. Existing version restored when possible; recovery data retained."
    } finally { foreach ($stream in $locks) { $stream.Dispose() } }
}
function Invoke-HidwayLocalTransaction([string]$Destination,[scriptblock]$Action) {
    $store = Get-HidwayStore $Destination; $null = [IO.Directory]::CreateDirectory($store.Root); $lock = $null; $guards = @()
    try {
        try { $lock = [IO.File]::Open((Join-Path $store.Root 'update.lock'),'OpenOrCreate','ReadWrite','None') } catch { throw 'Another updater owns this installation, or the folder is not writable.' }
        Repair-HidwayTransaction $store; Assert-HidwayClosed
        $guards = @(Get-HidwayOwnerGuards)
        if (!(Test-Path -LiteralPath (Join-Path $store.Destination 'hidway-client.exe')) -or !(Test-Path -LiteralPath (Join-Path $store.Destination 'hidway-probe.exe'))) { throw 'Install the complete Windows tools before using self-update.' }
        Set-Location -LiteralPath ([IO.Path]::GetDirectoryName($store.Destination))
        [Environment]::CurrentDirectory = [IO.Path]::GetDirectoryName($store.Destination)
        & $Action $store
    } finally { foreach ($guard in $guards) { $guard.ReleaseMutex(); $guard.Dispose() }; if ($lock) { $lock.Dispose() } }
}
function Install-HidwayUpdate($Prepared,[string]$Destination,[switch]$AllowPreview) {
    Invoke-HidwayLocalTransaction $Destination {
        param($store)
        $offer = Confirm-HidwayOffer $Prepared.Offer (Get-HidwayInstalledVersion $store.Destination)
        if ($offer.State -eq 'Preview' -and !$AllowPreview) { throw 'Installed version cannot be compared. Explicit Install stable release confirmation is required.' }
        $archive = Assert-HidwayLocalPath $Prepared.Archive
        if ((Get-Item -LiteralPath $archive).Length -ne $offer.Size -or (Get-HidwaySHA256 $archive) -cne $offer.Hash) { throw 'Prepared archive changed. Nothing installed.' }
        $source = Expand-HidwayUpdate $archive (Join-Path $store.Root ('extract-' + [guid]::NewGuid().ToString('N'))) $offer.Tag
        $candidate = Join-Path $store.Root ('candidate-' + [guid]::NewGuid().ToString('N'))
        Copy-HidwayTree $store.Destination $candidate
        foreach ($name in @(Read-HidwayPayload $source) + @('PAYLOAD.sha256')) {
            $target = Join-Path $candidate $name; $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)); [IO.File]::Copy((Join-Path $source $name),$target,$true)
        }
        $null = Read-HidwayPayload $candidate; Invoke-HidwaySwitch $store $candidate
    }
}
function Restore-HidwayUpdate([string]$Destination) {
    Invoke-HidwayLocalTransaction $Destination {
        param($store)
        if (!(Test-Path -LiteralPath $store.State)) { throw 'No previous version is available.' }
        $record = Read-HidwayRecord $store.State; $previous = Assert-HidwayCheckpoint $record $store
        $candidate = Join-Path $store.Root ('candidate-' + [guid]::NewGuid().ToString('N'))
        Copy-HidwayTree $store.Destination $candidate
        foreach ($entry in $record.Files) {
            # Never restore the legacy updater's different repository target.
            if ($entry.Name -in @('hidway-updater.psm1','hidway-updates.ps1','hidway-update-core.ps1','PAYLOAD.sha256')) { continue }
            $target = Join-Path $candidate $entry.Name; $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)); [IO.File]::Copy((Join-Path $previous $entry.Name),$target,$true)
        }
        $lines = @(Get-HidwaySnapshot $candidate | ForEach-Object { if ($_.Name -ne 'PAYLOAD.sha256') { $_.Hash + '  ' + $_.Name } })
        [IO.File]::WriteAllLines((Join-Path $candidate 'PAYLOAD.sha256'),$lines,[Text.Encoding]::ASCII)
        Invoke-HidwaySwitch $store $candidate
    }
}
function Repair-HidwayUpdate([string]$Destination) {
    # Restore/finish only a validated pending journal; never fetch or ARM.
    Invoke-HidwayLocalTransaction $Destination { param($store) }
}
Export-ModuleMember -Function Get-HidwayUpdateSource,Get-HidwayInstalledVersion,Get-HidwaySHA256,Get-HidwayReleaseOffer,Get-HidwayLatestUpdate,Save-HidwayUpdate,Expand-HidwayUpdate,Install-HidwayUpdate,Get-HidwayRollback,Restore-HidwayUpdate,Repair-HidwayUpdate
