import subprocess, time, os, socket
from PIL import Image
qemu = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
args = [qemu, "-m", "256M", "-drive", "format=raw,file=build/images/delta.img", "-boot", "c", "-display", "none", "-vga", "std", "-serial", "file:build/serial-bootshot.log", "-monitor", "tcp:127.0.0.1:4564,server,nowait", "-S"]
p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=os.getcwd())
time.sleep(0.5)
m=socket.create_connection(("127.0.0.1",4564),timeout=3); m.settimeout(0.4)
time.sleep(0.2)
m.sendall(b"cont\n")
for i,delay in enumerate([0.45,0.25,0.25,0.25]):
    time.sleep(delay)
    m.sendall(f"screendump build/boot{i}.ppm\n".encode())
    time.sleep(0.35)
m.sendall(b"quit\n")
try:
    while True: m.recv(65536)
except Exception: pass
m.close()
if p.poll() is None: p.kill()
p.communicate()
for i in range(4):
    img=Image.open(f"build/boot{i}.ppm")
    img.convert("RGB").resize((img.width*2,img.height*2),Image.NEAREST).save(f"build/boot{i}.png")
    print(i,img.size)
