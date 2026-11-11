#!/usr/bin/env python3
"""Exercise the shipped native shell through real QEMU PS/2 input."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import select
import subprocess
import time
from PIL import Image, ImageChops
from input_tr import type_turkish

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/out"
FW = Path("/Applications/UTM.app/Contents/Resources/qemu")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bios", action="store_true")
    parser.add_argument("--iso", type=Path, default=OUT / "shenlong.iso")
    parser.add_argument("--utm", type=Path, help="Validate the packaged ISO and matching UTM devices")
    args = parser.parse_args()
    if args.utm:
        args.iso = args.utm / "Data/shenlong.iso"
        config = plistlib.loads((args.utm / "config.plist").read_bytes())
        assert config["Drive"][0]["ImageType"] == "CD" and config["Drive"][0]["ReadOnly"]
        assert config["QEMU"]["PS2Controller"] and config["System"]["Target"] == "q35"
    iso_sha = hashlib.sha256(args.iso.read_bytes()).hexdigest()
    mode = "bios" if args.bios else "uefi"
    cmd = [str(OUT / "utm-qemu-runner"), "-L", str(FW),
           "-machine", "q35,i8042=on", "-accel", "tcg", "-cpu", "max",
           "-m", "512M", "-smp", "1", "-vga", "virtio", "-display", "none",
           "-serial", "none", "-qmp", "stdio", "-no-reboot",
           "-cdrom", str(args.iso.resolve()), "-boot", "d"]
    if not args.bios:
        cmd += ["-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={FW}/edk2-x86_64-code.fd",
                "-drive", f"if=pflash,format=qcow2,unit=1,snapshot=on,file={FW}/edk2-i386-vars.fd"]
    if args.utm:
        # Match UTM's one IDE bus per device on q35, preserve its fresh disk.
        del cmd[cmd.index("-cdrom"):cmd.index("-cdrom")+2]
        cmd += ["-drive", f"if=none,id=bootcd,format=raw,media=cdrom,readonly=on,file={args.iso.resolve()}",
                "-device", "ide-cd,drive=bootcd,bus=ide.0,bootindex=0",
                "-drive", f"if=none,id=data,format=raw,snapshot=on,file={(args.utm / 'Data/shenlong-disk.img').resolve()}",
                "-device", "ide-hd,drive=data,bus=ide.1,bootindex=1",
                "-netdev", "user,id=hostnet", "-device", "rtl8139,netdev=hostnet",
                "-audiodev", "none,id=audio", "-device", "AC97,audiodev=audio"]
    # Use opaque cursor pixels to locate the actual pointer, including acceleration.
    values = [int(v, 16) for v in re.findall(r"0x([0-9a-f]+)u", (ROOT / "kernel/cursor_data.h").read_text())]
    pattern = [(i % 22, i // 22, ((v >> 16) & 255, (v >> 8) & 255, v & 255))
               for i, v in enumerate(values) if v >> 24 == 255]
    checks = []
    with (OUT / f"win10-{mode}-qemu.log").open("w") as log:
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, bufsize=0)
        def receive():
            data = bytearray()
            while not data.endswith(b"\n"):
                if not select.select([proc.stdout], [], [], 25)[0]:
                    raise TimeoutError("QMP timeout")
                byte = proc.stdout.read(1)
                if not byte:
                    raise RuntimeError("QEMU exited")
                data.extend(byte)
            return json.loads(data)
        def qmp(command, **arguments):
            proc.stdin.write((json.dumps({"execute": command, "arguments": arguments}) + "\n").encode())
            while True:
                reply = receive()
                if "error" in reply:
                    raise RuntimeError(reply)
                if "return" in reply:
                    return reply["return"]
        def frame(name=None):
            path = OUT / f"win10-{mode}-current.ppm"
            qmp("screendump", filename=str(path))
            with Image.open(path) as src:
                im = src.convert("RGB")
            if name:
                im.save(OUT / f"win10-{mode}-{name}.png")
            return im
        def wait(check, message, seconds=15):
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                im = frame()
                if check(im):
                    return im
                time.sleep(.12)
            frame("failure")
            raise AssertionError(message)
        def key(code, down):
            qmp("input-send-event", events=[{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": code}}}])
            time.sleep(.04)
        def tap(code):
            key(code, True)
            key(code, False)
        def type_text(text):
            type_turkish(text, tap, key)
        def pointer():
            im = frame()
            data = im.tobytes()
            i = 0
            while True:
                i = data.find(b"\xf0\xf0\xf0", i)
                if i < 0:
                    raise AssertionError("Cursor not found")
                if i % 3 == 0:
                    x, y = i // 3 % im.width - 2, i // 3 // im.width - 3
                    if 0 <= x < im.width and 0 <= y < im.height:
                        if all(im.getpixel((x + dx, y + dy)) == c for dx, dy, c in pattern
                               if x + dx < im.width and y + dy < im.height):
                            return x, y
                i += 3
        def move_to(x, y):
            for _ in range(80):
                px, py = pointer()
                dx, dy = x - px, y - py
                if abs(dx) <= 1 and abs(dy) <= 1:
                    return
                def axis(delta):
                    if abs(delta) > 30:
                        return max(-60, min(60, int(delta / 2.6)))
                    return max(-2, min(2, delta))
                qmp("input-send-event", events=[
                    {"type": "rel", "data": {"axis": "x", "value": axis(dx)}},
                    {"type": "rel", "data": {"axis": "y", "value": axis(dy)}}])
                time.sleep(.035)
            raise AssertionError(f"Pointer failed to reach {x},{y}")
        def button(down, which="left"):
            qmp("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": which}}])
            time.sleep(.09)
        def click(x, y, which="left"):
            move_to(x, y)
            button(True, which)
            button(False, which)
            time.sleep(.25)
        def passed(message):
            checks.append(message)
            print("PASS " + mode + ": " + message, flush=True)
        try:
            receive()
            qmp("qmp_capabilities")
            wait(lambda im: im.size == (1920, 1080) and im.getpixel((960, 465))[0] > 80,
                 "Boot splash missing", 90)
            tap("esc")
            wait(lambda im: im.getpixel((800, 1060)) != im.getpixel((800, 500)), "Taskbar missing", 60)
            desktop_frame = frame("desktop")
            passed("Full HD desktop and taskbar rendered")
            tap("meta_l")
            wait(lambda im: ImageChops.difference(im, desktop_frame).getbbox() is not None, "Start missing")
            frame("start")
            passed("Left-aligned Start opens from Windows key")
            tap("esc")
            wait(lambda im: ImageChops.difference(im, desktop_frame).getbbox() is None, "Escape did not dismiss Start")
            click(24, 1052)
            wait(lambda im: ImageChops.difference(im, desktop_frame).getbbox() is not None, "Start click failed")
            start_frame = frame()
            type_text("term\n")
            wait(lambda im: ImageChops.difference(start_frame.crop((250,180,1450,980)), im.crop((250,180,1450,980))).getbbox() is not None, "Terminal search launch failed")
            frame("terminal")
            passed("Smooth Start menu, Escape dismiss, and Terminal launch")
            # Controls on the right: maximize, restore, minimize, taskbar restore.
            before_window = frame()
            click(1340, 252)
            wait(lambda im: ImageChops.difference(before_window, im).getbbox() is not None, "Maximize failed")
            frame("maximized")
            before_restore = frame()
            click(1848, 20)
            wait(lambda im: ImageChops.difference(before_restore, im).getbbox() is not None, "Restore failed")
            for _ in range(3):
                click(1340, 252)
                click(1848, 20)
            before_minimize = frame()
            click(1293, 252)
            wait(lambda im: ImageChops.difference(before_minimize, im).getbbox() is not None, "Minimize failed")
            click(312, 1053)
            wait(lambda im: ImageChops.difference(before_minimize, im).getbbox() is not None, "Taskbar restore failed")
            passed("Repeated maximize/restore, minimize and taskbar restore")
            # Drag title without altering window contents.
            move_to(740, 252)
            button(True)
            move_to(820, 292)
            button(False)
            frame("drag")
            passed("Native title drag")
            before_show_desktop = frame()
            click(1916, 1052)
            assert ImageChops.difference(before_show_desktop, frame()).getbbox() is not None, "Show desktop failed"
            before_context = frame()
            click(1510, 750, "right")
            wait(lambda im: ImageChops.difference(before_context, im).getbbox() is not None, "Context missing")
            frame("context")
            tap("esc")
            before_calendar = frame()
            click(1890, 1053)
            wait(lambda im: ImageChops.difference(before_calendar, im).getbbox() is not None, "Calendar missing")
            frame("calendar")
            tap("esc")
            # Open every application from its taskbar button, close with right control.
            widths = [900,900,720,1040,720,1000]
            heights = [570,600,540,680,540,660]
            for app in range(1, 6):
                before_app = frame()
                click(312 + app * 48, 1053)
                ww, hh = widths[app], heights[app]
                wx = (1920 - ww)//2 + app*26
                wy = (1032 - hh)//2 + app*18
                wait(lambda im: ImageChops.difference(before_app, im).getbbox() is not None, f"App {app} failed")
                frame(f"app-{app}")
                if app == 3:
                    before = frame().crop((wx+24,wy+200,wx+ww-24,wy+hh-60))
                    key("ctrl", True)
                    tap("l")
                    key("ctrl", False)
                    type_text("file:///ui/about.html\n")
                    wait(lambda im: ImageChops.difference(before,im.crop((wx+24,wy+200,wx+ww-24,wy+hh-60))).getbbox(),
                         "About navigation did not change content")
                    frame("about")
                    tap("pgdn")
                    time.sleep(.2)
                    frame("about-scrolled")
                click(wx + ww - 24, wy + 20)
                wait(lambda im: ImageChops.difference(before_app, im).getbbox() is not None, f"Close {app} failed")
            passed("Calendar and five application launch/close flows")
            tap("meta_l")
            time.sleep(.25)
            frame("start-final")
            assert hashlib.sha256(args.iso.read_bytes()).hexdigest() == iso_sha, "ISO changed during test"
            (OUT / f"win10-{mode}-results.json").write_text(json.dumps({
                "checks": checks, "iso_sha256": iso_sha,
                "resolution": [1920,1080], "physical_fps": "not measured"
            }, indent=2) + "\n")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


if __name__ == "__main__":
    main()
