param([string[]]$AEPath, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
# Source-derived Windows compatibility candidate. Target AE 2023-2026 x64 only.
$source = Join-Path $PSScriptRoot 'CharWeave.aex'
$expected = '44E07C55F7395869A562D672DD1ED688A79381AA0263CDF83FE8AEEA5861AC8D'
$years = @{23=2023;24=2024;25=2025;26=2026}
function Get-AETarget([string]$Path) {
  $root=(Resolve-Path -LiteralPath $Path).ProviderPath
  $support=Join-Path $root 'Support Files'
  if ((Split-Path $root -Leaf) -eq 'Support Files') {$support=$root}
  $exe=Join-Path $support 'AfterFX.exe'
  if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw "AfterFX.exe not found: $support" }
  $info=(Get-Item -LiteralPath $exe).VersionInfo
  if (!$years.ContainsKey($info.FileMajorPart)) { throw "Only AE 2023-2026 is accepted: $($info.FileVersion)" }
  $plugins=Join-Path $support 'Plug-ins'
  if (!(Test-Path -LiteralPath $plugins -PathType Container)) { throw "Plug-ins folder not found: $plugins" }
  [pscustomobject]@{Year=$years[$info.FileMajorPart];Version=$info.FileVersion;Plugins=$plugins;Target=(Join-Path $plugins 'CharWeave.aex')}
}
if (!(Test-Path -LiteralPath $source) -or (Get-FileHash -LiteralPath $source).Hash -ne $expected) { throw 'Candidate AEX hash mismatch.' }
if (!$AEPath) {
  $adobe=Join-Path $env:ProgramFiles 'Adobe'
  $candidates=@(Get-ChildItem -LiteralPath $adobe -Directory -ErrorAction SilentlyContinue | Where-Object {$_.Name -match '^Adobe After Effects 20(23|24|25|26)$'})
  if ($CheckOnly) {$AEPath=@($candidates.FullName)} else {
    for($i=0;$i -lt $candidates.Count;$i++){Write-Host "$($i+1): $($candidates[$i].FullName)"}
    $choice=Read-Host 'Choose AE 2023-2026, paste a custom AE folder, or blank to cancel'
    if ([string]::IsNullOrWhiteSpace($choice)){return}
    if($choice -match '^\d+$'){$index=[int]$choice-1;if($index -lt 0 -or $index -ge $candidates.Count){throw 'Invalid selection.'};$AEPath=@($candidates[$index].FullName)}else{$AEPath=@($choice.Trim('"'))}
  }
}
if (!$AEPath){Write-Host 'No AE path selected.';return}
$targets=@($AEPath|ForEach-Object {Get-AETarget $_}|Sort-Object Target -Unique)
foreach($t in $targets){
  $dupes=@(Get-ChildItem -LiteralPath $t.Plugins -Filter 'CharWeave*.aex' -Recurse | Where-Object {$_.FullName -ne $t.Target})
  $common=Join-Path ${env:CommonProgramFiles} 'Adobe\Plug-Ins\7.0\MediaCore'
  if(Test-Path -LiteralPath $common){$dupes+=@(Get-ChildItem -LiteralPath $common -Filter 'CharWeave*.aex' -Recurse)}
  if($dupes.Count){throw "Duplicate CharWeave copies found: $($dupes.FullName -join ', ')"}
  Write-Host "AE $($t.Year) ($($t.Version)): $($t.Target)"
}
if($CheckOnly){Write-Host 'CHECK PASS: no files changed; real AE launch testing is still required.';return}
if(Get-Process AfterFX,aerender -ErrorAction SilentlyContinue){throw 'Close AE and aerender first.'}
$id=[Security.Principal.WindowsIdentity]::GetCurrent();$pr=New-Object Security.Principal.WindowsPrincipal($id)
if(!$pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Run as administrator.'}
foreach($t in $targets){
  $backup=$null
  if(Test-Path -LiteralPath $t.Target){$dir=Join-Path $env:ProgramData ('CharWeave\Backups\'+[guid]::NewGuid().ToString());New-Item -ItemType Directory -Path $dir -Force|Out-Null;$backup=Join-Path $dir 'CharWeave.aex';Copy-Item $t.Target $backup;$t.Target|Set-Content (Join-Path $dir 'original-path.txt')}
  try{Copy-Item $source $t.Target -Force;if((Get-FileHash $t.Target).Hash -ne $expected){throw 'Installed hash mismatch.'}}catch{if($backup){Copy-Item $backup $t.Target -Force};throw}
  Write-Host "INSTALLED AND VERIFIED: AE $($t.Year)"
}
