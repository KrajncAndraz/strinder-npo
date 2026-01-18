import socket

HOST = "0.0.0.0"
PORT = 5000

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind((HOST, PORT))
s.listen(1)
print("Listening on", PORT)

conn, addr = s.accept()
print("Connected by", addr)

buf = b""
while True:
    data = conn.recv(1024)
    if not data:
        break
    buf += data
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        print(line.decode(errors="ignore"))
