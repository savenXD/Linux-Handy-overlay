param([int]$Seconds = 120)
$ErrorActionPreference = 'Stop'
$OutputPath = Join-Path $PSScriptRoot ('capture-state-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.jsonl')
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class CTWindowProbe {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr param);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
  [DllImport("user32.dll", SetLastError=true)] public static extern bool GetWindowDisplayAffinity(IntPtr hwnd, out uint affinity);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hwnd, int index);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  public static IntPtr[] Windows() {
    var result = new List<IntPtr>();
    EnumWindows((hwnd, param) => { result.Add(hwnd); return true; }, IntPtr.Zero);
    return result.ToArray();
  }
}
'@
function Write-Record($Record) {
  $Record | ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $OutputPath -Encoding UTF8
}
Write-Record @{type='start'; utc=[DateTime]::UtcNow.ToString('o'); os=[Environment]::OSVersion.VersionString; seconds=$Seconds; purpose='Read-only window metadata. No screenshots or page contents.'}
Write-Host "Recording for $Seconds seconds. Open CodeTantra, enter the course, then exit normally."
Write-Host "Output: $OutputPath"
$Deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
while ([DateTime]::UtcNow -lt $Deadline) {
  $TargetIds = @(Get-Process -Name 'CodeTantra-SEA' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
  $Foreground = [CTWindowProbe]::GetForegroundWindow()
  $Rows = @()
  foreach ($Window in [CTWindowProbe]::Windows()) {
    [uint32]$OwnerId = 0
    $null = [CTWindowProbe]::GetWindowThreadProcessId($Window, [ref]$OwnerId)
    if ($TargetIds -notcontains [int]$OwnerId) { continue }
    [uint32]$Affinity = 0
    $Success = [CTWindowProbe]::GetWindowDisplayAffinity($Window, [ref]$Affinity)
    $AffinityError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    $Rows += @{
      pid=$OwnerId; hwnd=('0x{0:X}' -f $Window.ToInt64())
      visible=[CTWindowProbe]::IsWindowVisible($Window)
      minimized=[CTWindowProbe]::IsIconic($Window)
      topmost=(([CTWindowProbe]::GetWindowLong($Window,-20) -band 8) -ne 0)
      foreground=($Window -eq $Foreground)
      affinityReadSucceeded=$Success
      affinity=$(if ($Success) { '0x{0:X}' -f $Affinity } else { $null })
      affinityReadError=$(if ($Success) { $null } else { $AffinityError })
    }
  }
  Write-Record @{type='sample'; utc=[DateTime]::UtcNow.ToString('o'); processIds=$TargetIds; windows=$Rows}
  Start-Sleep -Milliseconds 1000
}
Write-Record @{type='end'; utc=[DateTime]::UtcNow.ToString('o')}
Write-Host "Finished: $OutputPath"
