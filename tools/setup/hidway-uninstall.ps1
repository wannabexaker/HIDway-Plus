Add-Type -AssemblyName PresentationFramework
. (Join-Path $PSScriptRoot 'hidway-setup-core.ps1')
try {
    if([Windows.MessageBox]::Show('Remove HIDway and Input Monitor? Local settings, labels and recordings will be retained. Close both apps first.','Uninstall HIDway','OKCancel','Question') -ne 'OK'){exit}
    Uninstall-HidwayPackage $PSScriptRoot
    [Windows.MessageBox]::Show('Application files removed. Your settings and recordings were retained.','HIDway') | Out-Null
} catch { [Windows.MessageBox]::Show($_.Exception.Message,'HIDway uninstall','OK','Error') | Out-Null;exit 1 }