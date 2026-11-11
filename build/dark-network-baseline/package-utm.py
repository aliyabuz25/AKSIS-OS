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
    parser.add_argument('--name', default='SHENLONG v0.4 - Desktop')
    args = parser.parse_args()
    bundle = args.destination.resolve()
    iso = ROOT / 'build/out/shenlong.iso'
    if not iso.is_file():
        raise SystemExit('Build the ISO with make iso first.')
    if bundle.suffix != '.utm' or bundle.exists():
        raise SystemExit('Choose a new, non-existing .utm destination.')
    config = {
        'Backend': 'QEMU', 'ConfigurationVersion': 4,
        'Information': {
            'Name': args.name,
            'UUID': str(uuid.uuid4()).upper(), 'IconCustom': False,
        },
        'System': {
            'Architecture': 'x86_64', 'Target': 'q35', 'CPU': 'default',
            'CPUCount': 1, 'MemorySize': 256, 'JITCacheSize': 0,
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
            'NativeResolution': False, 'UpscalingFilter': 'Nearest',
            'DownscalingFilter': 'Linear',
        }],
        'Drive': [{
            'Identifier': str(uuid.uuid4()).upper(),
            'ImageName': 'shenlong.iso', 'ImageType': 'CD', 'Interface': 'IDE',
            'InterfaceVersion': 1, 'ReadOnly': True,
        }],
        'Network': [], 'Serial': [], 'Sound': [],
        'Sharing': {'ClipboardSharing': False, 'DirectoryShareMode': 'None',
                    'DirectoryShareReadOnly': True},
    }
    (bundle / 'Data').mkdir(parents=True)
    shutil.copy2(iso, bundle / 'Data/shenlong.iso')
    with (bundle / 'config.plist').open('wb') as f:
        plistlib.dump(config, f)
    print(bundle)


if __name__ == '__main__':
    main()
