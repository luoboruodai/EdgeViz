param([Parameter(Mandatory=$true)][string]$PluginPath)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class CharWeaveLoader {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern IntPtr LoadLibrary(string path);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool FreeLibrary(IntPtr handle);
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Ansi)]
  public static extern IntPtr GetProcAddress(IntPtr handle, string name);
  public static string Error() { return new Win32Exception(Marshal.GetLastWin32Error()).Message; }
}
'@
$path=(Resolve-Path -LiteralPath $PluginPath).ProviderPath
$h=[CharWeaveLoader]::LoadLibrary($path)
if($h -eq [IntPtr]::Zero){throw "LoadLibrary failed for $path : $([CharWeaveLoader]::Error())"}
try {
  $effect=[CharWeaveLoader]::GetProcAddress($h,'EffectMain')
  if($effect -eq [IntPtr]::Zero){throw "EffectMain export missing: $([CharWeaveLoader]::Error())"}
  Write-Host "PASS: LoadLibrary + EffectMain export resolved"
  Write-Host "Path: $path"
  Write-Host ("Module handle: 0x{0:X}" -f $h.ToInt64())
} finally {[void][CharWeaveLoader]::FreeLibrary($h)}
