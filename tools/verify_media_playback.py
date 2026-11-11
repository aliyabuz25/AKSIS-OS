import json, os, select, subprocess, time
from PIL import Image

OUT = "build/out"
FW = "/Applications/UTM.app/Contents/Resources/qemu"
ISO = os.path.abspath(f"{OUT}/shenlong.iso")

cmd = [
    f"{OUT}/utm-qemu-runner",
    "-L", FW,
    "-machine", "q35,i8042=on",
    "-accel", "tcg",
    "-cpu", "max",
    "-m", "256M",
    "-smp", "1",
    "-vga", "virtio",
    "-display", "none",
    "-serial", "none",
    "-qmp", "stdio",
    "-no-reboot",
    "-cdrom", ISO,
    "-boot", "d",
    "-netdev", "user,id=hostnet",
    "-device", "rtl8139,netdev=hostnet,mac=52:54:00:53:48:05",
    "-audiodev", f"wav,id=audio,path={OUT}/bieber-audio.wav",
    "-device", "AC97,audiodev=audio",
    "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={FW}/edk2-x86_64-code.fd",
    "-drive", f"if=pflash,format=qcow2,unit=1,snapshot=on,file={FW}/edk2-i386-vars.fd"
]

print("Launching QEMU runner...")
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)

def receive():
    data = bytearray()
    while not data.endswith(b'\n'):
        if not select.select([proc.stdout], [], [], 20)[0]:
            raise TimeoutError("QMP timeout")
        b = proc.stdout.read(1)
        if not b:
            raise RuntimeError("QEMU exited")
        data.extend(b)
    return json.loads(data)

def qmp(command, **arguments):
    msg = json.dumps({"execute": command, "arguments": arguments}) + "\n"
    proc.stdin.write(msg.encode())
    while True:
        rep = receive()
        if "error" in rep:
            raise RuntimeError(rep)
        if "return" in rep:
            return rep["return"]

receive()
qmp("qmp_capabilities")

def frame():
    ppm_path = f"{OUT}/temp_capture.ppm"
    qmp("screendump", filename=ppm_path)
    time.sleep(0.08)
    return Image.open(ppm_path)

def mouse_pos():
    for _ in range(15):
        im = frame()
        pixels = im.tobytes()
        w, h = im.size
        pts = [i // 3 for i in range(0, len(pixels), 3) if pixels[i:i+3] == b'\xf0\xf0\xf0']
        if pts:
            return min(p % w for p in pts), min(p // w for p in pts)
        time.sleep(0.1)
    return 960, 540

def move(dx, dy):
    while dx or dy:
        sx = max(-80, min(80, dx))
        sy = max(-80, min(80, dy))
        qmp('input-send-event', events=[
            {'type': 'rel', 'data': {'axis': 'x', 'value': sx}},
            {'type': 'rel', 'data': {'axis': 'y', 'value': sy}}
        ])
        dx -= sx
        dy -= sy
        time.sleep(0.035)

def move_to(x, y):
    for _ in range(12):
        mx, my = mouse_pos()
        if abs(x - mx) <= 6 and abs(y - my) <= 6:
            break
        move(x - mx, y - my)
        time.sleep(0.08)
    print(f"Moved mouse to ({mx}, {my}), target was ({x}, {y})")

def button(down):
    qmp('input-send-event', events=[{'type': 'btn', 'data': {'down': down, 'button': 'left'}}])
    time.sleep(0.15)

def click(x, y):
    move_to(x, y)
    button(True)
    button(False)
    time.sleep(0.2)

def tap(code):
    qmp('input-send-event', events=[{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': code}}}])
    time.sleep(0.035)
    qmp('input-send-event', events=[{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': code}}}])
    time.sleep(0.04)

print("Waiting for boot splash...")
for attempt in range(40):
    time.sleep(0.5)
    try:
        im = frame()
        if im.size == (1920, 1080) and im.getpixel((960, 540)) != (0, 0, 0):
            print(f"Boot splash detected at attempt {attempt}!")
            break
    except Exception:
        pass

# Skip splash
tap("ret")
time.sleep(1.0)
tap("ret")
time.sleep(1.5)

print("Waiting for desktop...")
for attempt in range(30):
    time.sleep(0.5)
    try:
        im = frame()
        c = im.getpixel((1500, 1055))
        if c != (0, 0, 0):
            print(f"Desktop rendered! Dock color at (1500, 1055): {c}")
            break
    except Exception:
        pass

frame().save(f"{OUT}/desktop_ready.png")

# Click Medya app on the dock (x=1677, y=1053)
print("Clicking Medya icon on dock at (1677, 1053)...")
click(1677, 1053)
time.sleep(1.5)

im1 = frame()
im1.save(f"{OUT}/bieber_video_frame1.png")
print("Saved bieber_video_frame1.png")

time.sleep(2.0)
im2 = frame()
im2.save(f"{OUT}/bieber_video_frame2.png")
print("Saved bieber_video_frame2.png")

# Crop the media player window: x in [610, 1570], y in [266, 946]
crop1 = im1.crop((610, 266, 1570, 946))
crop2 = im2.crop((610, 266, 1570, 946))
crop1.save(f"{OUT}/crop_bieber_window1.png")
crop2.save(f"{OUT}/crop_bieber_window2.png")
print("Saved crop_bieber_window1.png and crop_bieber_window2.png")

diff = sum(abs(a - b) for a, b in zip(crop1.tobytes(), crop2.tobytes()))
print(f"Video window byte diff between 2.0 seconds: {diff}")
if diff > 1000:
    print("SUCCESS: Justin Bieber - Off My Face is actively playing!")
else:
    print(f"Frame diff is {diff}")

wav_path = f"{OUT}/bieber-audio.wav"
if os.path.exists(wav_path):
    print(f"AC97 audio WAV file size: {os.path.getsize(wav_path)} bytes")

proc.terminate()
