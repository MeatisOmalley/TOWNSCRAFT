# Save a PNG of a process's main window (used to record emulator screenshots).
# Usage: powershell -File tools/capture_window.ps1 -ProcessId <pid> -Out <file.png>
param([Parameter(Mandatory)][int]$ProcessId, [Parameter(Mandatory)][string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
}
'@
[Win]::SetProcessDPIAware() | Out-Null
$h = (Get-Process -Id $ProcessId).MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "process $ProcessId has no main window" }
[Win]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 300
$r = New-Object Win+RECT
# Visible frame only: GetWindowRect includes invisible resize borders, which
# would capture whatever is behind the window.
if ([Win]::DwmGetWindowAttribute($h, 9, [ref]$r, 16) -ne 0) {  # DWMWA_EXTENDED_FRAME_BOUNDS
    [Win]::GetWindowRect($h, [ref]$r) | Out-Null
}
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out ($($r.R - $r.L)x$($r.B - $r.T))"
