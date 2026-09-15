# Capture the port's window to a PNG.
#
# The framebuffer dump in src/rt64_context.cpp says what the game DREW; this
# says what the player SEES, and the two are not the same question. The
# Controller Pak prompt was drawn by the CPU straight into RDRAM with no display
# list at all, so the dump showed a finished picture while the window showed
# nothing -- a difference no counter in the port can express.
#
# Select by process id, not by title. Matching on a title fragment picked the
# terminal this session runs in, because its own title contains the project
# name -- so the "screenshot of the game" was a screenshot of the transcript.
# A window belonging to the process under test cannot be confused with anything
# else.
#
# Usage:  powershell -File tools/grab_window.ps1 out.png <process-id>
#         powershell -File tools/grab_window.ps1 out.png -Match "fragment"
param(
    [string]$Out = "window.png",
    [int]$ProcId = 0,
    [string]$Match = ""
)


# Become DPI-aware before touching any window or bitmap.
#
# Windows lies to processes that are not. The display here runs at 125%, so an
# unaware capture reads the window's client rect in virtual coordinates (800x600)
# and then grabs that many virtual pixels off a screen it also sees scaled -- the
# two scalings do not cancel, and the result is the top-left ~80% of the window
# saved at the right size. That looked exactly like a UI laid out too large for
# its window, and sent a perfectly good frontend to be debugged. Ask for
# per-monitor v2 first and fall back to the older system-wide call.
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Dpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
try { if (-not [Dpi]::SetProcessDpiAwarenessContext([IntPtr](-4))) { [void][Dpi]::SetProcessDPIAware() } }
catch { try { [void][Dpi]::SetProcessDPIAware() } catch {} }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    public struct RECT { public int L, T, R, B; }
    public struct POINT { public int X, Y; }
}
"@

$found = [IntPtr]::Zero
$title = ""
$cb = [Win+EnumProc]{
    param($h, $p)
    if ([Win]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 512
        [void][Win]::GetWindowText($h, $sb, 512)
        $t = $sb.ToString()
        if ($t.Length -eq 0) { return $true }
        if ($ProcId -ne 0) {
            $wpid = 0
            [void][Win]::GetWindowThreadProcessId($h, [ref]$wpid)
            if ($wpid -eq $ProcId) { $script:found = $h; $script:title = $t; return $false }
        }
        elseif ($Match -ne "" -and $t -like "*$Match*") {
            $script:found = $h; $script:title = $t; return $false
        }
    }
    return $true
}
[void][Win]::EnumWindows($cb, [IntPtr]::Zero)

if ($found -eq [IntPtr]::Zero) {
    if ($ProcId -ne 0) { Write-Output "no visible window for process $ProcId" }
    else { Write-Output "no window matching '$Match'" }
    exit 1
}

$r = New-Object Win+RECT
[void][Win]::GetClientRect($found, [ref]$r)
$tl = New-Object Win+POINT
[void][Win]::ClientToScreen($found, [ref]$tl)
$w = $r.R - $r.L; $h = $r.B - $r.T
if ($w -le 0 -or $h -le 0) { Write-Output "window has no client area"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($tl.X, $tl.Y, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "captured '$title'  ${w}x${h} -> $Out"
