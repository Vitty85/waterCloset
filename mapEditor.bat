@echo off
setlocal
SET PATH=%PATH%;C:\msys64\mingw64\bin
REM set "CARTELLA=%~dp0data\stages"
REM for /f "usebackq delims=" %%a in (`powershell -WindowStyle Hidden -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; $files=Get-ChildItem -LiteralPath '%CARTELLA%' -Filter '*.json' | Sort-Object Name; $aliases=@(); $nums=@(); foreach ($f in $files) { if ($f.BaseName -match '^0*(\d+)$') { $n=[int]$matches[1]; $aliases+=\"Stage $n\"; $nums+=$n } }; $names=$files | ForEach-Object {$_.Name}; $form=New-Object Windows.Forms.Form; $form.Text='Choose a Stage'; $form.Size=New-Object Drawing.Size(640,480); $form.StartPosition='CenterScreen'; $list=New-Object Windows.Forms.ListBox; $list.Dock='Fill'; $list.Items.AddRange($aliases); $form.Controls.Add($list); $ok=New-Object Windows.Forms.Button; $ok.Text='EDIT'; $ok.Dock='Bottom'; $ok.Add_Click({$form.Tag=$list.SelectedIndex; $form.Close()}); $form.Controls.Add($ok); $form.ShowDialog() | Out-Null; Write-Output $nums[$form.Tag]"`) do set "SCELTA=%%a"
start "" "mapEditor.exe" -stage %SCELTA%
exit
