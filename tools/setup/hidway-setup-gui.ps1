param([switch]$ValidateOnly,[string]$RenderDirectory,[switch]$SmokeTest)
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase,System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HidwaySetupChrome {
 [DllImport("dwmapi.dll")] public static extern int DwmSetWindowAttribute(IntPtr window,int attribute,ref int value,int size);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
}
'@
. (Join-Path $PSScriptRoot 'hidway-setup-core.ps1')
[xml]$xaml=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Title="HIDway Setup" Width="820" Height="720" MinWidth="700" MinHeight="620" Background="#1E1F22" Foreground="#ECEDEF" WindowStartupLocation="CenterScreen" FontFamily="Segoe UI" FontSize="14">
 <Window.Resources>
  <Style TargetType="ScrollBar"><Setter Property="Width" Value="12"/><Setter Property="Background" Value="#1E1F22"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="ScrollBar"><Track x:Name="PART_Track" IsDirectionReversed="True" Orientation="Vertical"><Track.DecreaseRepeatButton><RepeatButton Command="ScrollBar.PageUpCommand"><RepeatButton.Template><ControlTemplate TargetType="RepeatButton"><Border Background="#1E1F22"/></ControlTemplate></RepeatButton.Template></RepeatButton></Track.DecreaseRepeatButton><Track.Thumb><Thumb><Thumb.Template><ControlTemplate TargetType="Thumb"><Border Background="#44474D" CornerRadius="5" Margin="2"/></ControlTemplate></Thumb.Template></Thumb></Track.Thumb><Track.IncreaseRepeatButton><RepeatButton Command="ScrollBar.PageDownCommand"><RepeatButton.Template><ControlTemplate TargetType="RepeatButton"><Border Background="#1E1F22"/></ControlTemplate></RepeatButton.Template></RepeatButton></Track.IncreaseRepeatButton></Track></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="Button"><Setter Property="Background" Value="#303237"/><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="Padding" Value="16,9"/><Setter Property="Margin" Value="0,0,8,8"/><Setter Property="BorderBrush" Value="#44474D"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border x:Name="B" Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="1" CornerRadius="5" Padding="{TemplateBinding Padding}"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter Property="Background" Value="#3C3E44"/></Trigger><Trigger Property="IsKeyboardFocused" Value="True"><Setter TargetName="B" Property="BorderBrush" Value="#ECEDEF"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.45"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="TextBox"><Setter Property="Background" Value="#2B2D32"/><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="BorderBrush" Value="#3E4147"/><Setter Property="Padding" Value="7,6"/><Setter Property="Margin" Value="0,0,0,8"/><Setter Property="CaretBrush" Value="White"/></Style>
  <Style x:Key="Label" TargetType="TextBlock"><Setter Property="TextWrapping" Value="Wrap"/><Setter Property="Margin" Value="0,4,0,3"/></Style>
  <Style TargetType="TextBlock"><Setter Property="TextWrapping" Value="Wrap"/><Setter Property="Margin" Value="0,0,0,12"/></Style>
  <Style TargetType="CheckBox"><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="Margin" Value="0,8,0,12"/></Style>
  <Style TargetType="RadioButton"><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="Margin" Value="0,8,20,12"/></Style>
 </Window.Resources>
 <Grid Margin="28" Background="#1E1F22"><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <TextBlock Text="HIDway  /  Setup" FontSize="28" FontWeight="SemiBold"/>
  <TextBlock x:Name="Steps" Grid.Row="1" Foreground="#A8ABB2"/>
  <ScrollViewer Grid.Row="2" VerticalScrollBarVisibility="Auto"><Grid>
   <StackPanel x:Name="Welcome">
    <TextBlock Text="One package. Both Windows tools." FontSize="22"/>
    <TextBlock Text="This wizard installs HIDway and Input Monitor together, then guides you through the Raspberry Pi relay and Pico firmware. You can install the Windows tools on both PCs and complete hardware setup on the PC connected to the Pico."/>
    <TextBlock Text="Windows input → HIDway → private network → Raspberry Pi relay → UART → Pico → USB HID → target PC" Foreground="#82E68C"/>
    <TextBlock Text="Before you begin: Windows 10/11 x64, a Raspberry Pi with Raspberry Pi OS and SSH enabled, the correct Pico board, and the documented UART wiring. Tailscale, Moonlight and Sunshine are separate optional applications."/>
    <TextBox x:Name="License" AutomationProperties.Name="Licence text" IsReadOnly="True" Height="170" VerticalScrollBarVisibility="Auto" TextWrapping="Wrap"/>
    <CheckBox x:Name="Accept" Content="I accept the MIT license and want to continue."/>
   </StackPanel>
   <StackPanel x:Name="WindowsPage" Visibility="Collapsed">
    <TextBlock Text="Install on this Windows PC" FontSize="22"/>
    <TextBlock Text="HIDway and Input Monitor are always installed together. Existing HIDway settings, device labels and recordings are preserved. Close the installed apps before updating; this wizard will not terminate them."/>
    <TextBlock Text="Installation folder" Style="{StaticResource Label}"/><TextBox x:Name="Folder" AutomationProperties.Name="Installation folder"/>
    <WrapPanel><Button x:Name="Browse" Content="Choose folder…"/><CheckBox x:Name="Desktop" Content="Desktop shortcut"/></WrapPanel>
    <TextBlock Text="PC role (initializes a new configuration; existing settings are preserved):"/>
    <RadioButton x:Name="Player" GroupName="Role" Content="Remote PC — this laptop sends input to the Gaming PC" IsChecked="True"/>
    <RadioButton x:Name="Gaming" GroupName="Role" Content="Gaming PC — remote virtual input returns through the Pico"/>
    <TextBlock Text="A Start menu folder and an entry in Windows Installed apps will be created for this user. Administrator access is not required. No automatic Windows startup is enabled." Foreground="#A8ABB2"/>
   </StackPanel>
   <StackPanel x:Name="NetworkPage" Visibility="Collapsed">
    <TextBlock Text="Connect the computers and Raspberry Pi" FontSize="22"/>
    <CheckBox x:Name="SkipPi" Margin="0,0,0,10" Content="The relay is already configured, or I will set it up later (skips the Pi relay step)."/>
    <TextBlock Text="Put the sending PC and Pi on the same private LAN or Tailscale network. For remote display, use Moonlight/Sunshine or AnyDesk separately. HIDway does not transmit the screen."/>
    <WrapPanel><Button x:Name="Tailscale" Content="Tailscale download"/><Button x:Name="Moonlight" Content="Moonlight download"/><Button x:Name="Sunshine" Content="Sunshine download"/></WrapPanel>
    <Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="16"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
     <StackPanel Grid.Row="0" Grid.Column="0" VerticalAlignment="Bottom"><TextBlock Text="Pi SSH hostname or IPv4" Style="{StaticResource Label}"/><TextBox x:Name="PiHost" AutomationProperties.Name="Pi SSH hostname or IPv4"/></StackPanel>
     <StackPanel Grid.Row="0" Grid.Column="2" VerticalAlignment="Bottom"><TextBlock Text="Pi Linux username" Style="{StaticResource Label}"/><TextBox x:Name="PiUser" Text="pi" AutomationProperties.Name="Pi Linux username"/></StackPanel>
     <StackPanel Grid.Row="1" Grid.Column="0" VerticalAlignment="Bottom"><TextBlock Text="Pi relay IPv4 (LAN/Tailscale address to use as HIDway Target)" Style="{StaticResource Label}"/><TextBox x:Name="RelayIP" AutomationProperties.Name="Pi relay IPv4"/></StackPanel>
     <StackPanel Grid.Row="1" Grid.Column="2" VerticalAlignment="Bottom"><TextBlock x:Name="SenderLabel" Text="Remote PC IPv4 (the sender allowed by the relay)" Style="{StaticResource Label}"/><TextBox x:Name="Sender" AutomationProperties.Name="Sending PC IPv4"/></StackPanel>
    </Grid>
    <TextBlock Text="The next step uses Windows OpenSSH in a visible terminal for fingerprint, password and sudo prompts. Authentication details are not stored. On first use, verify the fingerprint on your Pi before accepting it."/>
   </StackPanel>
   <StackPanel x:Name="PiPage" Visibility="Collapsed">
    <TextBlock Text="Install the Raspberry Pi relay" FontSize="22"/>
    <TextBlock Text="Connect Pi UART TX/RX/GND to the Pico according to the wiring guide. Use the correct voltage and cross TX/RX. USB here means a USB-to-UART adapter on the Pi; the Pico HID USB goes to the target Windows PC."/>
    <WrapPanel><RadioButton x:Name="UartUsb" GroupName="Uart" Content="USB serial adapter" IsChecked="True"/><RadioButton x:Name="UartGpio" GroupName="Uart" Content="Pi GPIO UART"/></WrapPanel>
    <TextBlock Text="Pi serial device path (filled in for the choice above; Pi 4 GPIO UART3: /dev/hidway-serial)" Style="{StaticResource Label}"/><TextBox x:Name="Serial" Text="/dev/ttyUSB0" AutomationProperties.Name="Pi serial device path"/>
    <TextBlock Text="Install uses the latest published release. It sets the relay bind IPv4 and adds this sender while preserving Remote PC / Gaming PC peers already allowed (maximum eight). An existing relay keeps its serial device unless you type another path above. Configuration is backed up. The relay may restart. GPIO setup supports Pi 4/400/CM4; use a USB UART adapter on other models." Foreground="#D6A038"/>
    <WrapPanel><Button x:Name="Wiring" Content="Open wiring guide"/><Button x:Name="InstallPi" Content="Connect and install relay…"/></WrapPanel>
    <TextBlock x:Name="PiStatus" Text="No relay installation has run in this wizard."/>
   </StackPanel>
   <StackPanel x:Name="PicoPage" Visibility="Collapsed">
    <TextBlock Text="Install Pico firmware" FontSize="22"/>
    <TextBlock Text="Select the exact name printed on the board. Unplug only its USB, hold BOOTSEL while reconnecting it to this PC, then release when the BOOTSEL drive appears. Existing HID input will be interrupted while flashing."/>
    <WrapPanel><RadioButton x:Name="Pico" GroupName="Board" Content="Pico"/><RadioButton x:Name="PicoW" GroupName="Board" Content="Pico W"/><RadioButton x:Name="Pico2" GroupName="Board" Content="Pico 2"/><RadioButton x:Name="Pico2W" GroupName="Board" Content="Pico 2 W"/></WrapPanel>
    <TextBlock Style="{StaticResource Label}" Text="BOOTSEL drive (example: E:\) — only a matching RP2040/RP2350 drive is accepted"/><TextBox x:Name="Drive" AutomationProperties.Name="BOOTSEL drive"/>
    <WrapPanel><Button x:Name="Detect" Content="Detect BOOTSEL drive"/><Button x:Name="Flash" Content="Verify drive and copy firmware…"/></WrapPanel>
    <TextBlock x:Name="FlashStatus" Text="The drive disappears when the Pico reboots. A successful copy still needs a real HID input test."/>
    <CheckBox x:Name="SkipPico" Content="Pico is already flashed, connected to the other PC, or I will complete this later."/>
   </StackPanel>
   <StackPanel x:Name="FinishPage" Visibility="Collapsed">
    <TextBlock Text="Windows installation complete" FontSize="22"/>
    <TextBlock x:Name="Summary"/>
    <TextBlock Text="1. Open HIDway and set Target to the Pi LAN/Tailscale IPv4, UDP port 47800 (unless changed on the relay). Save. Configure matching encryption keys if your relay requires them."/>
    <TextBlock Text="2. In Input options select Remote PC or Gaming PC. Select physical, touchpad and/or virtual sources as needed. Gaming PC capture accepts virtual input and excludes the returned Pico input."/>
    <TextBlock Text="3. Put Input Monitor in front on the target PC. ARM from the sending PC. Check keys, Left/Right/Middle/X1/X2 and wheel rotation separately. PANIC releases the target."/>
    <TextBlock Text="4. For repeatable tests, record a short sequence in Monitor, Stop/Save, then Replay in HIDway. Replay requires confirmation and starts after 3 seconds. Keep the target test window ready."/>
    <WrapPanel><Button x:Name="Launch" Content="Open HIDway"/><Button x:Name="Monitor" Content="Open Input Monitor"/><Button x:Name="Guide" Content="Open complete guide"/></WrapPanel>
   </StackPanel>
  </Grid></ScrollViewer>
  <TextBlock x:Name="Status" Grid.Row="3" Foreground="#D6A038" Margin="0,16,0,12"/>
  <DockPanel Grid.Row="4"><StackPanel Orientation="Horizontal" DockPanel.Dock="Left"><Button x:Name="Cancel" Content="Close"/><Button x:Name="Updates" Content="Check for updates"/></StackPanel><StackPanel Orientation="Horizontal" HorizontalAlignment="Right"><Button x:Name="Back" Content="Back"/><Button x:Name="Next" Content="Next" Background="#5F636B"/></StackPanel></DockPanel>
 </Grid>
