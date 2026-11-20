#requires -Version 5.1
# -Check: no window; one line "HIDWAY-UPDATE state=<State> tag=<vX.Y.Z or empty>" for the client's start check.
# -AutoInstall: the normal window, which waits for HIDway to close and then checks, downloads and installs by itself.
# -TestHook: tests only; a file of mock functions applied after each module import, and the client is not started.
param([switch]$ValidateOnly,[string]$RenderPath,[switch]$Check,[switch]$AutoInstall,[string]$TestHook)
$ErrorActionPreference='Stop'
$hookCode=''
if($TestHook){$TestHook=[IO.Path]::GetFullPath($TestHook);if(!(Test-Path -LiteralPath $TestHook -PathType Leaf)){throw 'Test hook file not found.'};$hookCode=[IO.File]::ReadAllText($TestHook)}
function Import-HidwayUpdater {
 Import-Module (Join-Path $PSScriptRoot 'hidway-updater.psm1') -Force -WarningAction SilentlyContinue
 if($hookCode){& (Get-Module hidway-updater) ([scriptblock]::Create($hookCode))}
}
if($hookCode){. ([scriptblock]::Create($hookCode))} # the same mocks for this script's own process checks
function Get-HidwayDestination {
 if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'hidway-client.exe')){return $PSScriptRoot}
 $registration=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HIDway' -ErrorAction SilentlyContinue
 if($registration -and $registration.PSObject.Properties['InstallLocation']){return [string]$registration.InstallLocation}
 return ''
}
if($Check){
 # Read-only: one request for the latest release; nothing is downloaded, written or started.
 $WarningPreference='SilentlyContinue';$ProgressPreference='SilentlyContinue'
 $state='Error';$tag=''
 try{
  Import-HidwayUpdater
  $installed='unknown';$folder=Get-HidwayDestination
  if($folder){$installed=Get-HidwayInstalledVersion $folder}
  $answer=Get-HidwayLatestUpdate $installed
  $state=[string]$answer.State
  if($answer.PSObject.Properties['Tag'] -and ([string]$answer.Tag).Length -le 128 -and [string]$answer.Tag -cmatch '^v[0-9]+\.[0-9]+\.[0-9]+$'){$tag=[string]$answer.Tag}
 }catch{$state='Error'}
 if($state -cnotin @('Available','Current','Preview','Older','None','RateLimited','Error')){$state='Error'}
 if($state -ceq 'Available' -and !$tag){$state='Error'}
 if($state -cin @('None','RateLimited','Error')){$tag=''}
 [Console]::Out.Write("HIDWAY-UPDATE state=$state tag=$tag`r`n");[Console]::Out.Flush()
 exit 0
}
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase
Import-HidwayUpdater
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class HidwayUpdateChrome {
 [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr h,int a,ref int v,int n);
}
"@
$source=Get-HidwayUpdateSource
$destination=Get-HidwayDestination
[xml]$xaml=@"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Title="HIDway / Windows updates" Width="760" Height="510" MinWidth="620" MinHeight="420" Background="#1E1F22" Foreground="#ECEDEF" FontFamily="Segoe UI" WindowStartupLocation="CenterScreen">
 <Window.Resources>
  <Style TargetType="TextBlock"><Setter Property="TextWrapping" Value="Wrap"/><Setter Property="Margin" Value="0,6,0,6"/></Style>
  <Style TargetType="Button"><Setter Property="Background" Value="#36383E"/><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="BorderBrush" Value="#484B52"/><Setter Property="Padding" Value="16,9"/><Setter Property="Margin" Value="0,0,8,8"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border x:Name="B" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="1" CornerRadius="5" Padding="{TemplateBinding Padding}"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter TargetName="B" Property="Background" Value="#484B52"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.4"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="B" Property="BorderBrush" Value="#A8ABB2"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
 </Window.Resources>
 <Grid Margin="24" Background="#1E1F22"><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel><TextBlock Text="HIDway / Windows updates" FontSize="26" FontWeight="SemiBold"/><TextBlock x:Name="Source" Foreground="#A8ABB2"/><TextBlock Text="Client, Probe and bundled companions update on this PC only."/></StackPanel>
  <StackPanel Grid.Row="1" Margin="0,8,0,8"><TextBlock x:Name="Installed"/><TextBlock x:Name="Latest" Text="Latest: not checked"/><TextBlock x:Name="Previous" Text="Rollback: none"/><TextBlock x:Name="Folder" Foreground="#A8ABB2"/></StackPanel>
  <ScrollViewer Grid.Row="2" VerticalScrollBarVisibility="Auto"><TextBlock x:Name="Status" Text="Choose Check. Nothing is downloaded or installed automatically."/></ScrollViewer>
  <WrapPanel Grid.Row="3"><Button x:Name="Check" Content="Check"/><Button x:Name="Download" Content="Download" IsEnabled="False"/><Button x:Name="Install" Content="Install" IsEnabled="False"/><Button x:Name="Rollback" Content="Rollback" IsEnabled="False"/></WrapPanel>
  <DockPanel Grid.Row="4"><TextBlock Text="Save your work and close the Windows tools before Install or Rollback." Foreground="#A8ABB2" MaxWidth="490" DockPanel.Dock="Left"/><Button x:Name="Close" Content="Close" HorizontalAlignment="Right"/></DockPanel>
 </Grid>
