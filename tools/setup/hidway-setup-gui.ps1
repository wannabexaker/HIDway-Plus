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
  <Style TargetType="Button"><Setter Property="Background" Value="#303237"/><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="Padding" Value="16,9"/><Setter Property="Margin" Value="0,0,8,8"/><Setter Property="BorderBrush" Value="#44474D"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}" BorderThickness="1" CornerRadius="5" Padding="{TemplateBinding Padding}"><ContentPresenter HorizontalAlignment="Center" VerticalAlignment="Center"/></Border><ControlTemplate.Triggers><Trigger Property="IsMouseOver" Value="True"><Setter Property="Background" Value="#3C3E44"/></Trigger><Trigger Property="IsEnabled" Value="False"><Setter Property="Opacity" Value="0.45"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>
  <Style TargetType="TextBox"><Setter Property="Background" Value="#2B2D32"/><Setter Property="Foreground" Value="#ECEDEF"/><Setter Property="BorderBrush" Value="#3E4147"/><Setter Property="Padding" Value="8"/><Setter Property="Margin" Value="0,5,0,12"/><Setter Property="CaretBrush" Value="White"/></Style>
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
    <TextBox x:Name="License" IsReadOnly="True" Height="170" VerticalScrollBarVisibility="Auto" TextWrapping="Wrap"/>
    <CheckBox x:Name="Accept" Content="I accept the MIT license and want to continue."/>
   </StackPanel>
   <StackPanel x:Name="WindowsPage" Visibility="Collapsed">
    <TextBlock Text="Install on this Windows PC" FontSize="22"/>
    <TextBlock Text="HIDway and Input Monitor are always installed together. Existing HIDway settings, device labels and recordings are preserved. Close the installed apps before updating; this wizard will not terminate them."/>
    <TextBlock Text="Installation folder"/><TextBox x:Name="Folder"/>
    <WrapPanel><Button x:Name="Browse" Content="Choose folder…"/><CheckBox x:Name="Desktop" Content="Desktop shortcut"/></WrapPanel>
    <TextBlock Text="PC role (initializes a new configuration; existing settings are preserved):"/>
    <RadioButton x:Name="Player" GroupName="Role" Content="Remote PC — this laptop sends input to the Gaming PC" IsChecked="True"/>
    <RadioButton x:Name="Gaming" GroupName="Role" Content="Gaming PC — remote virtual input returns through the Pico"/>
    <TextBlock Text="A Start menu folder and an entry in Windows Installed apps will be created for this user. Administrator access is not required. No automatic Windows startup is enabled." Foreground="#A8ABB2"/>
   </StackPanel>
   <StackPanel x:Name="NetworkPage" Visibility="Collapsed">
    <TextBlock Text="Connect the computers and Raspberry Pi" FontSize="22"/>
    <TextBlock Text="Put the sending PC and Pi on the same private LAN or Tailscale network. For remote display, use Moonlight/Sunshine or AnyDesk separately. HIDway does not transmit the screen."/>
    <WrapPanel><Button x:Name="Tailscale" Content="Tailscale download"/><Button x:Name="Moonlight" Content="Moonlight download"/><Button x:Name="Sunshine" Content="Sunshine download"/></WrapPanel>
    <TextBlock Text="Pi SSH hostname or IPv4"/><TextBox x:Name="PiHost"/>
    <TextBlock Text="Pi Linux username"/><TextBox x:Name="PiUser" Text="pi"/>
    <TextBlock Text="Pi relay IPv4 (LAN/Tailscale address to use as HIDway Target)"/><TextBox x:Name="RelayIP"/>
    <TextBlock x:Name="SenderLabel" Text="Remote PC IPv4 (the sender allowed by the relay)"/><TextBox x:Name="Sender"/>
    <TextBlock Text="The next step uses Windows OpenSSH in a visible terminal for fingerprint, password and sudo prompts. Authentication details are not stored. On first use, verify the fingerprint on your Pi before accepting it."/>
    <CheckBox x:Name="SkipPi" Content="The relay is already configured, or I will set it up later."/>
   </StackPanel>
   <StackPanel x:Name="PiPage" Visibility="Collapsed">
    <TextBlock Text="Install the Raspberry Pi relay" FontSize="22"/>
    <TextBlock Text="Connect Pi UART TX/RX/GND to the Pico according to the wiring guide. Use the correct voltage and cross TX/RX. USB here means a USB-to-UART adapter on the Pi; the Pico HID USB goes to the target Windows PC."/>
    <WrapPanel><RadioButton x:Name="UartUsb" GroupName="Uart" Content="USB serial adapter" IsChecked="True"/><RadioButton x:Name="UartGpio" GroupName="Uart" Content="Pi GPIO UART"/></WrapPanel>
    <TextBlock Text="Pi serial device path (Pi 4 GPIO UART3: /dev/ttyAMA3; USB adapter path varies)"/><TextBox x:Name="Serial" Text="/dev/ttyUSB0"/>
    <TextBlock Text="Install uses the repository main branch. It sets the relay bind IPv4 and adds this sender while preserving Remote PC / Gaming PC peers already allowed (maximum eight). Configuration is backed up. The relay may restart. GPIO setup supports Pi 4/400/CM4; use a USB UART adapter on other models." Foreground="#D6A038"/>
    <WrapPanel><Button x:Name="Wiring" Content="Open wiring guide"/><Button x:Name="InstallPi" Content="Connect and install relay…"/></WrapPanel>
    <TextBlock x:Name="PiStatus" Text="No relay installation has run in this wizard."/>
   </StackPanel>
   <StackPanel x:Name="PicoPage" Visibility="Collapsed">
    <TextBlock Text="Install Pico firmware" FontSize="22"/>
    <TextBlock Text="Select the exact name printed on the board. Unplug only its USB, hold BOOTSEL while reconnecting it to this PC, then release when the BOOTSEL drive appears. Existing HID input will be interrupted while flashing."/>
    <WrapPanel><RadioButton x:Name="Pico" GroupName="Board" Content="Pico"/><RadioButton x:Name="PicoW" GroupName="Board" Content="Pico W"/><RadioButton x:Name="Pico2" GroupName="Board" Content="Pico 2"/><RadioButton x:Name="Pico2W" GroupName="Board" Content="Pico 2 W"/></WrapPanel>
    <TextBlock Text="BOOTSEL drive (example: E:\) — only a matching RP2040/RP2350 drive is accepted"/><TextBox x:Name="Drive"/>
    <Button x:Name="Flash" Content="Verify drive and copy firmware…" HorizontalAlignment="Left"/>
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
  <DockPanel Grid.Row="4"><Button x:Name="Cancel" Content="Close" DockPanel.Dock="Left"/><StackPanel Orientation="Horizontal" HorizontalAlignment="Right"><Button x:Name="Back" Content="Back"/><Button x:Name="Next" Content="Next" Background="#5F636B"/></StackPanel></DockPanel>
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
$ui.License.Text=if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'LICENSE')){[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'LICENSE'))}else{'MIT License — see LICENSE in the extracted release package.'}
$script:step=0;$script:installed='';$script:firstInstall=$false;$script:piOK=$false;$script:flashed=$false;$script:piProcess=$null;$script:result=''
$pages=@('Welcome','WindowsPage','NetworkPage','PiPage','PicoPage','FinishPage')
function Show-Page {
    for($i=0;$i -lt $pages.Count;$i++){$ui[$pages[$i]].Visibility=if($i -eq $script:step){'Visible'}else{'Collapsed'}}
    $ui.Steps.Text="Step $($script:step+1) of 6  •  License → Windows → Network → Pi relay → Pico → Test"
    $ui.Back.IsEnabled=$script:step -gt 0 -and -not $script:piProcess
    $ui.Next.Content=if($script:step -eq 1){'Install and Next'}elseif($script:step -eq 5){'Finish'}else{'Next'}
    $ui.Status.Text=''
    $ui.SenderLabel.Text=if($ui.Gaming.IsChecked){'Gaming PC IPv4 (this PC sends virtual input through the relay)'}else{'Remote PC IPv4 (sending laptop / PC allowed by the relay)'}
}
function Invoke-UiAction([scriptblock]$Action) { try { & $Action } catch { $ui.Status.Text=$_.Exception.Message } }
function Open-Installed([string]$Name) {
    if(-not $script:installed){throw 'Complete Windows installation first.'}
    $processName=[IO.Path]::GetFileNameWithoutExtension($Name)
    foreach($process in @(Get-Process -Name $processName -ErrorAction SilentlyContinue)){if($process.Path -and [IO.Path]::GetDirectoryName($process.Path) -ne $script:installed){throw 'A previous copy is running from another folder. Close it before opening the installed version; the current session was left untouched.'}}
    Start-Process -FilePath (Join-Path $script:installed $Name) -WorkingDirectory $script:installed
}
$ui.Browse.Add_Click({$dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.SelectedPath=$ui.Folder.Text;if($dialog.ShowDialog() -eq 'OK'){$ui.Folder.Text=$dialog.SelectedPath};$dialog.Dispose()})
$ui.Cancel.Add_Click({$window.Close()})
$ui.Back.Add_Click({if($script:step -gt 0){$script:step--;Show-Page}})
$ui.Next.Add_Click({Invoke-UiAction {
    if($script:piProcess){throw 'Finish the SSH installation terminal first.'}
    switch($script:step){
        0 {if(-not $ui.Accept.IsChecked){throw 'Accept the license to continue.'}}
        1 {$script:firstInstall=-not(Test-Path -LiteralPath (Join-Path $ui.Folder.Text 'hidway.ini'));$script:installed=Install-HidwayPackage $PSScriptRoot $ui.Folder.Text -Desktop:([bool]$ui.Desktop.IsChecked)}
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
    $script:step++;$ui.Summary.Text="Installed: $script:installed`nPi relay: $(if($script:piOK){'service active'}else{'not verified in this wizard'})`nPico: $(if($script:flashed){'firmware copied; real input test required'}else{'not flashed in this wizard'})`nRoute to select: $(if($ui.Gaming.IsChecked){'Gaming PC'}else{'Remote PC'})"
    Show-Page
}})
$ui.InstallPi.Add_Click({Invoke-UiAction {
    if($script:piProcess){throw 'A relay installer is already open.'}
    $null=Get-HidwaySshTarget $ui.PiHost.Text $ui.PiUser.Text
    if(-not(Test-HidwayIPv4 $ui.Sender.Text) -or -not(Test-HidwayIPv4 $ui.RelayIP.Text)){throw 'Enter the Pi relay and sending PC IPv4 on the Network step.'}
    if($ui.Serial.Text -notmatch '^/dev/[a-zA-Z0-9_./-]+$' -or $ui.Serial.Text.Contains('..')){throw 'Enter a valid Pi serial device.'}
    $logs=Join-Path $env:LOCALAPPDATA 'HIDway\Setup';New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $script:result=Join-Path $logs ('pi-'+[guid]::NewGuid().ToString('N')+'.result')
    $uart=if($ui.UartGpio.IsChecked){'gpio'}else{'usb'}
    $ps=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arguments='-NoProfile -ExecutionPolicy Bypass -File "'+(Join-Path $PSScriptRoot 'hidway-pi-setup.ps1')+'" -PiHost '+$ui.PiHost.Text+' -PiUser '+$ui.PiUser.Text+' -RelayIP '+$ui.RelayIP.Text+' -SenderIP '+$ui.Sender.Text+' -Uart '+$uart+' -Serial '+$ui.Serial.Text+' -ResultFile "'+$script:result+'"'
    $script:piProcess=Start-Process -FilePath $ps -ArgumentList $arguments -WindowStyle Normal -PassThru
    $ui.PiStatus.Text='Complete SSH fingerprint verification, Linux login and sudo in the visible terminal. Read any errors before closing it.'
    $ui.InstallPi.IsEnabled=$false;$ui.Back.IsEnabled=$false;$ui.Next.IsEnabled=$false
}})
$timer=New-Object Windows.Threading.DispatcherTimer;$timer.Interval=[TimeSpan]::FromMilliseconds(500)
$timer.Add_Tick({if($script:piProcess -and $script:piProcess.HasExited){$script:piOK=(Test-Path -LiteralPath $script:result) -and [IO.File]::ReadAllText($script:result).Trim() -eq '0';$script:piProcess.Dispose();$script:piProcess=$null;$ui.InstallPi.IsEnabled=$true;$ui.Back.IsEnabled=$true;$ui.Next.IsEnabled=$true;$ui.PiStatus.Text=if($script:piOK){'Relay service is active. Continue to Pico.'}else{'Relay setup did not finish successfully. Retry after correcting the terminal error.'}}})
$ui.Flash.Add_Click({Invoke-UiAction {
    $board=if($ui.Pico.IsChecked){'pico'}elseif($ui.PicoW.IsChecked){'pico_w'}elseif($ui.Pico2.IsChecked){'pico2'}elseif($ui.Pico2W.IsChecked){'pico2_w'}else{throw 'Choose the exact Pico model first.'}
    $null=Get-HidwayPayload $PSScriptRoot
    $file=Get-HidwayPicoFile $PSScriptRoot $board $ui.Drive.Text
    if([Windows.MessageBox]::Show("Copy $board firmware to $($ui.Drive.Text)? The Pico HID connection will restart.",'Flash Pico','OKCancel','Question') -ne 'OK'){return}
    Copy-Item -LiteralPath $file -Destination (Join-Path $ui.Drive.Text ([IO.Path]::GetFileName($file))) -ErrorAction Stop
    $script:flashed=$true;$ui.FlashStatus.Text='Firmware copy completed. Wait for the Pico to reboot, then confirm both keyboard and mouse in Input Monitor.'
}})
foreach($entry in @(@('Tailscale','https://tailscale.com/download/windows'),@('Moonlight','https://moonlight-stream.org/'),@('Sunshine','https://docs.lizardbyte.dev/projects/sunshine/latest/'))){$url=$entry[1];$ui[$entry[0]].Add_Click({Start-Process $url}.GetNewClosure())}
$ui.Wiring.Add_Click({Invoke-UiAction {Start-Process (Join-Path $PSScriptRoot 'docs\RELAY.md')}})
$ui.Guide.Add_Click({Invoke-UiAction {Start-Process (Join-Path $PSScriptRoot 'docs\INSTALLATION.md')}})
$ui.Launch.Add_Click({Invoke-UiAction {Open-Installed 'hidway-client.exe'}})
$ui.Monitor.Add_Click({Invoke-UiAction {Open-Installed 'hidway-input-monitor.exe'}})
$window.Add_Closing({param($sender,$event)if($script:piProcess){$event.Cancel=$true;$ui.Status.Text='Finish/close the SSH terminal before closing this wizard.'}})
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
