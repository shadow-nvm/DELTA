import subprocess, time, os, socket, sys
qemu = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
try:
    from PIL import Image
except Exception as e:
    print("PIL missing", e); sys.exit(1)
args = [qemu, "-m", "256M", "-drive", "format=raw,file=build/images/delta.img", "-boot", "c", "-display", "none", "-vga", "std", "-serial", "tcp:127.0.0.1:4563,server,nowait", "-monitor", "tcp:127.0.0.1:4562,server,nowait", "-no-reboot"]
p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=os.getcwd())
time.sleep(2.0)
s=None
try:
    s=socket.create_connection(("127.0.0.1",4563),timeout=3); s.settimeout(0.5)
    for cmd in [b"help\r", b"info\r", b"dmesg\r"]:
        s.sendall(cmd); time.sleep(0.8)
    time.sleep(1.0)
    m=socket.create_connection(("127.0.0.1",4562),timeout=3); m.settimeout(1)
    time.sleep(0.2)
    m.sendall(b"screendump build/screen.ppm\n"); time.sleep(1.0)
    data=b""
    try:
        while True: data+=m.recv(65536)
    except socket.timeout: pass
    print(data.decode(errors="replace")[-500:])
    m.sendall(b"quit\n")
    m.close()
except Exception as e:
    print("err",repr(e))
finally:
    if s: s.close()
    if p.poll() is None: p.kill()
    p.communicate()
img = Image.open("build/screen.ppm")
print("raw", img.size, img.mode)
# The VGA text screen has a large black border in QEMU's 720x400 capture.
# Scale 2x with nearest-neighbour for a crisp documentation image.
img = img.convert("RGB").resize((img.width*2, img.height*2), Image.NEAREST)
img.save("build/delta-screen.png")
print("saved", img.size)
