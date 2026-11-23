"""Run with Xvfb and Pillow against the SDL executable."""

import ctypes as c
import os
import pathlib
import subprocess
import sys
import tempfile
import time
from PIL import ImageChops, ImageGrab


x11 = c.CDLL("libX11.so.6")
xtst = c.CDLL("libXtst.so.6")
x11.XOpenDisplay.restype = c.c_void_p
x11.XOpenDisplay.argtypes = [c.c_char_p]
x11.XStringToKeysym.restype = c.c_ulong
x11.XStringToKeysym.argtypes = [c.c_char_p]
x11.XKeysymToKeycode.restype = c.c_uint
x11.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
x11.XSync.argtypes = [c.c_void_p, c.c_int]
x11.XCloseDisplay.argtypes = [c.c_void_p]
xtst.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
display = x11.XOpenDisplay(None)
assert display

def key(name, delay=.38, held=.09):
    code = x11.XKeysymToKeycode(display, x11.XStringToKeysym(name.encode()))
    assert code
    for pressed in (1, 0):
        assert xtst.XTestFakeKeyEvent(display, code, pressed, 0)
        x11.XSync(display, 0)
        time.sleep(held if pressed else delay)

binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as folder:
    base = pathlib.Path(folder)
    preload = base / "slow_copy.so"
    subprocess.run(["cc", "-Wall", "-shared", "-fPIC", str(pathlib.Path(__file__).with_name("slow_copy.c")),
                    "-o", str(preload), "-ldl"], check=True)
    start = base / "start"
    start.mkdir()
    src = start / "žlutý soubor.bin"
    src.write_bytes(bytes(8 * 1024 * 1024))
    output = base / "marker"
    env = dict(os.environ, FILES_START_DIR=str(start), SDL_VIDEODRIVER="x11",
               LD_PRELOAD=str(preload), FILES_COPY_MARKER=str(output))
    with (base / "log").open("w") as log:
        app = subprocess.Popen([binary], env=env, stdout=log, stderr=log)
        try:
            time.sleep(1.0)
            key("Tab")
            key("Down")
            key("Down")
            key("Return")
            key("Tab")
            for _ in range(4):
                key("Down")
            key("Return")
            key("Return", delay=.08)
            deadline = time.monotonic() + 1
            while not output.exists() and time.monotonic() < deadline:
                time.sleep(.02)
            assert output.exists(), (base / "log").read_text()
            first = ImageGrab.grab()
            key("Escape", delay=.26)
            second = ImageGrab.grab()
            responsive = ImageChops.difference(first, second).getbbox() is not None
            time.sleep(3.1)
            residue = list(start.glob("* copy*")) + list(start.glob(".files-transfer-*"))
            print("UI changed after Esc during copy:", responsive, "| residue:", [p.name for p in residue])
            assert responsive and not residue and src.stat().st_size == 8 * 1024 * 1024, (base / "log").read_text()
        finally:
            app.terminate()
            app.wait(timeout=8)
x11.XCloseDisplay(display)
