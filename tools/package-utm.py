#!/usr/bin/env python3
"""Create a self-contained, PS/2-enabled UTM machine without touching other VMs."""
import argparse
from pathlib import Path
import plistlib
import shutil
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    parser.add_argument('--name', default='SHENLONG OS v7.1')
    parser.add_argument('--memory', type=int, default=8192)
    parser.add_argument('--screenshot', type=Path, default=None)
    parser.add_argument('-f', '--force', action='store_true', help='Overwrite destination if exists')
    args = parser.parse_args()
    bundle = args.destination.resolve()
    iso = ROOT / 'build/out/shenlong.iso'
    if not iso.is_file():
        raise SystemExit('Build the ISO with make iso first.')
    if bundle.suffix != '.utm':
        raise SystemExit('Destination must have .utm extension.')
    if bundle.exists():
        if args.force:
            shutil.rmtree(bundle)
        else:
            raise SystemExit('Destination .utm already exists. Use -f to overwrite.')
    config = {
        'Backend': 'QEMU', 'ConfigurationVersion': 4,
        'Information': {
            'Name': args.name,
            'UUID': str(uuid.uuid4()).upper(), 'IconCustom': False,
        },
        'System': {
            'Architecture': 'x86_64', 'Target': 'q35', 'CPU': 'max',
            'CPUCount': 1, 'MemorySize': args.memory, 'JITCacheSize': 0,
            'ForceMulticore': False, 'CPUFlagsAdd': [], 'CPUFlagsRemove': [],
        },
        'QEMU': {
            'Hypervisor': False, 'UEFIBoot': True, 'PS2Controller': True,
            'DebugLog': False, 'AdditionalArguments': [], 'RNGDevice': False,
            'BalloonDevice': False, 'TPMDevice': False, 'TSO': False,
            'RTCLocalTime': False,
        },
        'Input': {'UsbBusSupport': 'Disabled', 'UsbSharing': False, 'MaximumUsbShare': 0},
        'Display': [{
            'Hardware': 'virtio-vga', 'DynamicResolution': False,
            'NativeResolution': True, 'UpscalingFilter': 'Linear',
            'DownscalingFilter': 'Linear',
        }],
        'Drive': [
            {
                'Identifier': str(uuid.uuid4()).upper(),
                'ImageName': 'shenlong.iso', 'ImageType': 'CD', 'Interface': 'IDE',
                'InterfaceVersion': 1, 'ReadOnly': True,
            },
            {
                'Identifier': str(uuid.uuid4()).upper(),
                'ImageName': 'shenlong-disk.img', 'ImageType': 'Disk', 'Interface': 'IDE',
                'InterfaceVersion': 1, 'ReadOnly': False,
            }
        ],
        'Network': [{
            'Mode': 'Emulated', 'Hardware': 'rtl8139',
            'MacAddress': '52:54:00:53:48:20', 'IsolateFromHost': False,
            'PortForward': [{
                'GuestAddress': '10.0.2.15',
                'GuestPort': 22,
                'HostAddress': '127.0.0.1',
                'HostPort': 2222,
                'Protocol': 'TCP',
            }], 'VlanGuestAddress': '10.0.2.0/24',
            'VlanHostAddress': '10.0.2.2', 'VlanDhcpStartAddress': '10.0.2.15',
            'VlanDnsServerAddress': '10.0.2.3',
        }], 'Serial': [], 'Sound': [{'Hardware': 'AC97'}],
        'Sharing': {'ClipboardSharing': False, 'DirectoryShareMode': 'None',
                    'DirectoryShareReadOnly': True},
    }
    (bundle / 'Data').mkdir(parents=True)
    shutil.copy2(iso, bundle / 'Data/shenlong.iso')
    disk_file = bundle / 'Data/shenlong-disk.img'
    with disk_file.open('wb') as f:
        f.seek(64 * 1024 * 1024 - 1)
        f.write(b'\0')
    with (bundle / 'config.plist').open('wb') as f:
        plistlib.dump(config, f)
    if args.screenshot and args.screenshot.is_file():
        shutil.copy2(args.screenshot, bundle / 'screenshot.png')
    print(bundle)


if __name__ == '__main__':
    main()
