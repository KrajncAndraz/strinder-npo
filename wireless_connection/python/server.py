import socket
import select
import time
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

HOST = "0.0.0.0"   
PORT = 5000

DISPLAY_SECONDS = 10
SAMPLE_RATE = 10   
MAX_POINTS = DISPLAY_SECONDS * SAMPLE_RATE

time_data = deque(maxlen=MAX_POINTS)
gyro_x_data = deque(maxlen=MAX_POINTS)
gyro_y_data = deque(maxlen=MAX_POINTS)
gyro_z_data = deque(maxlen=MAX_POINTS)
acc_x_data = deque(maxlen=MAX_POINTS)
acc_y_data = deque(maxlen=MAX_POINTS)
acc_z_data = deque(maxlen=MAX_POINTS)


server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((HOST, PORT))
server.listen(1)

print(f"Listening on {HOST}:{PORT} ...")
print("Waiting for ESP32 connection...")

conn, addr = server.accept()
conn.setblocking(False)  
print("Connected by", addr)
start_time = time.time()
sock_buf = b""  


plt.rcParams['toolbar'] = 'None'
fig, axs = plt.subplots(2, 3, figsize=(16, 9))

fig.suptitle('STM32F303 - Gyro in Acc meter [%] (zadnjih 10 sekund)',
             fontsize=16, fontweight='bold')


ax_gx = axs[0, 0]
ax_gy = axs[0, 1]
ax_gz = axs[0, 2]

ax_ax = axs[1, 0]
ax_ay = axs[1, 1]
ax_az = axs[1, 2]


ax_gx.set_title('Žiroskop X [%]', fontweight='bold', fontsize=12)
ax_gy.set_title('Žiroskop Y [%]', fontweight='bold', fontsize=12)
ax_gz.set_title('Žiroskop Z [%]', fontweight='bold', fontsize=12)
ax_ax.set_title('Pospeškometer X [%]', fontweight='bold', fontsize=12)
ax_ay.set_title('Pospeškometer Y [%]', fontweight='bold', fontsize=12)
ax_az.set_title('Pospeškometer Z [%]', fontweight='bold', fontsize=12)


ax_gx.set_ylabel('Rotacija [%]', fontsize=10)
ax_ax.set_ylabel('Pospešek [%]', fontsize=10)


for ax in axs.flat:
    ax.set_xlabel('Čas [s]', fontsize=10)
    ax.set_xlim(0, DISPLAY_SECONDS)
    ax.set_xticks([0, 2, 4, 6, 8, 10])
    ax.set_ylim(-100, 100)
    ax.set_yticks([-100, -75, -50, -25, 0, 25, 50, 75, 100])
    ax.axhline(0, color='black', linewidth=0.8, linestyle='--', alpha=0.5)
    ax.grid(True, alpha=0.3, linestyle='--')


line_gx, = ax_gx.plot([], [], 'r-', linewidth=2, label='Gyro X')
line_gy, = ax_gy.plot([], [], 'g-', linewidth=2, label='Gyro Y')
line_gz, = ax_gz.plot([], [], 'b-', linewidth=2, label='Gyro Z')
line_ax, = ax_ax.plot([], [], 'r-', linewidth=2, label='Acc X')
line_ay, = ax_ay.plot([], [], 'g-', linewidth=2, label='Acc Y')
line_az, = ax_az.plot([], [], 'b-', linewidth=2, label='Acc Z')

for ax in axs.flat:
    ax.legend(loc='upper right', fontsize=9)

def init():
    line_gx.set_data([], [])
    line_gy.set_data([], [])
    line_gz.set_data([], [])
    line_ax.set_data([], [])
    line_ay.set_data([], [])
    line_az.set_data([], [])
    return line_gx, line_gy, line_gz, line_ax, line_ay, line_az

def animate(frame):
    global sock_buf, conn

    try:
        ready, _, _ = select.select([conn], [], [], 0)
        if ready:
            data = conn.recv(1024)
            if not data:
                # connection closed
                print("Client disconnected.")
                return line_gx, line_gy, line_gz, line_ax, line_ay, line_az

            sock_buf += data

            while b"\n" in sock_buf:
                raw_line, sock_buf = sock_buf.split(b"\n", 1)

                line = raw_line.decode(errors="ignore").strip()

                values = line.split(',')
                if len(values) == 6:
                    try:
                        gx, gy, gz, ax, ay, az = map(int, values)

                        current_time = time.time() - start_time

                        time_data.append(current_time)
                        gyro_x_data.append(gx)
                        gyro_y_data.append(gy)
                        gyro_z_data.append(gz)
                        acc_x_data.append(ax)
                        acc_y_data.append(ay)
                        acc_z_data.append(az)

                        if current_time > DISPLAY_SECONDS:
                            time_offset = current_time - DISPLAY_SECONDS
                            relative_time = [t - time_offset for t in time_data]
                        else:
                            relative_time = list(time_data)

                        line_gx.set_data(relative_time, gyro_x_data)
                        line_gy.set_data(relative_time, gyro_y_data)
                        line_gz.set_data(relative_time, gyro_z_data)
                        line_ax.set_data(relative_time, acc_x_data)
                        line_ay.set_data(relative_time, acc_y_data)
                        line_az.set_data(relative_time, acc_z_data)

                    except:
                        pass
    except Exception:
        pass

    return line_gx, line_gy, line_gz, line_ax, line_ay, line_az

ani = animation.FuncAnimation(
    fig, animate, init_func=init,
    interval=50, blit=True, cache_frame_data=False
)

plt.tight_layout()

try:
    plt.show()
except KeyboardInterrupt:
    print("\nClosing...")
finally:
    try:
        conn.close()
    except:
        pass
    server.close()
    print("Socket closed.")
