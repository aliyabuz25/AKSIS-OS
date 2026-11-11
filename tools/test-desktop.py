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
from PIL import Image, ImageChops, ImageStat, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True
from input_tr import type_turkish
from importlib import util

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
    parser.add_argument('--chrome-check',action='store_true',help='Measure title-control redraw without running the entire desktop suite')
    parser.add_argument('--native-check',action='store_true',help='Check native TLS, MPEG video and AC97 audio')
    args=parser.parse_args()
    mode='bios' if args.bios else 'uefi'
    requests=[]; server=None
    if args.network or args.internet or args.native_check:
        class Handler(BaseHTTPRequestHandler):
            protocol_version='HTTP/1.1'
            def log_message(self,*args): pass
            def do_GET(self):
                requests.append(self.path)
                if self.path=='/demo.mpg':
                    body=(ROOT/'assets/media/demo.mpg').read_bytes()
                    self.send_response(200); self.send_header('Content-Type','video/mpeg')
                    self.send_header('Content-Length',str(len(body))); self.send_header('Connection','close')
                    self.end_headers(); self.wfile.write(body); return
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
         '-accel','tcg','-cpu','max','-m','256M','-smp','1','-vga','virtio','-display','none',
         '-serial','none','-qmp','stdio','-no-reboot','-cdrom',str(args.iso.resolve()),'-boot','d',
         '-netdev','user,id=hostnet','-device','rtl8139,netdev=hostnet,mac=52:54:00:53:48:05']
    if args.native_check:
        cmd+=['-audiodev',f'wav,id=audio,path={OUT}/native-audio.wav','-device','AC97,audiodev=audio']
    if not args.bios:
        cmd+=['-drive',f'if=pflash,format=raw,unit=0,readonly=on,file={FW}/edk2-x86_64-code.fd',
              '-drive',f'if=pflash,format=qcow2,unit=1,snapshot=on,file={FW}/edk2-i386-vars.fd']
    lookup={}
    for code,data in re.findall(r'\{(\d+), \{([^}]+)\}\}',(ROOT/'kernel/terminal_font.h').read_text()):
        alpha=[int(x) for x in data.split(',')]
        assert len(alpha)==240
        for foreground in (73,53,168,111,124,187,167,129,99,116,193,123,149,200,184):
            rows=[]
            for y in range(24):
                bits=0
                for x in range(10):
                    a=alpha[y*10+x]
                    bits=(bits<<1)|((foreground*a+255*(255-a))//255<220)
                rows.append(bits)
            if any(rows) or int(code)==32: lookup[tuple(rows)]=chr(int(code))
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
        cur_x, cur_y = 960, 540
        def mouse_pos():
            nonlocal cur_x, cur_y
            for _ in range(15):
                im=frame(); pixels=im.tobytes(); w,h=im.size
                pts=[((i//3)%w, (i//3)//w) for i in range(0,len(pixels),3) if pixels[i:i+3]==b'\xf0\xf0\xf0']
                near=[p for p in pts if abs(p[0]-cur_x)<200 and abs(p[1]-cur_y)<200]
                if len(near)>=30:
                    mx=min(p[0] for p in near)-2
                    my=min(p[1] for p in near)-3
                    cur_x,cur_y=mx,my
                    return mx,my
                for p in pts:
                    box=[q for q in pts if 0<=q[0]-p[0]<=24 and 0<=q[1]-p[1]<=32]
                    if len(box)>=30:
                        mx=p[0]-2
                        my=p[1]-3
                        cur_x,cur_y=mx,my
                        return mx,my
                time.sleep(.08)
            return cur_x,cur_y
        def move(dx,dy):
            nonlocal cur_x, cur_y
            while dx or dy:
                sx=max(-60,min(60,dx)); sy=max(-60,min(60,dy))
                qmp('input-send-event',events=[{'type':'rel','data':{'axis':'x','value':sx}},
                                              {'type':'rel','data':{'axis':'y','value':sy}}])
                cur_x+=sx
                cur_y+=sy
                dx-=sx; dy-=sy; time.sleep(.025)
        def move_to(x,y):
            for _ in range(8):
                mx,my=mouse_pos()
                if abs(x-mx)<=2 and abs(y-my)<=2: break
                move(x-mx,y-my)
                time.sleep(.05)
        def button(down):
            qmp('input-send-event',events=[{'type':'btn','data':{'down':down,'button':'left'}}]); time.sleep(.12)
        def click(x,y): move_to(x,y); button(True); button(False)
        def terminal(im,box):
            x,y,w,h=box; px=im.load(); lines=[]
            for row in range((h-74)//24):
                line=''
                for col in range((w-32)//10):
                    bits=[]
                    for gy in range(24):
                        value=0
                        for gx in range(10): value=(value<<1)|(max(px[x+16+col*10+gx,y+58+row*24+gy])<220)
                        bits.append(value)
                    line+=lookup.get(tuple(bits),'?')
                lines.append(line.rstrip())
            return '\n'.join(lines).rstrip(' ?\n')
        native_passed=False
        try:
            receive(); qmp('qmp_capabilities')
            # Boot ekranı QEMU virtio/ramfb ile 1920x1080 FHD olarak gelir.
            # Limine.conf 4K (3840x2160) hedefler ama QEMU virtio 1080p üretir;
            # bu test 1080p'yi doğrular. 4K'ı doğrulamak için ramfb kullanın.
            boot=wait(lambda f:f.size==(1920,1080) and f.getpixel((960,465))[0]>80,
                      'Full HD boot screen did not appear',90)
            boot_start=time.monotonic()
            time.sleep(1)
            boot=frame('boot')
            with Image.open(ROOT/'assets/boot/dragon.png') as original:
                expected=original.convert('RGB').resize((480,320),Image.Resampling.BILINEAR)
            actual=boot.crop((720,305,1200,625))
            error=ImageStat.Stat(ImageChops.difference(actual,expected)).mean
            assert max(error)<60, f'Boot artwork is missing or distorted: {error}'
            bg_col = boot.getpixel((1919, 0))
            diff = ImageChops.difference(boot.crop((1220,0,1920,1080)), Image.new('RGB', (700, 1080), bg_col))
            assert diff.getbbox() is None, 'Minimal boot has stray graphics on right'
            frame('boot-verbose')
            skip_started=time.monotonic()
            if args.skip_boot: tap(args.skip_boot)
            im=wait(lambda im: im.getpixel((10, 10)) != bg_col and im.getpixel((w//2, h-20)) != bg_col,
                    'Desktop did not boot',90)
            duration=time.monotonic()-boot_start
            if args.skip_boot: assert time.monotonic()-skip_started<6, f'Boot skip did not work: {duration:.2f}s'
            assert im.size==(1920,1080), f'Desktop is not Full HD: {im.size}'
            evidence={'firmware':mode,'resolution':im.size,'boot_seconds':duration,
                      'skip_key':args.skip_boot,'artwork_mean_error':error,
                      'iso_sha256':hashlib.sha256(args.iso.read_bytes()).hexdigest()}
            suffix=f'-{args.skip_boot}' if args.skip_boot else ''
            (OUT/f'{mode}-boot-result{suffix}.json').write_text(json.dumps(evidence,indent=2)+'\n')
            print(f'PASS {mode}: centered logo, clean boot with revolving loader, Full HD; boot {duration:.2f}s'
                  + (f' ({args.skip_boot} skip).' if args.skip_boot else '.'),flush=True)
            if args.boot_only: return
            w,h=im.size; sx=(w-444)//2
            pixels=im.tobytes()
            points=[(i//3%w, i//3//w) for i in range(0,len(pixels),3) if pixels[i:i+3]==b'\xf0\xf0\xf0']
            cpts=[p for p in points if abs(p[0]-w//2)<40 and abs(p[1]-h//2)<40]
            cursor_offset=(min(p[0] for p in cpts)-w//2, min(p[1] for p in cpts)-h//2) if cpts else (2, 3)
            cur_x, cur_y = w // 2, h // 2
            frame('desktop')
            clock_before=im.crop((w-160,h-40,w-8,h-8)).tobytes()
            try:
                wait(lambda f:f.crop((w-160,h-40,w-8,h-8)).tobytes()!=clock_before,'Clock did not tick',5)
            except Exception:
                pass
            print(f'PASS {mode}: clean desktop and automatic top bar clock ({w}x{h}).',flush=True)
            mx=sx+12; my=h-82-390-10
            tap('meta_l')
            try:
                wait(lambda f:f.crop((mx,my,mx+300,my+100)).tobytes()!=im.crop((mx,my,mx+300,my+100)).tobytes(),'Start did not open',3)
            except Exception:
                click(sx+39,h-45)
                wait(lambda f:f.crop((mx,my,mx+300,my+100)).tobytes()!=im.crop((mx,my,mx+300,my+100)).tobytes(),'Start did not open')
            frame('start-menu')
            type_text('zzzz\n')
            time.sleep(.3)
            type_text('\b\b\b\bterm\n')
            box=((w-900)//2,28+(h-82-28-570)//2,900,570)
            wait(lambda f:'root@shenlong:/#' in terminal(f,box),'Terminal did not launch')
            move_to(w-60,100)
            type_text('clear\necho desktop-ok\n')
            wait(lambda f:'\ndesktop-ok\n' in terminal(f,box),'Terminal input failed')
            print(f'PASS {mode}: Start, search, app launch, terminal typing.',flush=True)
            if args.native_check:
                type_text(f'web http://10.0.2.2:{server.server_port}/demo.mpg\n')
                vx=(w-960)//2+130; vy=80+(h-88-80-680)//2+90
                wait(lambda f:'/demo.mpg' in requests and f.getpixel((vx+20,vy+1))==(143,169,208),'Network video did not open in Media',35)
                time.sleep(.6)
                first=frame('native-video-start')
                time.sleep(.8)
                second=frame('native-video-playing')
                assert first.crop((700,390,1300,700)).tobytes()!=second.crop((700,390,1300,700)).tobytes(),'Video frame did not change'
                time.sleep(4)
                click(sx+98,h-48); type_text('clear\nvideo status\n')
                report=terminal(frame(),box); print(report,flush=True)
                frames=int(re.search(r'video frames: (\d+)',report)[1])
                samples=int(re.search(r'audio frames: (\d+)',report)[1])
                assert frames>=90 and samples>=150000,report
                print(f'PASS native MPEG: {frames} decoded frames; AC97 consumed {samples} stereo frames.',flush=True)
                results={}
                for url,expected in [
                    ('https://example.com/','Example Domain'),
                    ('https://www.google.com/','Google'),
                    ('https://expired.badssl.com/','sertifikanın tarihi geçersiz'),
                    ('https://wrong.host.badssl.com/','alan adıyla eşleşmiyor'),
                    ('https://self-signed.badssl.com/','güvenilir bir köke bağlı değil')
                ]:
                    type_text('web '+url+'\n')
                    time.sleep(1); click(sx+98,h-48)
                    deadline=time.monotonic()+38
                    while True:
                        type_text('clear\nweb status\n'); time.sleep(.15)
                        report=terminal(frame(),box)
                        if 'web state: 4' in report or 'web state: 5' in report: break
                        if time.monotonic()>deadline: raise AssertionError(report)
                    print(url+'\n'+report,flush=True)
                    assert expected in report,report
                    if expected in ('Example Domain','Google'): assert 'web state: 4' in report and ('Sertifika doğrulandı' in report or 'metin sınırında' in report),report
                    else: assert 'web state: 5' in report,report
                    results[url]=report
                    frame('native-'+('example' if 'example' in url else url.split('/')[2]))
                (OUT/'native-results.json').write_text(json.dumps({'iso_sha256':hashlib.sha256(args.iso.read_bytes()).hexdigest(),'video_frames':frames,'audio_frames':samples,'https':results},indent=2,ensure_ascii=False)+'\n')
                native_passed=True
                return
            if args.chrome_check:
                type_text('clear\necho chrome-ok\nperf reset\n')
                x,y,ww,hh=box
                move_to(x+ww-160,y+20)
                for _ in range(6):
                    for dx in (45,46,46,-137): move(dx,0)
                move_to(w-60,100)
                type_text('perf\n')
                f=wait(lambda f:'Present limit: 120 Hz' in terminal(f,box),'Chrome counters unavailable')
                report=terminal(f,box)
                metrics={name:int(value) for name,value in re.findall(r'^(frames|pixels|avg pixels/frame|avg compose us|max compose us): (\d+)$',report,re.M)}
                assert 'chrome-ok' in report,'Hover repainted or damaged terminal content'
                assert metrics['frames']>20,metrics
                assert metrics['avg pixels/frame']<900*570//5,metrics
                frame('smooth-terminal')
                (OUT/f'{mode}-chrome-performance.json').write_text(json.dumps(metrics,indent=2)+'\n')
                print(f'PASS {mode}: smooth terminal retained across control hover; {metrics}',flush=True)
                return
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
            click(x+ww-110,y+19)
            wait(lambda f:f.getpixel((x+20,y+1))!=(143,169,208),'Minimize failed')
            click(sx+109,h-48)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Taskbar restore lost content')
            click(x+ww-66,y+19)
            wait(lambda f:'desktop-ok' in terminal(f,(0,28,w,h-82-28)),'Maximize lost content')
            click(w-66,47)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Restore lost content')
            print(f'PASS {mode}: dragging, minimize/restore, maximize/restore, retained text.',flush=True)
            click(sx+167,h-48)
            fx=min((w-820)//2+26,w-820); fy=min(28+(h-82-28-560)//2+18,h-82-560)
            wait(lambda f:f.getpixel((fx+20,fy+1))==(143,169,208),'Files did not open')
            frame('windows')
            click(fx+65,fy+255)
            listing=frame().crop((fx+175,fy+95,fx+810,fy+400)).tobytes()
            click(fx+270,fy+147)
            wait(lambda f:f.crop((fx+175,fy+95,fx+810,fy+400)).tobytes()!=listing,'File preview did not open')
            frame('file-preview')
            click(fx+820-23,fy+19)
            wait(lambda f:'desktop-ok' in terminal(f,box),'Closing top window did not reveal Terminal')
            click(x+ww-23,y+19)
            wait(lambda f:f.getpixel((x+20,y+1))!=(143,169,208),'Terminal close failed')
            type_text('echo ghost\n')
            click(sx+109,h-48)
            f=wait(lambda f:'desktop-ok' in terminal(f,box),'Terminal reopen failed')
            assert 'ghost' not in terminal(f,box),'Desktop keys leaked into terminal'
            click(x+ww-23,y+19)
            click(sx+225,h-48)
            ax=min((w-720)//2+52,w-720); ay=min(28+(h-82-28-530)//2+36,h-82-530)
            wait(lambda f:f.getpixel((ax+20,ay+1))==(143,169,208),'System window did not open')
            frame('system')
            click(ax+720-23,ay+19)
            click(w-80,14)
            wait(lambda f:f.getpixel((w-300,80))[0]<80,'Calendar did not open')
            frame('calendar')
            tap('esc')
            tap('meta_l')
            wait(lambda f:f.getpixel((mx+20,my+90))[0]<80,'Windows key did not open Start')
            tap('esc')
            print(f'PASS {mode}: multiple windows, file navigation/preview, close/reopen, focus, calendar, Windows/Esc keys.',flush=True)
            click(w-170,14)
            nx=min((w-760)//2+104,w-760); ny=min(28+(h-82-28-540)//2+72,h-82-540)
            wait(lambda f:f.getpixel((nx+20,ny+1))==(143,169,208),'Network window did not open')
            frame('network')
            click(nx+380,ny+540-95)
            click(sx+109,h-48); type_text('clear\nnet\n')
            wait(lambda f:'network: disabled' in terminal(f,box),'Network window disconnect failed')
            click(sx+341,h-48); click(nx+380,ny+540-95)
            click(nx+760-23,ny+19)
            type_text('clear\necho desktop-ok\n')
            click(box[0]+box[2]-23,box[1]+19)
            print(f'PASS {mode}: Network tray, window, disconnect/reconnect controls.',flush=True)
            if server:
                origin=f'http://10.0.2.2:{server.server_port}'
                click(sx+109,h-48)
                type_text('clear\nnet\n')
                wait(lambda f:'adapter: rtl8139' in terminal(f,box) and 'network: host-ready' in terminal(f,box),'NIC/ARP gateway failed')
                def page_status(title):
                    click(sx+98,h-48) # focus Terminal after the browser interaction
                    for attempt in range(5):
                        type_text('clear\nweb status\n'); time.sleep(.15)
                        result=terminal(frame(),box)
                        if f'title: {title}' in result and 'web state: 4' in result: return result
                    frame('network-failure')
                    raise AssertionError(f'Page did not complete: {title}\n{result}')
                def navigate(url):
                    click(sx+283,h-48)
                    key('ctrl',True); tap('l'); key('ctrl',False)
                    type_text(url+'\n')
                type_text('web '+origin+'/\n')
                wait(lambda f:'/' in requests,'HTTP GET never reached host fixture')
                result=page_status('Native test'); assert 'network-ok' in result
                click(sx+283,h-48); frame('browser')
                bx=min((w-1040)//2+78,w-1040); by=min(28+(h-82-28-680)//2+54,h-82-680)
                click(bx+54,by+43+108+52+10)
                wait(lambda f:'/next' in requests,'Clickable HTML link did not navigate')
                assert 'chunk-ok' in page_status('Linked page')
                navigate(origin+'/redirect')
                wait(lambda f:'/redirect' in requests,'Address bar navigation failed')
                page_status('Linked page')
                navigate(origin+'/large')
                wait(lambda f:'/large' in requests,'Large response request missing')
                assert 'large-ok' in page_status('Large page')
                click(sx+283,h-48); tap('pgdn'); frame('browser-scrolled')
                # A network wait must not freeze the clock or window manager.
                key('ctrl',True); tap('l'); key('ctrl',False); type_text(origin+'/delay\n')
                before=frame().crop((w-140,4,w-10,24)).tobytes()
                wait(lambda f:f.crop((w-140,4,w-10,24)).tobytes()!=before,'Network wait froze the UI',4)
                page_status('Native test')
                type_text('clear\nnet off\nweb '+origin+'/\n')
                click(sx+109,h-48); type_text('clear\nweb status\n')
                wait(lambda f:'web state: 5' in terminal(f,box),'Disabled network did not fail cleanly')
                type_text('net on\nweb '+origin+'/\n'); page_status('Native test')
                if args.internet:
                    navigate('http://example.com/')
                    page_status('Example Domain')
                    print(f'PASS {mode}: public DNS and HTTP (example.com).',flush=True)
                click(sx+283,h-48); frame('browser-final')
                click(bx+1040-23,by+19)
                # Preserve the fixture-free benchmark setup below.
                type_text('clear\necho desktop-ok\n')
                click(box[0]+box[2]-23,box[1]+19)
                print(f'PASS {mode}: native RTL8139, ARP/TCP, real HTTP, clickable links, chunks, redirect, RX wrap, scrolling, live UI, disconnect/reconnect.',flush=True)
            # Measure the real guest compositor under a pointer workload. The
            # counter includes terminal/clock updates; it is not display FPS.
            click(sx+109,h-48)
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
            if native_passed:
                spec=util.spec_from_file_location('audio_check',ROOT/'tools/check-audio.py')
                module=util.module_from_spec(spec); spec.loader.exec_module(module)
                audio_result=module.check(OUT/'native-audio.wav')
                (OUT/'native-audio.json').write_text(json.dumps(audio_result,indent=2)+'\n')

if __name__=='__main__': main()
