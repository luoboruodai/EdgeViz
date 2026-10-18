param([string]$OutputPath = $(Join-Path $PWD 'CharWeave-diagnostics.txt'))
$ErrorActionPreference='Continue'
$lines=New-Object System.Collections.Generic.List[string]
function Add([string]$s){[void]$lines.Add($s)}
Add ('Collected: '+(Get-Date -Format o))
Add ('Computer: '+$env:COMPUTERNAME)
Add ('OS: '+(Get-CimInstance Win32_OperatingSystem).Caption+' '+(Get-CimInstance Win32_OperatingSystem).Version)
Add '--- After Effects installations ---'
$adobe=Join-Path $env:ProgramFiles 'Adobe'
Get-ChildItem -LiteralPath $adobe -Directory -ErrorAction SilentlyContinue | Where-Object {$_.Name -match '^Adobe After Effects 20(23|24|25|26)$'} | ForEach-Object {
  $exe=Join-Path $_.FullName 'Support Files\AfterFX.exe'
  if(Test-Path -LiteralPath $exe){$v=(Get-Item $exe).VersionInfo;Add ("AE: {0} | FileVersion={1} | SHA256={2}" -f $_.FullName,$v.FileVersion,(Get-FileHash $exe).Hash)}
}
Add '--- CharWeave plugin copies ---'
$roots=New-Object System.Collections.Generic.List[string]
Get-ChildItem -LiteralPath $adobe -Directory -ErrorAction SilentlyContinue | Where-Object {$_.Name -match '^Adobe After Effects 20(23|24|25|26)$'} | ForEach-Object {[void]$roots.Add((Join-Path $_.FullName 'Support Files\Plug-ins'))}
$common=Join-Path ${env:CommonProgramFiles} 'Adobe\Plug-Ins\7.0\MediaCore';if(Test-Path $common){[void]$roots.Add($common)}
foreach($r in $roots){Get-ChildItem -LiteralPath $r -Filter 'CharWeave*.aex' -Recurse -ErrorAction SilentlyContinue | ForEach-Object {Add ("Plugin: {0} | Size={1} | SHA256={2}" -f $_.FullName,$_.Length,(Get-FileHash $_.FullName).Hash)}}
Add '--- Recent Application Error events mentioning AE/CharWeave ---'
Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=(Get-Date).AddDays(-14)} -ErrorAction SilentlyContinue | Where-Object {$_.Message -match 'AfterFX|After Effects|CharWeave'} | Select-Object -First 20 | ForEach-Object {Add ("Event {0} {1}: {2}" -f $_.TimeCreated,$_.ProviderName,($_.Message -replace "`r?`n",' '))}
$lines | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Wrote $OutputPath"