</Window>
"@
$window=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $xaml))
$area=[Windows.SystemParameters]::WorkArea
if($area.Width -gt 0 -and $area.Height -gt 0){
 $window.MinWidth=[Math]::Min(620,[Math]::Max(320,$area.Width-32));$window.MinHeight=[Math]::Min(420,[Math]::Max(300,$area.Height-32))
 $window.Width=[Math]::Min(760,$area.Width-32);$window.Height=[Math]::Min(510,$area.Height-32)
}
$window.Add_SourceInitialized({
 $handle=(New-Object Windows.Interop.WindowInteropHelper $window).Handle
 $dark=1;$caption=0x221F1E;$text=0xEFEDEC
 $null=[HidwayUpdateChrome]::DwmSetWindowAttribute($handle,20,[ref]$dark,4)
 $null=[HidwayUpdateChrome]::DwmSetWindowAttribute($handle,35,[ref]$caption,4)
 $null=[HidwayUpdateChrome]::DwmSetWindowAttribute($handle,36,[ref]$text,4)
})
$ui=@{};foreach($name in @('Source','Installed','Latest','Previous','Folder','Status','Check','Download','Install','Rollback','Close')){$ui[$name]=$window.FindName($name)}
$ui.Source.Text="Fixed source: $($source.Owner)/$($source.Repo) - stable GitHub Releases"
$ui.Folder.Text=if($destination){$destination}else{'Install the complete Windows tools first.'}
$script:current='unknown';$script:previous=$null;$script:offer=$null;$script:prepared=$null
$script:localReady=$false;$script:instancesClosed=$false;$script:lastProcessCheck=[DateTime]::MinValue
$script:task=$null;$script:async=$null;$script:operation='';$script:closing=$false
# -AutoInstall stages: Wait (for HIDway to close), Check, Download, Install, then Done or Stopped.
$script:autoStage=if($AutoInstall){'Wait'}else{''};$script:autoDeadline=[DateTime]::UtcNow.AddSeconds(30);$script:autoResult='';$script:frame=$null
function Refresh-LocalState {
 $script:previous=$null;$script:localReady=$false
 try{
  if($destination){$script:current=Get-HidwayInstalledVersion $destination;$null=Sync-HidwayRegisteredVersion $destination} # also corrects an entry left stale by an older updater
  $ui.Installed.Text="Installed: $script:current"
  if($destination){
   $script:localReady=(Test-Path -LiteralPath (Join-Path $destination 'hidway-client.exe') -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $destination 'hidway-probe.exe') -PathType Leaf)
   $script:previous=Get-HidwayRollback $destination
  }
  $ui.Previous.Text=if($script:previous -and $script:previous.Available){"Rollback: $($script:previous.Version) (local previous version)"}else{'Rollback: no previous version retained'}
 }catch{$ui.Previous.Text='Rollback: unavailable (local verification failed)';$ui.Status.Text=$_.Exception.Message}
}
function Refresh-ProcessState {
 $script:instancesClosed=@(Get-Process -Name hidway-client,hidway-probe,hidway-input-monitor,HIDway-Setup -ErrorAction SilentlyContinue).Count -eq 0
 $script:lastProcessCheck=[DateTime]::UtcNow
}
function Refresh-UpdateButtons {
 $busy=$null -ne $script:task -or $script:autoStage -in @('Wait','Check','Download','Install')
 $ui.Check.IsEnabled=!$busy
 $ui.Download.IsEnabled=!$busy -and $null -ne $script:offer -and $script:offer.State -in @('Available','Preview')
 $ui.Install.IsEnabled=!$busy -and $null -ne $script:prepared -and $script:localReady -and $script:instancesClosed
 $ui.Install.Content=if($script:offer -and $script:offer.State -eq 'Preview'){'Install stable release...'}else{'Install'}
 $ui.Rollback.IsEnabled=!$busy -and $script:localReady -and $script:instancesClosed -and $null -ne $script:previous -and $script:previous.Available
}
function Start-UpdateTask([string]$Operation,[bool]$AllowPreview=$false) {
 if($script:task){return}
 $script:operation=$Operation
 if($Operation -eq 'Check'){$script:offer=$script:prepared=$null}
 $ui.Status.Text=switch($Operation){Check{'Checking the fixed repository...'} Download{'Downloading and verifying the release...'} Install{'Installing the verified Windows tools...'} Rollback{'Restoring the verified previous local tools...'}}
 $script:task=[PowerShell]::Create()
 $null=$script:task.AddScript({param($root,$operation,$directory,$version,$offer,$prepared,$allow,$cache,$hook)
  $ErrorActionPreference='Stop';Import-Module (Join-Path $root 'hidway-updater.psm1') -Force
  if($hook){& (Get-Module hidway-updater) ([scriptblock]::Create($hook))}
  switch($operation){
   Check{Get-HidwayLatestUpdate $version}
   Download{Save-HidwayUpdate $offer $cache}
   Install{Install-HidwayUpdate $prepared $directory -AllowPreview:$allow;[pscustomobject]@{Message='Windows tools updated. Settings retained. ARM remains manual.'}}
   Rollback{Restore-HidwayUpdate $directory;[pscustomobject]@{Message='Previous tools restored. Current settings and the fixed-source updater were retained.'}}
  }
 }).AddArgument($PSScriptRoot).AddArgument($Operation).AddArgument($destination).AddArgument($script:current).AddArgument($script:offer).AddArgument($script:prepared).AddArgument($AllowPreview).AddArgument((Join-Path $env:LOCALAPPDATA 'HIDway\Updates')).AddArgument($hookCode)
 $script:async=$script:task.BeginInvoke();Refresh-UpdateButtons
}
function Stop-AutoInstall([string]$Message) {
 # Leaves the window open with the reason and the normal buttons; nothing more is started or installed.
 if($Message){$ui.Status.Text=$Message}
 $script:autoStage='Stopped';$script:autoResult='Stopped';Refresh-UpdateButtons
 if($script:frame){$script:frame.Continue=$false}
}
function Start-HidwayClient {
 # The installed client always starts disarmed. Tests (-TestHook) never start it.
 if($TestHook){return}
 $info=New-Object Diagnostics.ProcessStartInfo (Join-Path $destination 'hidway-client.exe')
 $info.WorkingDirectory=$destination;$info.UseShellExecute=$true
 $null=[Diagnostics.Process]::Start($info)
}
function Step-AutoInstall([bool]$Succeeded) {
 # After each -AutoInstall operation: the same tasks as the buttons, one after the other.
 if(!$Succeeded){Stop-AutoInstall '';return} # the module's message is already in the window
 switch($script:autoStage){
  Check{
   if($script:offer.State -eq 'Available'){$script:autoStage='Download';Start-UpdateTask 'Download'}
   elseif($script:offer.State -eq 'Preview'){Stop-AutoInstall "$($script:offer.Message) Choose Download, then Install stable release... to confirm."}
   else{Stop-AutoInstall $script:offer.Message}
  }
  Download{$script:autoStage='Install';Start-UpdateTask 'Install' $false}
  Install{
   $script:autoStage='Done';$script:autoResult='Installed';Refresh-UpdateButtons
   try{Start-HidwayClient}catch{$ui.Status.Text="$($ui.Status.Text) HIDway could not be started: $($_.Exception.Message)";if($script:frame){$script:frame.Continue=$false};return}
   if($script:frame){$script:frame.Continue=$false}else{$window.Close()}
  }
 }
}
$timer=New-Object Windows.Threading.DispatcherTimer;$timer.Interval=[TimeSpan]::FromMilliseconds(200)
$timer.Add_Tick({
 if(!$script:task -and ([DateTime]::UtcNow-$script:lastProcessCheck).TotalSeconds -ge 1){Refresh-ProcessState;Refresh-UpdateButtons}
 if($script:autoStage -eq 'Wait' -and !$script:task){
  if(@(Get-Process -Name hidway-client -ErrorAction SilentlyContinue).Count -eq 0){$script:autoStage='Check';Refresh-LocalState;Start-UpdateTask 'Check'}
  elseif([DateTime]::UtcNow -gt $script:autoDeadline){Stop-AutoInstall 'HIDway is still open, so nothing was installed. Close HIDway, then choose Check.'}
 }
 if($script:task -and $script:async.IsCompleted){
  $succeeded=$false
  try{
   $result=@($script:task.EndInvoke($script:async));if($script:task.HadErrors){throw ($script:task.Streams.Error | Out-String)}
   if($result.Count -ne 1){throw 'The operation returned an incomplete result.'}
   switch($script:operation){
    Check{$script:offer=$result[0];$ui.Status.Text=$script:offer.Message;$ui.Latest.Text=if($script:offer.PSObject.Properties['Tag']){"Latest: $($script:offer.Tag)"}else{'Latest: unavailable'}}
    Download{$script:prepared=$result[0];$ui.Status.Text='Archive and payload verified. Close Client, Probe, Monitor and Setup, then choose Install.'}
    default{$ui.Status.Text=$result[0].Message;$script:prepared=$script:offer=$null;Refresh-LocalState}
   }
   $succeeded=$true
  }catch{$problem=$_.Exception.Message;if($script:operation -in @('Install','Rollback')){Refresh-LocalState};$ui.Status.Text=$problem}
  finally{$script:task.Dispose();$script:task=$script:async=$null;Refresh-UpdateButtons}
  if($script:closing){$window.Close()}
  elseif($script:autoStage -in @('Check','Download','Install')){Step-AutoInstall $succeeded}
 }
})
$ui.Check.Add_Click({Refresh-LocalState;Start-UpdateTask 'Check'})
$ui.Download.Add_Click({Start-UpdateTask 'Download'})
$ui.Install.Add_Click({
 $allow=$script:offer -and $script:offer.State -eq 'Preview'
 if($allow -and [Windows.MessageBox]::Show("Installed '$script:current' cannot be compared. Explicitly install stable '$($script:offer.Tag)'?",'Install stable release','YesNo','Question') -ne 'Yes'){return}
 Start-UpdateTask 'Install' ([bool]$allow)
})
$ui.Rollback.Add_Click({
 if([Windows.MessageBox]::Show("Restore local previous tools '$($script:previous.Version)'? Current settings will be retained.",'Rollback Windows tools','YesNo','Question') -eq 'Yes'){Start-UpdateTask 'Rollback'}
})
$ui.Close.Add_Click({$window.Close()})
$window.Add_Closing({param($sender,$event)
 if($script:task){
  $script:closing=$true;$event.Cancel=$true
  if($script:operation -in @('Install','Rollback')){$ui.Status.Text='Finishing the file transaction before closing...'}
  else{$ui.Status.Text='Cancelling network operation...';$null=$script:task.BeginStop($null,$null)}
 }
})
Refresh-LocalState;Refresh-ProcessState;Refresh-UpdateButtons
if($ValidateOnly){
 if($RenderPath){
  $surface=$window.Content;$surface.Measure((New-Object Windows.Size(712,420)));$surface.Arrange((New-Object Windows.Rect(0,0,712,420)));$surface.UpdateLayout()
  $bitmap=New-Object Windows.Media.Imaging.RenderTargetBitmap(712,420,96,96,[Windows.Media.PixelFormats]::Pbgra32);$bitmap.Render($surface)
  $encoder=New-Object Windows.Media.Imaging.PngBitmapEncoder;$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
  $stream=[IO.File]::Create($RenderPath);try{$encoder.Save($stream)}finally{$stream.Dispose()}
 }
 Write-Output "Updater loaded: $($ui.Count) controls; Check/Download/Install/Rollback; no network or install";$window.Close();exit 0
}
if($AutoInstall){$ui.Status.Text='Update now: waiting for HIDway to close. Check, Download and Install then run by themselves.';Refresh-UpdateButtons}
if($hookCode -and $AutoInstall){
 # Tests: the same timer and tasks without showing the window, then one result line.
 $script:frame=New-Object Windows.Threading.DispatcherFrame
 $guard=New-Object Windows.Threading.DispatcherTimer;$guard.Interval=[TimeSpan]::FromSeconds(150);$guard.Add_Tick({$script:autoResult='Timeout';$script:frame.Continue=$false})
 $timer.Start();$guard.Start();[Windows.Threading.Dispatcher]::PushFrame($script:frame);$timer.Stop();$guard.Stop()
 $status=$ui.Status.Text -replace '[\r\n]+',' '
 [Console]::Out.Write("HIDWAY-AUTOINSTALL result=$($script:autoResult) stage=$($script:autoStage) status=$status`r`n");[Console]::Out.Flush();exit 0
}
$owned=$false;$mutex=New-Object Threading.Mutex($false,'Local\HIDwayUpdateOwner')
try{
 try{$owned=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$owned=$true}
 if(!$owned){exit 0}
 $timer.Start();$null=$window.ShowDialog()
}finally{$timer.Stop();if($owned){$mutex.ReleaseMutex()};$mutex.Dispose()}
