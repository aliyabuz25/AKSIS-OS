#!/usr/bin/env python3
"""Exercise the actual desktop via QEMU input and framebuffer, without guest hooks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import select
import subprocess
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from PIL import Image, ImageChops, ImageStat
from input_tr import type_turkish

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/out'
FW=Path('/Applications/UTM.app/Contents/Resources/qemu')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bios',action='store_true')
    parser.add_argument('--iso',type=Path,default=OUT/'shenlong.iso')
    parser.add_argument('--network',action='store_true',help='Use a local HTTP fixture through the emulated NIC')
    parser.add_argument('--internet',action='store_true',help='Also verify DNS and public HTTP using example.com')
    parser.add_argument('--boot-only',action='store_true',help='Check Full HD artwork, verbose boot and duration only')
    parser.add_argument('--skip-boot',choices=['esc','ret'],help='Check the boot skip key instead of the default 18-second hold')
    args=parser.parse_args()
    mode='bios' if args.bios else 'uefi'
    requests=[]; server=None
    if args.network or args.internet:
        class Handler(BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def log_message(self,*args): pass
            def do_GET(self):
                requests.append(self.path)
                if self.path=='/redirect':
                    self.send_response(302); self.send_header('Location','/next'); self.send_header('Content-Length','0')
                    self.send_header('Connection','close'); self.end_headers(); return
                if self.path=='/delay': time.sleep(2)
                if self.path=='/next': body=b'<title>Linked page</title><h1>chunk-ok</h1><p>Real TCP response.</p>'
                elif self.path=='/large': body=b'<title>Large page</title><h1>large-ok</h1>'+b'<p>RX ring wrap and TCP acknowledgements.</p>'*800
                else: body=b'<title>Native test</title><h1>Network works</h1><p>network-ok</p><a href="/next">Open next page</a>'
                self.send_response(200); self.send_header('Content-Type','text/html; charset=utf-8'); self.send_header('Connection','close')
                if self.path=='/next':
                    self.send_header('Transfer-Encoding','chunked'); self.end_headers()
                    for start in range(0,len(body),11):
                        chunk=body[start:start+11]; self.wfile.write(f'{len(chunk):x}\r\n'.encode()+chunk+b'\r\n')
                    self.wfile.write(b'0\r\n\r\n')
                else:
                    self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        Thread(target=server.serve_forever,daemon=True).start()
    cmd=[str(OUT/'utm-qemu-runner'),'-L',str(FW),'-machine','q35,i8042=on',
         '-accel','tcg','-m','256M','-smp','1','-vga','virtio','-display','none',
         '-serial','none','-qmp','stdio','-no-reboot','-cdrom',str(args.iso.resolve()),'-boot','d',
         '-netdev','user,id=hostnet','-device','rtl8139,netdev=hostnet,mac=52:54:00:53:48:05']
    if not args.bios:
        cmd+=['-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={FW}/edk2-x86_64-code.fd',
              '-drive',f'if=pflash,format=qcow2,unit=1,snapshot=on,file={FW}/edk2-i386-vars.fd']
    glyphs=re.findall(r'\{((?:0x[0-9a-fA-F]+,?\s*){16})\}',(ROOT/'kernel/font8x16.h').read_text())
    lookup={tuple(int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',g)):chr(i+32) for i,g in enumerate(glyphs)}
    for code,rows in re.findall(r'\{(\d+), \{([^}]+)\}\}',(ROOT/'kernel/terminal_extra.h').read_text()):
        lookup[tuple(int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',rows))]=chr(int(code))
    with (OUT/f'{mode}-desktop.log').open('w') as log:
        proc=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,bufsize=0)
        def receive():
            data=bytearray()
            while not data.endswith(b'\n'):
                if not select.select([proc.stdout],[],[],20)[0]: raise TimeoutError('QMP timeout')
                b=proc.stdout.read(1)
                if not b: raise RuntimeError('QEMU exited')
                data.extend(b)
            return json.loads(data)
        def qmp(command,**arguments):
            proc.stdin.write((json.dumps({'execute':command,'arguments':arguments})+'\n').encode())
            while True:
                reply=receive()
                if 'error' in reply: raise RuntimeError(reply)
                if 'return' in reply: return reply['return']
        def frame(name=None):
            path=OUT/f'{mode}-desktop-current.ppm'
            qmp('screendump',filename=str(path))
            with Image.open(path) as src: im=src.copy()
            if name: im.save(OUT/f'{mode}-{name}.png')
            return im
        def wait(check,message,timeout=12):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                im=frame()
                if check(im): return im
                time.sleep(.12)
            frame('failure')
            raise AssertionError(message)
        def key(code,down):
            qmp('input-send-event',events=[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':code}}}])
            time.sleep(.035)
        def tap(code): key(code,True); key(code,False)
        def type_text(text):
            type_turkish(text,tap,key)
        def mouse_pos():
            # QMP may capture between the framebuffer blit and cursor overlay.
            for _ in range(15):
                im=frame(); pixels=im.tobytes(); w,h=im.size
                pts=[i//3 for i in range(0,len(pixels),3) if pixels[i:i+3]==b'\xf0\xf0\xf0']
                if pts: return min(p%w for p in pts)-cursor_offset[0], min(p//w for p in pts)-cursor_offset[1]
                time.sleep(.1)
            raise AssertionError('Mouse cursor is missing')
        def move(dx,dy):
            while dx or dy:
                sx=max(-100,min(100,dx)); sy=max(-100,min(100,dy))
                qmp('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':sx}},
                                              {'type':'rel','data':{'axis':'y','value':sy}}])
                dx-=sx; dy-=sy; time.sleep(.035)
        def move_to(x,y):
            mx,my=mouse_pos(); move(x-mx,y-my); time.sleep(.1)
        def button(down):
            qmp('input-send-event',events=[{'type':'btn','data':{'down':down,'button':'left'}}]); time.sleep(.12)
        def click(x,y): move_to(x,y); button(True); button(False)
        def terminal(im,box):
            x,y,w,h=box; px=im.load(); lines=[]
            for row in range((h-74)//16):
                line=''
                for col in range((w-32)//8):
                    bits=[]
                    for gy in range(16):
                        value=0
                        for gx in range(8): value=(value<<1)|(max(px[x+16+col*8+gx,y+58+row*16+gy])>127)
                        bits.append(value)
                    line+=lookup.get(tuple(bits),'?')
                lines.append(line.rstrip())
            return '\n'.join(lines).rstrip(' ?\n')
        try:
            receive(); qmp('qmp_capabilities')
            boot=wait(lambda f:f.size==(1920,1080) and f.getpixel((64,208))==(56,157,255),
                      'Full HD boot screen did not appear',90)
            boot_start=time.monotonic()
            time.sleep(1)
            boot=frame('boot')
            with Image.open(ROOT/'assets/boot/dragon.png') as original:
                expected=original.convert('RGB').resize((1280,853),Image.Resampling.BILINEAR)
            actual=boot.crop((640,37,1920,890))
            error=ImageStat.Stat(ImageChops.difference(actual,expected)).mean
            assert max(error)<2, f'Boot artwork is missing, distorted or recolored: {error}'
            # Diagnostic rows must contain real rendered values, not an empty splash.
            for row in range(11):
                colors=boot.crop((386,260+row*36,630,288+row*36)).tobytes()
                assert sum(max(colors[i:i+3])>100 for i in range(0,len(colors),3))>15, f'Verbose row {row} is empty'
            if args.skip_boot: tap(args.skip_boot)
            im=wait(lambda im: im.getpixel((200,im.height-2))==(18,20,22),'Desktop did not boot',90)
            duration=time.monotonic()-boot_start
            if args.skip_boot: assert duration<6, f'Boot skip did not work: {duration:.2f}s'
            else: assert 15<=duration<=20, f'Boot duration is outside 15–20 seconds: {duration:.2f}s'
            assert im.size==(1920,1080), f'Desktop is not Full HD: {im.size}'
            evidence={'firmware':mode,'resolution':im.size,'boot_seconds':duration,
                      'skip_key':args.skip_boot,'artwork_mean_error':error,
                      'iso_sha256':hashlib.sha256(args.iso.read_bytes()).hexdigest()}
            suffix=f'-{args.skip_boot}' if args.skip_boot else ''
            (OUT/f'{mode}-boot-result{suffix}.json').write_text(json.dumps(evidence,indent=2)+'\n')
            print(f'PASS {mode}: original blue logo, 11 verbose rows, Full HD; boot {duration:.2f}s'
                  + (f' ({args.skip_boot} skip).' if args.skip_boot else '.'),flush=True)
            if args.boot_only: return
            w,h=im.size; sx=(w-444)//2+132 if w>=1100 else 144
            pixels=im.tobytes(); points=[i//3 for i in range(0,len(pixels),3) if pixels[i:i+3]==b'\xf0\xf0\xf0']
            cursor_offset=(min(p%w for p in points)-w//2,min(p//w for p in points)-h//2)
            assert im.getpixel((w//2,h//2-100))==(0,0,0),'Black desktop is not clean at boot'
            frame('desktop')
            clock_before=im.crop((w-170,h-52,w-30,h-28)).tobytes()
            wait(lambda f:f.crop((w-170,h-52,w-30,h-28)).tobytes()!=clock_before,'Clock did not tick',5)
            print(f'PASS {mode}: clean desktop and automatic clock ({w}x{h}).',flush=True)
            mx=(w-480)//2; my=h-56-556-12
            click(sx-60,h-28)
            wait(lambda f:f.getpixel((mx+20,my+90))==(16,18,20),'Start did not open')
            frame('start-menu')
            type_text('zzzz\n')
            wait(lambda f:f.getpixel((mx+20,my+90))==(16,18,20),'Empty search crashed')
            type_text('\b\b\b\bterm\n')
            box=((w-900)//2,(h-56-570)//2,900,570)
            wait(lambda f:'root@shenlong:/#' in terminal(f,box),'Terminal did not launch')
            move_to(w-60,100)
            type_text('clear\necho desktop-ok\n')
            wait(lambda f:'\ndesktop-ok\n' in terminal(f,box),'Terminal input failed')
            print(f'PASS {mode}: Start, search, app launch, terminal typing.',flush=True)
            type_text('echo abx\bc\n')
            wait(lambda f:'\nabc\n' in terminal(f,box),'Backspace failed')
            type_text('echo ')
            key('shift',True); tap('a'); key('shift',False)
            tap('caps_lock'); tap('b'); key('shift',True); tap('c'); key('shift',False)
            tap('caps_lock'); tap('ret')
            wait(lambda f:'\nABc\n' in terminal(f,box),'Shift/Caps Lock failed')
            type_text('echo çığşöüİ\n')
            wait(lambda f:'\nçığşöüİ\n' in terminal(f,box),'Turkish Q/UTF-8 terminal failed')
            type_text('echo çx'); tap('left'); tap('backspace'); type_text('ğ'); tap('end'); tap('ret')
            wait(lambda f:'\nğx\n' in terminal(f,box),'UTF-8 cursor editing corrupted a character')
            type_text('help\n')
            wait(lambda f:terminal(f,box).endswith('root@shenlong:/#'),'Scrolling did not finish')
            type_text('clear\necho desktop-ok\n')
            wait(lambda f:'\ndesktop-ok\n' in terminal(f,box),'Clear/scroll damaged terminal')
            x,y,ww,hh=box
            move_to(x+180,y+20); button(True); move(80,40); button(False)
            box=(x+80,y+40,ww,hh); x,y,ww,hh=box
            wait(lambda f:'desktop-ok' in terminal(f,box),'Drag lost terminal content')
            click(x+ww-115,y+20)
            wait(lambda f:f.getpixel((x+20,y+1))!=(81,91,98),'Minimize failed')
            click(sx+74,h-28)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Taskbar restore lost content')
            click(x+ww-69,y+20)
            wait(lambda f:'desktop-ok' in terminal(f,(0,0,w,h-56)),'Maximize lost content')
            click(w-69,20)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Restore lost content')
            print(f'PASS {mode}: dragging, minimize/restore, maximize/restore, retained text.',flush=True)
            click(sx+126,h-28)
            fx=min((w-820)//2+26,w-820); fy=min((h-56-560)//2+18,h-56-560)
            wait(lambda f:f.getpixel((fx+20,fy+1))==(81,91,98),'Files did not open')
            frame('windows')
            click(fx+65,fy+255)
            listing=frame().crop((fx+175,fy+95,fx+810,fy+400)).tobytes()
            click(fx+270,fy+147)
            wait(lambda f:f.crop((fx+175,fy+95,fx+810,fy+400)).tobytes()!=listing,'File preview did not open')
            frame('file-preview')
            click(fx+820-23,fy+20)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Closing top window did not reveal Terminal')
            click(x+ww-23,y+20)
            wait(lambda f:f.getpixel((x+20,y+1))!=(81,91,98),'Terminal close failed')
            type_text('echo ghost\n')
            click(sx+74,h-28)
            f=wait(lambda f:'desktop-ok' in terminal(f,box),'Terminal reopen failed')
            assert 'ghost' not in terminal(f,box),'Desktop keys leaked into terminal'
            click(x+ww-23,y+20)
            click(sx+178,h-28)
            ax=min((w-720)//2+52,w-720); ay=min((h-56-530)//2+36,h-56-530)
            wait(lambda f:f.getpixel((ax+20,ay+1))==(81,91,98),'System window did not open')
            frame('system')
            click(ax+720-23,ay+20)
            click(w-92,h-28)
            wait(lambda f:f.getpixel((w-356,h-56-456+110))==(16,18,20),'Calendar did not open')
            frame('calendar')
            tap('esc')
            tap('meta_l')
            wait(lambda f:f.getpixel((mx+20,my+90))==(16,18,20),'Windows key did not open Start')
            tap('esc')
            print(f'PASS {mode}: multiple windows, file navigation/preview, close/reopen, focus, calendar, Windows/Esc keys.',flush=True)
            click(w-240,h-28)
            nx=min((w-760)//2+104,w-760); ny=min((h-56-540)//2+72,h-56-540)
            wait(lambda f:f.getpixel((nx+20,ny+1))==(81,91,98),'Network window did not open')
            frame('network')
            click(nx+380,ny+540-95)
            click(sx+74,h-28); type_text('clear\nnet\n')
            wait(lambda f:'network: disabled' in terminal(f,box),'Network window disconnect failed')
            click(sx+282,h-28); click(nx+380,ny+540-95)
            click(nx+760-23,ny+20)
            type_text('clear\necho desktop-ok\n')
            click(box[0]+box[2]-23,box[1]+20)
            print(f'PASS {mode}: Network tray, window, disconnect/reconnect controls.',flush=True)
            if server:
                origin=f'http://10.0.2.2:{server.server_port}'
                click(sx+74,h-28)
                type_text('clear\nnet\n')
                wait(lambda f:'adapter: rtl8139' in terminal(f,box) and 'network: host-ready' in terminal(f,box),'NIC/ARP gateway failed')
                def page_status(title):
                    click(sx+74,h-28) # focus Terminal after the browser interaction
                    for attempt in range(5):
                        type_text('clear\nweb status\n'); time.sleep(.15)
                        result=terminal(frame(),box)
                        if f'title: {title}' in result and 'web state: 4' in result: return result
                    frame('network-failure')
                    raise AssertionError(f'Page did not complete: {title}\n{result}')
                def navigate(url):
                    click(sx+230,h-28)
                    key('ctrl',True); tap('l'); key('ctrl',False)
                    type_text(url+'\n')
                type_text('web '+origin+'/\n')
                wait(lambda f:'/' in requests,'HTTP GET never reached host fixture')
                result=page_status('Native test'); assert 'network-ok' in result
                click(sx+230,h-28); frame('browser')
                bx=min((w-1040)//2+78,w-1040); by=min((h-56-680)//2+54,h-56-680)
                click(bx+54,by+43+108+52+10)
                wait(lambda f:'/next' in requests,'Clickable HTML link did not navigate')
                assert 'chunk-ok' in page_status('Linked page')
                navigate(origin+'/redirect')
                wait(lambda f:'/redirect' in requests,'Address bar navigation failed')
                page_status('Linked page')
                navigate(origin+'/large')
                wait(lambda f:'/large' in requests,'Large response request missing')
                assert 'large-ok' in page_status('Large page')
                click(sx+230,h-28); tap('pgdn'); frame('browser-scrolled')
                # A network wait must not freeze the clock or window manager.
                key('ctrl',True); tap('l'); key('ctrl',False); type_text(origin+'/delay\n')
                before=frame().crop((w-170,h-52,w-30,h-28)).tobytes()
                wait(lambda f:f.crop((w-170,h-52,w-30,h-28)).tobytes()!=before,'Network wait froze the UI',4)
                page_status('Native test')
                navigate('https://example.com/')
                click(sx+74,h-28); type_text('clear\nweb status\n')
                wait(lambda f:'HTTPS bu sürümde desteklenmiyor' in terminal(f,box),'HTTPS restriction was not explicit')
                type_text('clear\nnet off\nweb '+origin+'/\n')
                click(sx+74,h-28); type_text('clear\nweb status\n')
                wait(lambda f:'web state: 5' in terminal(f,box),'Disabled network did not fail cleanly')
                type_text('net on\nweb '+origin+'/\n'); page_status('Native test')
                if args.internet:
                    navigate('http://example.com/')
                    page_status('Example Domain')
                    print(f'PASS {mode}: public DNS and HTTP (example.com).',flush=True)
                click(sx+230,h-28); frame('browser-final')
                click(bx+1040-23,by+20)
                # Preserve the fixture-free benchmark setup below.
                type_text('clear\necho desktop-ok\n')
                click(box[0]+box[2]-23,box[1]+20)
                print(f'PASS {mode}: native RTL8139, ARP/TCP, real HTTP, clickable links, chunks, redirect, RX wrap, scrolling, live UI, disconnect/reconnect.',flush=True)
            # Measure the real guest compositor under a pointer workload. The
            # counter includes terminal/clock updates; it is not display FPS.
            click(sx+74,h-28)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Benchmark terminal did not reopen')
            move_to(w-100,100)
            type_text('clear\nperf reset\n')
            started=time.monotonic()
            for i in range(120):
                qmp('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':8 if i%2 else -8}}])
                time.sleep(1/120)
            elapsed=time.monotonic()-started
            type_text('perf\n')
            f=wait(lambda f:'Present limit: 120 Hz' in terminal(f,box),'Performance counters unavailable')
            report=terminal(f,box)
            metrics={name:int(value) for name,value in re.findall(r'^(frames|pixels|avg pixels/frame|avg compose us|max compose us): (\d+)$',report,re.M)}
            assert metrics.get('frames',0)>5,'Pointer frames were not presented'
            assert metrics['avg pixels/frame']<w*h//4,'Pointer workload still redraws the full screen'
            metrics.update(width=w,height=h,input_events=120,input_seconds=round(elapsed,3))
            (OUT/f'{mode}-performance.json').write_text(json.dumps(metrics,indent=2)+'\n')
            frame('performance')
            print(f'PASS {mode}: pointer workload: {metrics}',flush=True)
            type_text('clear\nperf reset\n')
            x,y,ww,hh=box
            move_to(x+180,y+20); button(True)
            for i in range(120):
                qmp('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':8 if i%2 else -8}}])
                time.sleep(1/120)
            button(False)
            move_to(w-100,100)
            type_text('perf\n')
            f=wait(lambda f:'Present limit: 120 Hz' in terminal(f,box),'Rapid drag lost final window position or text')
            report=terminal(f,box)
            drag={name:int(value) for name,value in re.findall(r'^(frames|pixels|avg pixels/frame|avg compose us|max compose us): (\d+)$',report,re.M)}
            assert drag.get('frames',0)>5,'Drag frames were not presented'
            assert drag['avg pixels/frame']<w*h*3//4,'Dragging still redraws the full screen'
            metrics['drag']=drag
            (OUT/f'{mode}-performance.json').write_text(json.dumps(metrics,indent=2)+'\n')
            frame('drag-performance')
            print(f'PASS {mode}: rapid window drag: {drag}',flush=True)
        finally:
            if proc.poll() is None:
                try: qmp('quit'); proc.wait(timeout=5)
                finally:
                    if proc.poll() is None: proc.kill(); proc.wait()
            if server: server.shutdown(); server.server_close()

if __name__=='__main__': main()
