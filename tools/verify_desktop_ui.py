import json, os, select, subprocess, time
from PIL import Image, ImageFile
ImageFile.LOAD_TRUNCATED_IMAGES = True

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

def send_key(code):
    qmp("input-send-event", events=[
        {"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": code}}},
        {"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": code}}}
    ])

def move_mouse(x, y):
    # Absolute mouse coordinates [0, 0x7fff]
    ax = int(x * 0x7fff / 1920)
    ay = int(y * 0x7fff / 1080)
    qmp("input-send-event", events=[
        {"type": "abs", "data": {"axis": "x", "value": ax}},
        {"type": "abs", "data": {"axis": "y", "value": ay}}
    ])

def click_mouse(x, y):
    move_mouse(x, y)
    time.sleep(0.05)
    qmp("input-send-event", events=[
        {"type": "btn", "data": {"down": True, "button": "left"}},
        {"type": "btn", "data": {"down": False, "button": "left"}}
    ])
    time.sleep(0.1)

def save_frame(name):
    ppm_path = f"{OUT}/temp_{name}.ppm"
    qmp("screendump", filename=ppm_path)
    time.sleep(0.2)
    with Image.open(ppm_path) as im:
        png_path = f"{OUT}/{name}.png"
        im.save(png_path)
        print(f"Saved {png_path} ({im.size[0]}x{im.size[1]})")
        return im

print("Waiting for boot splash to appear in Full HD...")
for attempt in range(40):
    time.sleep(0.5)
    ppm_path = f"{OUT}/temp_check.ppm"
    qmp("screendump", filename=ppm_path)
    try:
        with Image.open(ppm_path) as im:
            if im.size == (1920, 1080) and im.getpixel((960, 465))[0] > 80:
                print(f"Boot splash detected at attempt {attempt}!")
                break
    except Exception as e:
        pass

# Send Enter to skip boot splash
print("Skipping boot splash with Enter...")
send_key("ret")
time.sleep(1.0)
send_key("ret")
time.sleep(2.0)

# Wait until desktop is loaded (top bar rendered)
print("Waiting for desktop to initialize...")
for attempt in range(40):
    time.sleep(0.5)
    ppm_path = f"{OUT}/temp_check.ppm"
    qmp("screendump", filename=ppm_path)
    try:
        with Image.open(ppm_path) as im:
            top_box = im.crop((0, 0, im.width, 28)).getbbox()
            if top_box is not None:
                print(f"Desktop rendered at attempt {attempt}! Top bar bbox: {top_box}")
                break
    except Exception as e:
        pass

print("Capturing Windows Desktop...")
save_frame("final_windows_desktop")

print("Toggling Start Menu with Super / Windows key...")
send_key("meta_l")
time.sleep(1.0)
save_frame("final_windows_start_menu")

print("All screenshots captured successfully!")
proc.terminate()
