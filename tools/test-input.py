#!/usr/bin/env python3
"""Boot the real ISO with UTM's QEMU; check actual framebuffer text after keys."""
import argparse
import json
from pathlib import Path
import re
import select
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/out'
FIRMWARE = Path('/Applications/UTM.app/Contents/Resources/qemu')


def read_frame(path):
    with path.open('rb') as f:
        assert f.readline().strip() == b'P6'
        width, height = map(int, f.readline().split())
        assert f.readline().strip() == b'255'
        pixels = f.read()
    return width, height, pixels


def read_screen(path, scale=1):
    # Decode rendered terminal cells, not a test-only log or mocked driver.
    width, height, pixels = read_frame(path)
    source = (ROOT / 'kernel/font8x16.h').read_text()
    fw, fh = 8, 16
    glyphs = re.findall(r'\{((?:0x[0-9a-fA-F]+,?\s*){16})\}', source)
    lookup = {}
    for char, glyph in enumerate(glyphs, 32):
        data = bytes(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', glyph))
        lookup.setdefault(tuple(data), chr(char))
    sidebar = 208 if width >= 1000 else 168
    left = sidebar + 28 if width >= 900 else 20
    rail = 280 if width >= 1152 else 0
    panel_width = width - left - (28 if width >= 900 else 20) - rail
    top = 158 if height >= 600 else 140
    panel_height = height - top - (64 if height >= 600 else 44)
    x0, y0 = left + 20, top + 64
    columns, rows = (panel_width - 40)//fw, (panel_height - 80)//fh
    lines = []
    for row in range(rows):
        line = ''
        for col in range(columns):
            glyph = []
            for y in range(fh):
                bits = 0
                for x in range(fw):
                    offset = ((y0 + row*fh + y)*width + x0 + col*fw + x)*3
                    bits = (bits << 1) | (max(pixels[offset:offset+3]) > 127)
                glyph.append(bits)
            line += lookup.get(tuple(glyph), '?')
        lines.append(line.rstrip())
    return '\n'.join(lines).rstrip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--usb-only', action='store_true')
    parser.add_argument('--bios', action='store_true')
    parser.add_argument('--scale', type=int, choices=[1, 2], default=1)
    parser.add_argument('--mouse', action='store_true')
    parser.add_argument('--iso', type=Path, default=OUT / 'shenlong.iso')
    parser.add_argument('--boot-only', action='store_true')
    parser.add_argument('--vga', default='virtio')
    args = parser.parse_args()
    mode = ('bios' if args.bios else 'uefi') + ('-usb-only' if args.usb_only else '-ps2')
    if args.mouse: mode += '-mouse'
    cmd = [str(OUT / 'utm-qemu-runner'), '-L', str(FIRMWARE),
           '-machine', 'q35,i8042=' + ('off' if args.usb_only else 'on'),
           '-accel', 'tcg', '-m', '256M', '-smp', '1', '-vga', args.vga,
           '-display', 'none', '-serial', 'none', '-qmp', 'stdio', '-no-reboot',
           '-cdrom', str(args.iso.resolve()), '-boot', 'd']
    if not args.bios:
        cmd += ['-drive', f'if=pflash,format=raw,unit=0,readonly=on,file={FIRMWARE}/edk2-x86_64-code.fd',
                '-drive', f'if=pflash,format=qcow2,unit=1,snapshot=on,file={FIRMWARE}/edk2-i386-vars.fd']
    if args.usb_only:
        cmd += ['-device', 'qemu-xhci', '-device', 'usb-kbd']
    with (OUT / f'{mode}-qemu.log').open('w') as log:
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=log, bufsize=0)
        def receive():
            data = bytearray()
            while not data.endswith(b'\n'):
                if not select.select([proc.stdout], [], [], 20)[0]:
                    raise TimeoutError('QMP response timeout')
                byte = proc.stdout.read(1)
                if not byte:
                    raise RuntimeError('QEMU exited: ' + (OUT / f'{mode}-qemu.log').read_text())
                data.extend(byte)
            return json.loads(data)

        def qmp(command, **arguments):
            proc.stdin.write((json.dumps({'execute': command, 'arguments': arguments}) + '\n').encode())
            while True:
                reply = receive()
                if 'error' in reply:
                    raise RuntimeError(reply)
                if 'return' in reply:
                    return reply['return']

        def key(code, down):
            qmp('input-send-event', events=[{'type': 'key', 'data': {
                'down': down, 'key': {'type': 'qcode', 'data': code}}}])
            time.sleep(0.025)

        def tap(code):
            key(code, True)
            key(code, False)

        def type_text(text):
            for char in text:
                tap({' ': 'spc', '\n': 'ret', '\b': 'backspace', '-': 'minus'}.get(char, char))
            if text.endswith('\n') and not args.usb_only:
                wait_prompt()

        def screen():
            path = OUT / f'{mode}-input.ppm'
            qmp('screendump', filename=str(path))
            return read_screen(path, args.scale)

        def frame():
            path = OUT / f'{mode}-mouse.ppm'
            qmp('screendump', filename=str(path))
            return read_frame(path)

        def wait_prompt():
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                time.sleep(0.1)
                if screen().rstrip(' ?\n').endswith('root@shenlong:/#'):
                    return
            raise AssertionError('Command did not finish within 10 seconds')

        def move(dx, dy):
            qmp('input-send-event', events=[
                {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}])
            time.sleep(0.04)

        def cursor_box(snapshot):
            w, h, pixels = snapshot
            # Cursor's exact neutral white is distinct from UI text.
            points = [i//3 for i in range(0, len(pixels), 3) if pixels[i:i+3] == b'\xf0\xf0\xf0']
            assert points, 'Cursor not visible'
            xs = [p % w for p in points]; ys = [p // w for p in points]
            box = (min(xs), min(ys), max(xs), max(ys))
            assert box[2]-box[0] < 24 and box[3]-box[1] < 34, ('Cursor trails', box)
            return box

        try:
            receive()
            qmp('qmp_capabilities')
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                time.sleep(0.4)
                text = screen()
                if 'root@shenlong:/#' in text:
                    break
            else:
                raise AssertionError('Shell did not start: ' + text)
            qmp('screendump', filename=str(OUT / f'{mode}-desktop.ppm'))
            print(f'PASS {mode}: boot reached the real shell.', flush=True)
            if args.boot_only:
                return
            if not args.usb_only:
                type_text('clear\n')
            type_text('echo input-ok\n')
            time.sleep(0.2)
            text = screen()
            if args.usb_only:
                assert 'input-ok' not in text, text
                print('REPRODUCED: USB-only UTM configuration cannot deliver keys to PS/2 kernel.')
            else:
                assert '\ninput-ok\n' in text, text
                type_text('echo abx\bc\n')
                type_text('echo ')
                key('shift', True)
                tap('a')
                key('shift', False)
                tap('caps_lock')
                tap('b')
                key('shift', True)
                tap('c')
                key('shift', False)
                tap('caps_lock')
                tap('ret')
                type_text('uname\n')
                time.sleep(0.2)
                text = screen()
                assert '\nabc\n' in text, text
                assert '\nABc\n' in text, text
                assert '\nSHENLONG OS\n' in text, text
                print(f'PASS {mode}: typing, Enter, Backspace, Shift, Caps Lock, Shift+Caps, command execution.')
                if args.mouse:
                    baseline = frame()
                    box = cursor_box(baseline)
                    move(180, -140)
                    shifted = cursor_box(frame())
                    assert shifted[0] == box[0]+180 and shifted[1] == box[1]-140, (box, shifted)
                    move(-180, 140)
                    returned = frame()
                    # The status bar deliberately updates the live uptime.
                    size = baseline[0] * (baseline[1]-36) * 3
                    assert returned[2][:size] == baseline[2][:size], 'Motion left stale pixels'
                    # Exercise shared-port dispatch while typing and moving.
                    type_text('echo ')
                    for i in range(30):
                        move(2 if i%2 == 0 else -2, 1 if i%2 == 0 else -1)
                        tap('a')
                    tap('ret')
                    # Park away from glyphs before OCR: a correct cursor overlay
                    # intentionally changes the pixels underneath it.
                    move(300, 200)
                    text = screen()
                    assert '\n' + 'a'*30 + '\n' in text, text
                    move(-300, -200)
                    # Move over text, draw underneath it, then move away.
                    move(-300, -260)
                    type_text('clear\necho redraw-ok\n')
                    move(300, 260)
                    assert '\nredraw-ok\n' in screen()
                    cursor_box(frame())
                    # Full-screen scrolling and clearing must not bake in cursor pixels.
                    type_text('help\n')
                    wait_prompt()
                    cursor_box(frame())
                    type_text('clear\necho scroll-ok\n')
                    assert '\nscroll-ok\n' in screen()
                    cursor_box(frame())
                    for down in (True, False):
                        qmp('input-send-event', events=[{'type': 'btn', 'data': {'down': down, 'button': 'left'}}])
                        time.sleep(0.04)
                    type_text('echo click-ok\n')
                    assert '\nclick-ok\n' in screen()
                    # Clamp at the screen edges, then return to a visible position.
                    for _ in range(8): move(-200, -200)
                    assert cursor_box(frame())[0] <= 2
                    for _ in range(8): move(200, 200)
                    move(-100, -100)
                    edge = cursor_box(frame())
                    # Bright pixels are inset from the actual arrow hotspot.
                    assert abs(edge[0] - (baseline[0]-101 + box[0]-baseline[0]//2)) < 3, edge
                    assert abs(edge[1] - (baseline[1]-101 + box[1]-baseline[1]//2)) < 3, edge
                    text = screen()
                    print('PASS mouse: visibility, signed motion, no trails, simultaneous typing, redraw, scroll, clear, click and edge clipping.')
                    # Sidebar navigation and a real quick-action command.
                    def click_at(x, y):
                        box = cursor_box(frame())
                        move(x-box[0], y-box[1])
                        for down in (True, False):
                            qmp('input-send-event', events=[{'type':'btn', 'data':{'down':down,'button':'left'}}])
                            time.sleep(0.08)
                    click_at(80,260)
                    w,h,pixels=frame()
                    assert pixels[(154*w+16)*3:(154*w+16)*3+3] != b'\x63\xe0\xbe', 'Terminal tab stayed selected'
                    click_at(80,164)
                    if w >= 1152:
                        click_at(w-180,518)
                        wait_prompt()
                        assert 'bin' in screen(), 'Browse files action did not execute ls'
                    click_at(w-280-80, h-48)
                    wait_prompt()
                    assert screen().strip(' ?\n') == 'root@shenlong:/#', 'Clear button failed'
                    print('PASS UI: sidebar navigation and quick actions.', flush=True)
            (OUT / f'{mode}-input.txt').write_text(text + '\n')
            print(text)
        finally:
            if proc.poll() is None:
                try:
                    qmp('quit')
                    proc.wait(timeout=5)
                finally:
                    if proc.poll() is None:
                        proc.kill()
                        proc.wait()


if __name__ == '__main__':
    main()