</Window>
'@
$window=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $xaml))
# WPF sizes are device-independent. Fit the actual work area so footer controls
# stay reachable on a scaled laptop display instead of falling below the screen.
$setupWorkArea=[Windows.SystemParameters]::WorkArea
$setupWidth=[Math]::Max(320,$setupWorkArea.Width-24)
$setupHeight=[Math]::Max(240,$setupWorkArea.Height-24)
$window.MinWidth=[Math]::Min(700,$setupWidth);$window.MinHeight=[Math]::Min(620,$setupHeight)
$window.Width=[Math]::Min(820,$setupWidth);$window.Height=[Math]::Min(720,$setupHeight)
$iconFile=Join-Path $PSScriptRoot 'hidway-client.exe'
if(Test-Path -LiteralPath $iconFile){$icon=[Drawing.Icon]::ExtractAssociatedIcon($iconFile);if($icon){try{$window.Icon=[Windows.Interop.Imaging]::CreateBitmapSourceFromHIcon($icon.Handle,[Windows.Int32Rect]::Empty,[Windows.Media.Imaging.BitmapSizeOptions]::FromEmptyOptions())}finally{$icon.Dispose()}}}
$window.Add_SourceInitialized({$handle=(New-Object Windows.Interop.WindowInteropHelper $window).Handle;$dark=1;$color=0x221F1E;$null=[HidwaySetupChrome]::DwmSetWindowAttribute($handle,20,[ref]$dark,4);$null=[HidwaySetupChrome]::DwmSetWindowAttribute($handle,35,[ref]$color,4)})
$ui=@{}
foreach ($element in $xaml.SelectNodes('//*')) { $name=$element.GetAttribute('Name','http://schemas.microsoft.com/winfx/2006/xaml');if($name){$found=$window.FindName($name);if($found){$ui[$name]=$found}} }
$ui.Folder.Text=Join-Path $env:LOCALAPPDATA 'Programs\HIDway'
$existingFolder=Get-HidwayInstallationFolder
if($existingFolder){
    $ui.Folder.Text=$existingFolder
    $existingIni=Join-Path $existingFolder 'hidway.ini'
    if((Test-Path -LiteralPath $existingIni) -and [IO.File]::ReadAllText($existingIni) -match '(?m)^loopback_mode\s*=\s*1\s*$'){$ui.Gaming.IsChecked=$true}
}
$ui.License.Text=if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'LICENSE')){[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'LICENSE'))}else{'MIT License — see LICENSE in the extracted release package.'}
$script:step=0;$script:installed='';$script:firstInstall=$false;$script:piOK=$false;$script:flashed=$false;$script:piProcess=$null;$script:result='';$script:serialTyped=$false;$script:serialFilling=$false;$script:tailscaleTried=$false;$script:packageProblem=''
try{$script:packageProblem=Get-HidwayOneDriveProblem $PSScriptRoot}catch{}
$pages=@('Welcome','WindowsPage','NetworkPage','PiPage','PicoPage','FinishPage')
$brushes=@{};foreach($k in @(@('error','#FF7B72'),@('success','#82E68C'),@('note','#D6A038'))){$brushes[$k[0]]=New-Object Windows.Media.SolidColorBrush ([Windows.Media.Color][Windows.Media.ColorConverter]::ConvertFromString($k[1]))}
function Set-Status([string]$Text,[string]$Kind='note') { $ui.Status.Text=$Text;$ui.Status.Foreground=$brushes[$Kind] }
function Test-InstalledHere { Test-HidwayInstalledHere $ui.Folder.Text $PSScriptRoot (Get-HidwayInstallationFolder) }
function Update-NextLabel {
    $label='Next'
    if($script:step -eq 1){$label='Install and Next';try{if(Test-InstalledHere){$label='Next'}elseif(Test-Path -LiteralPath (Join-Path $ui.Folder.Text 'hidway-client.exe')){$label='Update and Next'}}catch{}}
    elseif($script:step -eq 5){$label='Finish'}
    $ui.Next.Content=$label
}
function Update-Status {
    # The message that is known without clicking anything: OneDrive package, unusable folder, tools already here.
    if($script:packageProblem){Set-Status $script:packageProblem 'error';return}
    if($script:step -eq 1 -and $ui.Folder.Text.Trim()){
        $problem='';try{$null=Assert-HidwayInstallPath $ui.Folder.Text}catch{$problem=$_.Exception.Message}
        if($problem){Set-Status $problem 'error';return}
        if(Test-InstalledHere){Set-Status 'HIDway and Input Monitor are already installed in this folder. Next continues with the Pi relay and Pico steps without installing again.' 'note';return}
    }
    Set-Status ''
}
function Find-BootselDrive([switch]$Auto) {
    $found=@();try{$found=@(Get-HidwayBootselDrives)}catch{}
    if($found.Count -eq 1){
        if(-not $ui.Drive.Text.Trim()){$ui.Drive.Text=$found[0];$ui.FlashStatus.Text="Found a BOOTSEL drive: $($found[0]) (filled in above). Choose the board model, then copy the firmware."}
        else{$ui.FlashStatus.Text="Found a BOOTSEL drive: $($found[0]). The drive box already has text, so it was not changed."}
    }elseif($found.Count -gt 1){$ui.FlashStatus.Text="Several BOOTSEL drives found: $($found -join ', '). Type the one that belongs to this Pico."}
    elseif(-not $Auto){$ui.FlashStatus.Text='No BOOTSEL drive found. Unplug the Pico, hold BOOTSEL while reconnecting it, then press Detect again.'}
}
function Set-SerialDefault {
    if($script:serialTyped -and $ui.Serial.Text.Trim()){return}
    $script:serialFilling=$true
    try{$ui.Serial.Text=Get-HidwayDefaultSerial $(if($ui.UartGpio.IsChecked){'gpio'}else{'usb'})}finally{$script:serialFilling=$false}
    $script:serialTyped=$false
}
function Open-Document([string]$Path) {
    if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "Not found in this folder: $Path"}
    if(Test-HidwayFileAssociation ([IO.Path]::GetExtension($Path))){Start-Process -FilePath $Path}
    else{Start-Process -FilePath (Join-Path $env:WINDIR 'System32\notepad.exe') -ArgumentList ('"'+$Path+'"')}
}
function Show-Page {
    for($i=0;$i -lt $pages.Count;$i++){$ui[$pages[$i]].Visibility=if($i -eq $script:step){'Visible'}else{'Collapsed'}}
    $ui.Steps.Text="Step $($script:step+1) of 6  •  License → Windows → Network → Pi relay → Pico → Test"
    $ui.Back.IsEnabled=$script:step -gt 0 -and -not $script:piProcess
    Update-NextLabel
    Update-Status
    $ui.SenderLabel.Text=if($ui.Gaming.IsChecked){'Gaming PC IPv4 (this PC sends virtual input through the relay)'}else{'Remote PC IPv4 (sending laptop / PC allowed by the relay)'}
    if($script:step -eq 2 -and -not $script:tailscaleTried -and -not $ValidateOnly){ # renders/validation never start another program
        $script:tailscaleTried=$true
        if(-not $ui.Sender.Text.Trim()){$ip=Get-HidwayTailscaleIPv4 (Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe') 1500;if($ip){$ui.Sender.Text=$ip}}
    }
    if($script:step -eq 4){Find-BootselDrive -Auto}
}
function Invoke-UiAction([scriptblock]$Action) { try { & $Action } catch { Set-Status $_.Exception.Message 'error' } }
function Open-Installed([string]$Name) {
    if(-not $script:installed){throw 'Complete Windows installation first.'}
    $processName=[IO.Path]::GetFileNameWithoutExtension($Name)
    foreach($process in @(Get-Process -Name $processName -ErrorAction SilentlyContinue)){if($process.Path -and [IO.Path]::GetDirectoryName($process.Path) -ne $script:installed){throw 'A previous copy is running from another folder. Close it before opening the installed version; the current session was left untouched.'}}
    Start-Process -FilePath (Join-Path $script:installed $Name) -WorkingDirectory $script:installed
}
$ui.Browse.Add_Click({$dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.SelectedPath=$ui.Folder.Text;if($dialog.ShowDialog() -eq 'OK'){$ui.Folder.Text=$dialog.SelectedPath};$dialog.Dispose()})
$ui.Cancel.Add_Click({$window.Close()})
$ui.Updates.Add_Click({Invoke-UiAction {
    if($script:piProcess){throw 'Finish the SSH setup terminal before checking updates.'}
    $launcher=Join-Path $PSScriptRoot 'HIDway-Setup.exe'
    Start-Process -FilePath $launcher -ArgumentList '--updates' -WorkingDirectory (Split-Path $launcher)
    $window.Close()
}})
$ui.Back.Add_Click({if($script:step -gt 0){$script:step--;if($script:step -eq 3 -and $ui.SkipPi.IsChecked){$script:step--};Show-Page}})
$ui.Next.Add_Click({Invoke-UiAction {
    if($script:piProcess){throw 'Finish the SSH installation terminal first.'}
    switch($script:step){
        0 {if(-not $ui.Accept.IsChecked){throw 'Accept the license to continue.'}}
        1 {
            if(Test-InstalledHere){$script:installed=Assert-HidwayInstallPath $PSScriptRoot} # started from the installed folder: nothing to install
            else{
                if(-not $script:firstInstall){$script:firstInstall=-not(Test-Path -LiteralPath (Join-Path $ui.Folder.Text 'hidway.ini'))} # stays true on Back/Next
                $script:installed=Install-HidwayPackage $PSScriptRoot $ui.Folder.Text -Desktop:([bool]$ui.Desktop.IsChecked)
            }
        }
        2 {if(-not $ui.SkipPi.IsChecked){$null=Get-HidwaySshTarget $ui.PiHost.Text $ui.PiUser.Text;if(-not(Test-HidwayIPv4 $ui.Sender.Text) -or -not(Test-HidwayIPv4 $ui.RelayIP.Text)){throw 'Enter both the Pi relay and sending Windows PC IPv4.'}}}
        3 {if(-not $ui.SkipPi.IsChecked -and -not $script:piOK){throw 'Complete relay installation, or go Back and choose setup later.'}}
        4 {
            if(-not $ui.SkipPico.IsChecked -and -not $script:flashed){throw 'Flash the Pico, or select already flashed / later.'}
            if($script:firstInstall){
                $null=Initialize-HidwayConfig $script:installed $ui.RelayIP.Text ([bool]$ui.Gaming.IsChecked)
            }
        }
        5 {$window.Close();return}
    }
    $script:step++;if($script:step -eq 3 -and $ui.SkipPi.IsChecked){$script:step++};$ui.Summary.Text="Installed: $script:installed`nPi relay: $(if($script:piOK){'service active'}else{'not verified in this wizard'})`nPico: $(if($script:flashed){'firmware copied; real input test required'}else{'not flashed in this wizard'})`nRoute to select: $(if($ui.Gaming.IsChecked){'Gaming PC'}else{'Remote PC'})"
    Show-Page
}})
$ui.InstallPi.Add_Click({Invoke-UiAction {
    if($script:piProcess){throw 'A relay installer is already open.'}
    $null=Get-HidwaySshTarget $ui.PiHost.Text $ui.PiUser.Text
    if(-not(Test-HidwayIPv4 $ui.Sender.Text) -or -not(Test-HidwayIPv4 $ui.RelayIP.Text)){throw 'Enter the Pi relay and sending PC IPv4 on the Network step.'}
    if($ui.Serial.Text -notmatch '^/dev/[a-zA-Z0-9_./-]+\z' -or $ui.Serial.Text.Contains('..')){throw 'Enter a valid Pi serial device.'}
    $logs=Join-Path $env:LOCALAPPDATA 'HIDway\Setup';New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $script:result=Join-Path $logs ('pi-'+[guid]::NewGuid().ToString('N')+'.result')
    $uart=if($ui.UartGpio.IsChecked){'gpio'}else{'usb'}
    $ps=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $PSScriptRoot 'hidway-pi-setup.ps1')+'" -PiHost '+$ui.PiHost.Text+' -PiUser '+$ui.PiUser.Text+' -RelayIP '+$ui.RelayIP.Text+' -SenderIP '+$ui.Sender.Text+' -Uart '+$uart+$(if($script:serialTyped){' -Serial '}else{' -SerialDefault '})+$ui.Serial.Text+' -ResultFile "'+$script:result+'"'
    $script:piProcess=Start-Process -FilePath $ps -ArgumentList $arguments -WindowStyle Normal -PassThru
    $ui.PiStatus.Text='Complete SSH fingerprint verification, Linux login and sudo in the visible terminal. Read any errors before closing it.'
    $ui.InstallPi.IsEnabled=$false;$ui.Back.IsEnabled=$false;$ui.Next.IsEnabled=$false
}})
$timer=New-Object Windows.Threading.DispatcherTimer;$timer.Interval=[TimeSpan]::FromMilliseconds(500)
$timer.Add_Tick({if($script:piProcess -and $script:piProcess.HasExited){$script:piOK=(Test-Path -LiteralPath $script:result) -and [IO.File]::ReadAllText($script:result).Trim() -eq '0';$script:piProcess.Dispose();$script:piProcess=$null;$ui.InstallPi.IsEnabled=$true;$ui.Back.IsEnabled=$true;$ui.Next.IsEnabled=$true;$ui.PiStatus.Text=if($script:piOK){'Relay service is active. Continue to Pico.'}else{'Relay setup did not finish successfully. Retry after correcting the terminal error.'};if($script:piOK){Set-Status 'Relay service is active.' 'success'}else{Set-Status 'Relay setup did not finish successfully.' 'error'}}})
$ui.Flash.Add_Click({Invoke-UiAction {
    $board=if($ui.Pico.IsChecked){'pico'}elseif($ui.PicoW.IsChecked){'pico_w'}elseif($ui.Pico2.IsChecked){'pico2'}elseif($ui.Pico2W.IsChecked){'pico2_w'}else{throw 'Choose the exact Pico model first.'}
    $null=Get-HidwayPayload $PSScriptRoot
    $file=Get-HidwayPicoFile $PSScriptRoot $board $ui.Drive.Text
    if([Windows.MessageBox]::Show("Copy $board firmware to $($ui.Drive.Text)? The Pico HID connection will restart.",'Flash Pico','OKCancel','Question') -ne 'OK'){return}
    Copy-Item -LiteralPath $file -Destination (Join-Path $ui.Drive.Text ([IO.Path]::GetFileName($file))) -ErrorAction Stop
    $script:flashed=$true;Set-Status 'Firmware copy completed.' 'success';$ui.FlashStatus.Text='Firmware copy completed. Wait for the Pico to reboot, then confirm both keyboard and mouse in Input Monitor.'
}})
foreach($entry in @(@('Tailscale','https://tailscale.com/download/windows'),@('Moonlight','https://moonlight-stream.org/'),@('Sunshine','https://docs.lizardbyte.dev/projects/sunshine/latest/'))){$url=$entry[1];$ui[$entry[0]].Add_Click({Start-Process $url}.GetNewClosure())}
$ui.Wiring.Add_Click({Invoke-UiAction {Open-Document (Join-Path $PSScriptRoot 'docs\RELAY.md')}})
$ui.Detect.Add_Click({Invoke-UiAction {Find-BootselDrive}})
$ui.Serial.Add_TextChanged({if(-not $script:serialFilling){$script:serialTyped=$true}})
$ui.UartUsb.Add_Checked({Set-SerialDefault});$ui.UartGpio.Add_Checked({Set-SerialDefault})
$ui.Folder.Add_TextChanged({try{Update-NextLabel;if($script:step -eq 1){Update-Status}}catch{}})
$ui.Guide.Add_Click({Invoke-UiAction {Open-Document (Join-Path $PSScriptRoot 'docs\INSTALLATION.md')}})
$ui.Launch.Add_Click({Invoke-UiAction {Open-Installed 'hidway-client.exe'}})
$ui.Monitor.Add_Click({Invoke-UiAction {Open-Installed 'hidway-input-monitor.exe'}})
$window.Add_Closing({param($sender,$event)if($script:piProcess){$event.Cancel=$true;Set-Status 'Finish/close the SSH terminal before closing this wizard.' 'note'}})
Show-Page
if($SmokeTest){
    $window.ShowActivated=$false;$window.Show();$window.UpdateLayout()
    $handle=(New-Object Windows.Interop.WindowInteropHelper $window).Handle
    $visible=$handle -ne [IntPtr]::Zero -and [HidwaySetupChrome]::IsWindowVisible($handle)
    $fits=$setupWorkArea.Height -le 0 -or ($window.ActualHeight -le $setupWorkArea.Height -and $window.ActualWidth -le $setupWorkArea.Width)
    $window.Close();if(-not $visible){throw 'The setup window was hidden; bootstrap visibility test failed.'}
    if(-not $fits){throw 'The setup window exceeds the desktop work area; navigation would be clipped.'}
    Write-Output 'Visible WPF setup window PASS; no installation performed';exit 0
}
if($ValidateOnly){
    if($RenderDirectory){
        New-Item -ItemType Directory -Path $RenderDirectory -Force | Out-Null
        for($page=0;$page -lt $pages.Count;$page++){
            $script:step=$page;Show-Page
            # Component rendering only: no desktop window or installation.
            $surface=$window.Content;$surface.Width=[double]::NaN;$surface.Height=[double]::NaN
            $surface.Measure((New-Object Windows.Size(764,630)));$surface.Arrange((New-Object Windows.Rect(0,0,764,630)));$surface.UpdateLayout()
            $bitmap=New-Object Windows.Media.Imaging.RenderTargetBitmap(764,630,96,96,[Windows.Media.PixelFormats]::Pbgra32);$bitmap.Render($surface)
            $encoder=New-Object Windows.Media.Imaging.PngBitmapEncoder;$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
            $stream=[IO.File]::Create((Join-Path $RenderDirectory ("setup-$page.png")));try{$encoder.Save($stream)}finally{$stream.Dispose()}
        }
    }
    Write-Output "WPF wizard loaded: $($ui.Count) named controls, $($pages.Count) pages";$window.Close();exit 0
}
$timer.Start();$null=$window.ShowDialog();$timer.Stop()
