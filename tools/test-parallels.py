#!/usr/bin/env python3
"""Automated Parallels Desktop validation for SHENLONG OS."""
import shutil
import subprocess
import sys
import time
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/out'


def run(cmd, check=True):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=check)


def main():
    prlctl = shutil.which('prlctl') or '/usr/local/bin/prlctl'
    if not Path(prlctl).is_file():
        sys.exit("prlctl not found")

    vm_name = "SHENLONG v3.8"
    print(f"[*] Starting Parallels VM '{vm_name}'...")
    run([prlctl, "start", vm_name], check=False)

    screenshot_path = OUT / "parallels-win10-desktop.png"
    deadline = time.time() + 30
    booted = False

    while time.time() < deadline:
        time.sleep(2)
        res = subprocess.run([prlctl, "capture", vm_name, "--file", str(screenshot_path)],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if res.returncode == 0 and screenshot_path.is_file():
            im = Image.open(screenshot_path).convert("RGB")
            # Check taskbar color (23, 23, 23)
            if im.height >= 48 and (im.getpixel((im.width // 2, im.height - 10)) == (23, 23, 23) or im.getpixel((10, im.height - 10)) == (23, 23, 23)):
                booted = True
                print(f"PASS parallels: Booted successfully in Parallels Desktop ({im.width}x{im.height})")
                break

    run([prlctl, "stop", vm_name, "--kill"], check=False)

    if not booted:
        sys.exit("FAIL: VM did not render taskbar within deadline")

    print(f"[+] Screenshot saved to {screenshot_path}")


if __name__ == '__main__':
    main()
