import subprocess, time, os, socket
qemu = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
args = [qemu, "-m", "256M", "-drive", "format=raw,file=build/images/delta.img", "-boot", "c", "-display", "none", "-serial", "tcp:127.0.0.1:4555,server,nowait", "-monitor", "none", "-no-reboot"]
p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=os.getcwd())
time.sleep(2.0)
s = None
try:
    s = socket.create_connection(("127.0.0.1", 4555), timeout=3)
    s.settimeout(1)
    time.sleep(0.5)
    for cmd in [b"help\r", b"info\r", b"echo hello-delta\r"]:
        s.sendall(cmd); time.sleep(0.8)
    time.sleep(1.5)
    data=b""
    try:
        while True:
            b=s.recv(65536)
            if not b: break
            data+=b
    except socket.timeout:
        pass
    print(data.decode(errors="replace"))
finally:
    if s: s.close()
    if p.poll() is None: p.kill()
    p.communicate()
