#!/usr/bin/env python3
"""Configure and package SHENLONG OS for Parallels Desktop on Apple Silicon & Intel Macs."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run_cmd(cmd, check=True):
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and res.returncode != 0:
        print(f"Error executing: {' '.join(cmd)}\n{res.stderr.strip()}", file=sys.stderr)
        sys.exit(res.returncode)
    return res


def main():
    parser = argparse.ArgumentParser(description="Create or configure Parallels Desktop VM for SHENLONG OS")
    parser.add_argument('--name', default='SHENLONG v3.8', help='Virtual Machine Name in Parallels')
    parser.add_argument('--memory', type=int, default=4096, help='RAM size in MB')
    parser.add_argument('--iso', type=Path, default=ROOT / 'build/out/shenlong.iso', help='Path to ISO')
    args = parser.parse_args()

    prlctl = shutil.which('prlctl')
    if not prlctl:
        for candidate in [
            '/usr/local/bin/prlctl',
            '/Applications/Parallels Desktop.app/Contents/MacOS/prlctl',
        ]:
            if Path(candidate).is_file():
                prlctl = candidate
                break

    if not prlctl:
        sys.exit("Error: Parallels Desktop CLI (prlctl) not found. Is Parallels Desktop installed?")

    iso_path = args.iso.resolve()
    if not iso_path.is_file():
        sys.exit(f"Error: ISO not found at {iso_path}. Run 'make iso' first.")

    # Check if VM already exists
    list_res = run_cmd([prlctl, 'list', '-a', '--json'], check=False)
    vm_names = []
    if list_res.returncode == 0:
        try:
            import json
            vms = json.loads(list_res.stdout)
            vm_names = [v.get('name') for v in vms if 'name' in v]
        except Exception:
            pass

    if args.name not in vm_names:
        print(f"[*] Creating Parallels VM '{args.name}' with x86 emulator...")
        run_cmd([prlctl, 'create', args.name, '-o', 'windows', '--cpu-type', 'x86'])
    else:
        print(f"[*] Parallels VM '{args.name}' exists. Stopping if active...")
        run_cmd([prlctl, 'stop', args.name, '--kill'], check=False)

    print(f"[*] Configuring '{args.name}' (UEFI, SecureBoot off, BootOrder cdrom0)...")
    run_cmd([prlctl, 'set', args.name, '--efi-secure-boot', 'off'])
    run_cmd([prlctl, 'set', args.name, '--device-bootorder', 'cdrom0 hdd0'])
    run_cmd([prlctl, 'set', args.name, '--device-set', 'cdrom0', '--image', str(iso_path), '--connect'])
    run_cmd([prlctl, 'set', args.name, '--memsize', str(args.memory), '--cpus', '1'])
    run_cmd([prlctl, 'set', args.name, '--device-del', 'usb'], check=False)
    run_cmd([prlctl, 'set', args.name, '--smart-mouse-optimize', 'on', '--sticky-mouse', 'off', '--smooth-scrolling', 'off'], check=False)
    run_cmd([prlctl, 'set', args.name, '--high-resolution', 'on', '--high-resolution-in-guest', 'on', '--native-scaling-in-guest', 'on', '--video-adapter-type', 'parallels', '--videosize', '256'], check=False)
    run_cmd([prlctl, 'set', args.name, '--autostop', 'stop', '--on-window-close', 'stop', '--on-shutdown', 'close'], check=False)

    # Get home path of VM
    info_res = run_cmd([prlctl, 'list', '-i', args.name])
    home_dir = None
    for line in info_res.stdout.splitlines():
        if line.strip().startswith('Home:'):
            home_dir = Path(line.split('Home:', 1)[1].strip())
            break

    desktop_link = Path.home() / 'Desktop' / f"{args.name.replace(' ', '-')}.pvm"
    if home_dir and home_dir.is_dir():
        if desktop_link.is_symlink() or desktop_link.exists():
            desktop_link.unlink() if desktop_link.is_symlink() else shutil.rmtree(desktop_link)
        desktop_link.symlink_to(home_dir)
        print(f"[*] Desktop shortcut created: {desktop_link} -> {home_dir}")

    print(f"\n[+] Parallels Desktop VM '{args.name}' hazır!")
    print(f"    Parallels Desktop uygulamasından veya '{prlctl} start \"{args.name}\"' ile başlatabilirsiniz.")


if __name__ == '__main__':
    main()
