#!/usr/bin/env python3
"""2.5 interaction smoke test against the real guest framebuffer and PS/2 input."""
import argparse
import json
import re
import select
import subprocess
import time
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/out'
FW = Path('/Applications/UTM.app/Contents/Resources/qemu')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bios', action='store_true')
    args = parser.parse_args()
    prefix = 'v25-bios' if args.bios else 'v25'
    command = [str(OUT/'utm-qemu-runner'), '-L', str(FW), '-machine', 'q35,i8042=on',
               '-accel', 'tcg', '-cpu', 'max', '-m', '1024M', '-smp', '1', '-vga', 'virtio',
               '-display', 'none', '-serial', 'none', '-qmp', 'stdio', '-no-reboot',
               '-cdrom', str(OUT/'shenlong.iso'), '-boot', 'd', '-netdev', 'user,id=n',
               '-device', 'rtl8139,netdev=n', '-audiodev', 'none,id=a', '-device', 'AC97,audiodev=a']
    if not args.bios:
        command += ['-drive', f'if=pflash,format=raw,unit=0,readonly=on,file={FW}/edk2-x86_64-code.fd',
                    '-drive', f'if=pflash,format=qcow2,unit=1,snapshot=on,file={FW}/edk2-i386-vars.fd']
    log = (OUT/f'{prefix}-qemu.log').open('w')
    proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, bufsize=0)
    def receive():
        data = bytearray()
        while not data.endswith(b'\n'):
            if not select.select([proc.stdout], [], [], 30)[0]:
                raise TimeoutError('QMP timeout')
            b = proc.stdout.read(1)
            if not b:
                raise RuntimeError('QEMU exited; see log')
            data.extend(b)
        return json.loads(data)
    def qmp(cmd, **kw):
        proc.stdin.write((json.dumps({'execute': cmd, 'arguments': kw})+'\n').encode())
        while True:
            result = receive()
            if 'error' in result:
                raise RuntimeError(result)
            if 'return' in result:
                return result['return']
    def frame(name=None):
        path = OUT/f'{prefix}-current.ppm'
        qmp('screendump', filename=str(path))
        with Image.open(path) as source:
            im = source.copy()
        if name:
            im.save(OUT/f'{prefix}-{name}.png')
        return im
    cursor = [int(x, 16) for x in re.findall(r'0x([0-9a-f]+)u', (ROOT/'kernel/cursor_data.h').read_text())]
    opaque = [(i % 22, i // 22, (p >> 16 & 255, p >> 8 & 255, p & 255))
              for i, p in enumerate(cursor) if p >> 24 == 255][:18]
    def pointer(im):
        data = im.tobytes()
        dx, dy, color = opaque[0]
        start = 0
        while True:
            at = data.find(bytes(color), start)
            if at < 0:
                raise AssertionError('Pointer not found')
            start = at + 3
            if at % 3:
                continue
            x, y = at//3 % im.width-dx, at//3//im.width-dy
            if x < 0 or y < 0 or x+22 >= im.width or y+30 >= im.height:
                continue
            if all(im.getpixel((x+sx, y+sy)) == c for sx, sy, c in opaque):
                return x, y
    def move(x, y):
        for _ in range(40):
            px, py = pointer(frame())
            dx, dy = x-px, y-py
            if abs(dx) <= 1 and abs(dy) <= 1:
                return
            distance = dx*dx+dy*dy
            factor = 2.6 if distance > 256 else 2.2 if distance > 81 else 1.8 if distance > 25 else 1.4 if distance > 4 else 1
            sx, sy = int(dx/factor), int(dy/factor)
            sx = max(-60, min(60, sx)); sy = max(-60, min(60, sy))
            if not sx and dx: sx = 1 if dx > 0 else -1
            if not sy and dy: sy = 1 if dy > 0 else -1
            qmp('input-send-event', events=[{'type': 'rel', 'data': {'axis': 'x', 'value': sx}},
                                            {'type': 'rel', 'data': {'axis': 'y', 'value': sy}}])
            time.sleep(.04)
        raise AssertionError('Pointer did not reach target')
    def button(down):
        qmp('input-send-event', events=[{'type': 'btn', 'data': {'button': 'left', 'down': down}}])
        time.sleep(.07)
    def click(x, y, settle=.5):
        move(x, y); button(True); button(False); time.sleep(settle)
    def key(code):
        for down in (True, False):
            qmp('input-send-event', events=[{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': code}}}])
            time.sleep(.06)
    def geometry(app):
        widths = [900, 900, 720, 1040, 720, 1000]
        heights = [570, 600, 540, 680, 540, 660]
        w, h = widths[app], heights[app]
        return min((1920-w)//2+app*26,1920-w), min(32+(936-h)//2+app*18,968-h), w, h
    checks = []
    try:
        receive(); qmp('qmp_capabilities')
        deadline = time.monotonic()+120
        while time.monotonic() < deadline:
            im = frame()
            if im.size == (1920,1080) and im.getpixel((2,2)) == (233,238,246):
                break
            time.sleep(.25)
        else:
            raise AssertionError('2.5 desktop did not boot')
        time.sleep(1); frame('desktop'); checks.append('Full HD boot')
        click(762,1026); frame('launcher')
        assert frame().getpixel((750,600))[0] > 200, 'Launcher failed'
        key('esc'); time.sleep(.3)
        click(1840,16); frame('calendar')
        assert frame().getpixel((1590,150))[0] > 220, 'Calendar failed'
        key('esc'); time.sleep(.4); checks.append('Launcher/calendar open and dismiss')
        for app in range(6):
            click(844+app*60,1026)
            x,y,w,h = geometry(app)
            frame(f'app-{app}')
            assert frame().getpixel((x+22,y+22)) == (255,98,90), f'App {app} did not open'
            if app == 1:
                click(x+218,y+74)  # parent at root must be safe
                click(x+66,y+22); frame('files-maximized')
                assert frame().getpixel((22,54)) == (255,98,90), 'Maximize failed'
                click(66,54); checks.append('Files root navigation and maximize/restore')
            if app == 5:
                key('spc'); time.sleep(.3); frame('media-paused')
                box=(x+260,y+100,x+800,y+380)
                paused=frame().crop(box); time.sleep(.4)
                assert ImageChops.difference(paused,frame().crop(box)).getbbox() is None, 'Pause did not freeze video'
                assert len(paused.getcolors(1000000)) > 100, 'No decoded video pixels'
                # Volume drag and timeline seek, then resume.
                click(x+w-112,y+h-51)
                click(x+600,y+h-111)
                key('spc'); time.sleep(1); frame('media-playing')
                playing=frame().crop(box); time.sleep(.4)
                assert ImageChops.difference(playing,frame().crop(box)).getbbox() is not None, 'Resume did not advance video'
                click(x+66,y+22); frame('media-maximized')
                assert frame().getpixel((22,54)) == (255,98,90), 'Media maximize failed'
                click(66,54)
                checks.append('Media pause, volume, seek and resume')
            click(x+44,y+22); time.sleep(.4)
            assert frame().getpixel((x+22,y+22)) != (255,98,90), 'Minimize failed'
            click(844+app*60,1026); time.sleep(.4)
            assert frame().getpixel((x+22,y+22)) == (255,98,90), 'Restore failed'
            click(x+22,y+22)
            assert frame().getpixel((x+22,y+22)) != (255,98,90), 'Close failed'
            checks.append(f'App {app}: open, minimize, restore, close')
        click(904,1026); click(1840,16); move(1400,920); frame('overview')
        report = {'checks': checks, 'resolution': [1920,1080], 'firmware': 'BIOS' if args.bios else 'UEFI',
                  'fps': '100 Hz configured target; physical display FPS not measured'}
        (OUT/f'{prefix}-result.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2), flush=True)
    except Exception:
        if proc.poll() is None: frame('failure')
        raise
    finally:
        if proc.poll() is None:
            try: qmp('quit')
            except (RuntimeError, BrokenPipeError): pass
            proc.wait(timeout=10)
        log.close()


if __name__ == '__main__':
    main()
