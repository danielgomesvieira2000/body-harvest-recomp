#!/usr/bin/env python3
"""Save a window's own contents to a PNG, even when other windows cover it.

    python tools/print_window.py OUT.png PID

Why: `grab_window.ps1` copies the desktop where the window sits, so a terminal on
top is what gets saved; `capture_frames.py` (Windows Graphics Capture) delivered
no frames of this port's window on the development laptop. `PrintWindow` with
`PW_RENDERFULLCONTENT` (Windows 8.1+) asks the compositor for the window's
content, which includes DirectX swap chains and ignores what covers it.

Finds the largest visible top-level window of process PID. DPI-aware, so the
saved size is the window's real pixel size. Windows only; needs Pillow.
"""
import ctypes
import ctypes.wintypes as wt
import sys

from PIL import Image

PW_RENDERFULLCONTENT = 0x00000002


def main() -> int:
    if len(sys.argv) == 5 and sys.argv[1] == "--burst":
        return burst(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    return grab(sys.argv[1], int(sys.argv[2]))


def burst(outdir: str, pid: int, count: int) -> int:
    """    python tools/print_window.py --burst OUTDIR PID COUNT

    COUNT grabs as fast as PrintWindow allows, saved as OUTDIR/tMMMMMM.jpg (ms since
    the first), the naming tools/frame_motion.py reads."""
    import os
    import time
    os.makedirs(outdir, exist_ok=True)
    start = time.perf_counter()
    for _ in range(count):
        ms = int((time.perf_counter() - start) * 1000)
        if grab(os.path.join(outdir, f"t{ms:06d}.jpg"), pid, quiet=True) != 0:
            return 1
    span = time.perf_counter() - start
    print(f"{count} grabs in {span:.2f} s ({count / span:.1f} a second)")
    return 0


def grab(out: str, pid: int, quiet: bool = False) -> int:
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    try:
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per-monitor v2
    except Exception:
        pass

    found = []
    enum_proc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)

    def visit(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            r = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            found.append(((r.right - r.left) * (r.bottom - r.top), hwnd, r.right - r.left, r.bottom - r.top))
        return True

    user32.EnumWindows(enum_proc(visit), 0)
    if not found:
        print(f"no visible window for process {pid}", file=sys.stderr)
        return 1
    _, hwnd, w, h = max(found)

    hdc_window = user32.GetDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_window)
    bitmap = gdi32.CreateCompatibleBitmap(hdc_window, w, h)
    gdi32.SelectObject(hdc_mem, bitmap)
    ok = user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT | 1)   # 1 = client area only

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    header = BITMAPINFOHEADER()
    header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    header.biWidth = w
    header.biHeight = -h
    header.biPlanes = 1
    header.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, bitmap, 0, h, buf, ctypes.byref(header), 0)
    Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB").save(out)

    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_window)
    if not quiet or not ok:
        print(f"{'printed' if ok else 'PrintWindow failed for'} window {w}x{h} -> {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
