#!/usr/bin/env python3
"""
LoRa Earthquake Seismograph Dashboard
Real-time seismograph display with MMI scale
Listens for UDP data from ESP32 receiver
"""

import socket
import threading
import time
import asyncio
import json
import websockets
from collections import deque
from datetime import datetime
import tkinter as tk
from tkinter import font

# ─── Configuration ───
UDP_IP = "0.0.0.0"
UDP_PORT = 8888
WS_PORT = 8765
MAX_POINTS = 300  # Visible points on chart
BUFFER_SIZE = 4096

# ─── WebSocket Clients ───
ws_clients = set()
ws_lock = threading.Lock()
ws_loop = None
ws_broadcast_queue = None  # Created after event loop starts

# ─── Data Storage ───
data_lock = threading.Lock()
ax_data = deque(maxlen=MAX_POINTS)
ay_data = deque(maxlen=MAX_POINTS)
az_data = deque(maxlen=MAX_POINTS)
mag_data = deque(maxlen=MAX_POINTS)
peak_data = deque(maxlen=MAX_POINTS)
time_data = deque(maxlen=MAX_POINTS)

current_values = {
    'ax': 0, 'ay': 0, 'az': 0,
    'mag': 0, 'peak': 0, 'mmi': 1, 'rssi': 0, 'vib': 0
}
max_mmi = 1
alert_count = 0
alert_active = False
last_update = time.time()


def mmi_label(mmi):
    labels = ['', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X', 'XI', 'XII']
    return labels[mmi] if 1 <= mmi <= 12 else '?'


def mmi_desc(mmi):
    descs = [
        '-', 'Tidak terasa', 'Terasa sangat lemah', 'Lemah - seperti truk lewat',
        'Sedang - jendela bergetar', 'Agak kuat - benda bergerak', 'Kuat - terasa semua orang',
        'Sangat kuat - sulit berdiri', 'Merusak - bangunan retak', 'Hancur - bangunan rubuh',
        'Bencana - struktur hancur', 'Bencana total', 'Kehancuran total'
    ]
    return descs[mmi] if 1 <= mmi <= 12 else '-'


def mmi_color(mmi):
    colors = {
        1: '#ffffff', 2: '#00ff00', 3: '#00ff00', 4: '#ffff00', 5: '#ffff00',
        6: '#ffaa00', 7: '#ff8800', 8: '#ff4400', 9: '#ff0000',
        10: '#cc0000', 11: '#880000', 12: '#000000'
    }
    return colors.get(mmi, '#ffffff')


# ─── UDP Listener ───
def udp_listener():
    global max_mmi, alert_count, alert_active, last_update
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"Listening on UDP port {UDP_PORT}...")

    while True:
        try:
            data, addr = sock.recvfrom(BUFFER_SIZE)
            msg = data.decode('utf-8', errors='ignore').strip()
            parts = msg.split(',')
            if len(parts) in (6, 7):
                ax = float(parts[0])
                ay = float(parts[1])
                az = float(parts[2])
                peak = float(parts[3])
                mmi = int(parts[4])
                rssi = int(parts[5])
                vib = int(parts[6]) if len(parts) == 7 else 0
                mag = (ax**2 + ay**2 + az**2) ** 0.5

                now = time.time()
                with data_lock:
                    ax_data.append(ax)
                    ay_data.append(ay)
                    az_data.append(az)
                    mag_data.append(mag)
                    peak_data.append(peak)
                    time_data.append(now)
                    current_values['ax'] = ax
                    current_values['ay'] = ay
                    current_values['az'] = az
                    current_values['mag'] = mag
                    current_values['peak'] = peak
                    current_values['mmi'] = mmi
                    current_values['rssi'] = rssi
                    current_values['vib'] = vib

                    if mmi > max_mmi:
                        max_mmi = mmi
                    if mmi >= 5:
                        alert_count += 1
                        alert_active = True
                    else:
                        alert_active = False
                    last_update = now

                # Broadcast to WebSocket clients via queue
                broadcast_data = json.dumps({
                    'type': 'sensor',
                    'ax': ax,
                    'ay': ay,
                    'az': az,
                    'peak': peak,
                    'mmi': mmi,
                    'rssi': rssi,
                    'vib': vib
                })
                if ws_loop is not None and ws_broadcast_queue is not None:
                    try:
                        ws_loop.call_soon_threadsafe(
                            ws_broadcast_queue.put_nowait, broadcast_data
                        )
                    except Exception as e:
                        print(f"WS Broadcast Error: {e}")
                else:
                    print(f"WS Queue not ready: loop={ws_loop is not None}, queue={ws_broadcast_queue is not None}")
        except Exception as e:
            print(f"UDP Error: {e}")


# ─── Main GUI ───
class SeismoGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("SEISMOGRAPH MONITOR - LoRa Earthquake Detection")
        self.root.configure(bg='#000000')
        self.root.geometry("1200x800")

        # Fonts
        self.font_title = font.Font(family='Courier New', size=14, weight='bold')
        self.font_data = font.Font(family='Courier New', size=24, weight='bold')
        self.font_small = font.Font(family='Courier New', size=11)
        self.font_mmi = font.Font(family='Courier New', size=48, weight='bold')

        # Title bar
        title_frame = tk.Frame(self.root, bg='#000000')
        title_frame.pack(fill='x', padx=10, pady=5)

        tk.Label(title_frame, text="◉ SEISMOGRAPH MONITOR",
                fg='#ffffff', bg='#000000', font=self.font_title).pack(side='left')

        self.status_var = tk.StringVar(value="● WAITING FOR DATA...")
        tk.Label(title_frame, textvariable=self.status_var,
                fg='#00ff00', bg='#000000', font=self.font_small).pack(side='right')

        # Alert bar
        self.alert_frame = tk.Frame(self.root, bg='#000000')
        self.alert_frame.pack(fill='x', padx=10, pady=2)
        self.alert_label = tk.Label(self.alert_frame, text="",
                                    bg='#000000', fg='#000000',
                                    font=font.Font(family='Courier New', size=14, weight='bold'))
        self.alert_label.pack(fill='x')

        # Status bar
        status_frame = tk.Frame(self.root, bg='#111111')
        status_frame.pack(fill='x', padx=10, pady=5)

        # Left stats
        stats_left = tk.Frame(status_frame, bg='#111111')
        stats_left.pack(side='left', fill='x', expand=True)

        self.mag_var = tk.StringVar(value="0.000")
        self.peak_var = tk.StringVar(value="0.000")
        self.mmi_var = tk.StringVar(value="I")
        self.max_mmi_var = tk.StringVar(value="I")
        self.alerts_var = tk.StringVar(value="0")
        self.rssi_var = tk.StringVar(value="---")
        self.time_var = tk.StringVar(value="--:--:--")

        labels = [
            ("MAG (m/s²)", self.mag_var),
            ("PEAK (m/s²)", self.peak_var),
            ("MMI", self.mmi_var),
            ("MAX MMI", self.max_mmi_var),
            ("ALERTS", self.alerts_var),
            ("RSSI (dBm)", self.rssi_var)
        ]

        for label, var in labels:
            row = tk.Frame(stats_left, bg='#111111')
            row.pack(fill='x')
            tk.Label(row, text=label, fg='#666666', bg='#111111',
                    font=self.font_small).pack(side='left', padx=10)
            tk.Label(row, textvariable=var, fg='#ffffff', bg='#111111',
                    font=self.font_data).pack(side='left', padx=5)

        # MMI Badge (center)
        mmi_frame = tk.Frame(self.root, bg='#000000')
        mmi_frame.pack(pady=10)

        self.mmi_canvas = tk.Canvas(mmi_frame, width=120, height=100,
                                    bg='#000000', highlightthickness=2,
                                    highlightcolor='#ffffff')
        self.mmi_canvas.pack()

        self.mmi_desc_var = tk.StringVar(value="Menunggu data...")
        tk.Label(mmi_frame, textvariable=self.mmi_desc_var,
                fg='#888888', bg='#000000', font=self.font_small).pack()

        # Seismograph canvas
        self.seismo_canvas = tk.Canvas(self.root, bg='#000000',
                                       height=300, highlightthickness=1,
                                       highlightcolor='#333333')
        self.seismo_canvas.pack(fill='x', padx=10, pady=5)

        # 3-Axis canvas
        self.axis_canvas = tk.Canvas(self.root, bg='#000000',
                                     height=200, highlightthickness=1,
                                     highlightcolor='#333333')
        self.axis_canvas.pack(fill='x', padx=10, pady=5)

        # Start update loop
        self.update_gui()

    def update_gui(self):
        with data_lock:
            mag = current_values['mag']
            peak = current_values['peak']
            mmi = current_values['mmi']
            rssi = current_values['rssi']

        self.mag_var.set(f"{mag:.3f}")
        self.peak_var.set(f"{peak:.3f}")
        self.mmi_var.set(mmi_label(mmi))
        self.max_mmi_var.set(mmi_label(max_mmi))
        self.alerts_var.set(str(alert_count))
        self.rssi_var.set(str(rssi) if rssi != 0 else "---")
        self.time_var.set(datetime.now().strftime("%H:%M:%S"))

        # Update status
        elapsed = time.time() - last_update
        if elapsed < 3:
            self.status_var.set("● RECEIVING")
            self.status_label_config(fg='#00ff00')
        else:
            self.status_var.set("● NO SIGNAL")
            self.status_label_config(fg='#ff0000')

        # Alert bar
        if alert_active:
            self.alert_label.config(
                text=f"⚠ ALERT — MMI {mmi_label(mmi)} — {mmi_desc(mmi)} ⚠",
                bg='#ff0000',
                fg='#ffffff'
            )
        else:
            self.alert_label.config(text="", bg='#000000')

        # MMI Badge
        self.draw_mmi_badge(mmi)

        # Draw charts
        self.draw_seismograph()
        self.draw_axis()

        self.root.after(200, self.update_gui)

    def status_label_config(self, fg):
        for child in self.root.children.values():
            if isinstance(child, tk.Frame):
                for c in child.children.values():
                    if isinstance(c, tk.Label) and c.cget('text') == self.status_var.get():
                        c.config(fg=fg)

    def draw_mmi_badge(self, mmi):
        self.mmi_canvas.delete('all')
        color = mmi_color(mmi)
        bg_color = '#111111'

        # Background
        self.mmi_canvas.create_rectangle(5, 5, 115, 75,
                                         fill=color, outline='#ffffff', width=2)

        # Roman numeral
        text_color = '#000000' if mmi in [4, 5, 6, 7, 8, 9, 10, 11] else '#ffffff'
        self.mmi_canvas.create_text(60, 40, text=mmi_label(mmi),
                                    fill=text_color, font=self.font_mmi)

        self.mmi_desc_var.set(mmi_desc(mmi))

    def draw_seismograph(self):
        canvas = self.seismo_canvas
        canvas.delete('all')
        w = canvas.winfo_width() or 1180
        h = canvas.winfo_height() or 300

        # Grid
        for y in range(0, h, 50):
            canvas.create_line(0, y, w, y, fill='#1a1a1a', width=1)

        # Center line
        canvas.create_line(0, h - 20, w, h - 20, fill='#333333', width=1)

        with data_lock:
            if len(mag_data) < 2:
                return

            data = list(mag_data)
            times = list(time_data)

        # Scale: use max of data or 5.0
        max_val = max(max(data), 5.0)
        step = w / (MAX_POINTS - 1)

        # White line - seismograph style
        for i in range(len(data) - 1):
            x1 = i * step
            y1 = h - 20 - (data[i] / max_val) * (h - 40)
            x2 = (i + 1) * step
            y2 = h - 20 - (data[i + 1] / max_val) * (h - 40)

            # Color based on intensity
            if data[i] > 3.0:
                color = '#ff0000'
            elif data[i] > 1.0:
                color = '#ffaa00'
            elif data[i] > 0.5:
                color = '#ffff00'
            else:
                color = '#ffffff'

            canvas.create_line(x1, y1, x2, y2, fill=color, width=2)

        # Scale labels
        canvas.create_text(5, 15, text=f"{max_val:.1f} m/s²",
                          fill='#666666', font=self.font_small, anchor='w')
        canvas.create_text(5, h - 8, text="0",
                          fill='#666666', font=self.font_small, anchor='w')
        canvas.create_text(5, h - 25, text=f"-{len(data)}s",
                          fill='#444444', font=self.font_small, anchor='w')
        canvas.create_text(w - 5, h - 8, text="now",
                          fill='#444444', font=self.font_small, anchor='e')

        # Title
        canvas.create_text(w - 5, 15,
                          text=f"ACCELERATION MAGNITUDE | Samples: {len(data)}",
                          fill='#444444', font=self.font_small, anchor='e')

    def draw_axis(self):
        canvas = self.axis_canvas
        canvas.delete('all')
        w = canvas.winfo_width() or 1180
        h = canvas.winfo_height() or 200

        # Grid
        for y in range(0, h, 40):
            canvas.create_line(0, y, w, y, fill='#1a1a1a', width=1)

        mid = h // 2
        canvas.create_line(0, mid, w, mid, fill='#333333', width=1)

        with data_lock:
            if len(ax_data) < 2:
                return
            ax = list(ax_data)
            ay = list(ay_data)
            az = list(az_data)

        # Scale
        all_vals = [abs(v) for v in ax + ay + az]
        max_val = max(max(all_vals) if all_vals else 0, 5.0)
        step = w / (MAX_POINTS - 1)

        # Draw each axis
        datasets = [
            (ax, '#ff4444'),
            (ay, '#44ff44'),
            (az, '#4444ff')
        ]

        for data, color in datasets:
            for i in range(len(data) - 1):
                x1 = i * step
                y1 = mid - (data[i] / max_val) * (mid - 20)
                x2 = (i + 1) * step
                y2 = mid - (data[i + 1] / max_val) * (mid - 20)
                canvas.create_line(x1, y1, x2, y2, fill=color, width=1)

        # Legend
        canvas.create_text(w - 60, 15, text="X", fill='#ff4444', font=self.font_small)
        canvas.create_text(w - 40, 15, text="Y", fill='#44ff44', font=self.font_small)
        canvas.create_text(w - 20, 15, text="Z", fill='#4444ff', font=self.font_small)

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    # Create WebSocket event loop BEFORE starting threads
    ws_loop = asyncio.new_event_loop()

    # Start UDP listener thread
    listener_thread = threading.Thread(target=udp_listener, daemon=True)
    listener_thread.start()
    print("Seismograph dashboard starting...")

    # Start WebSocket server in event loop
    async def ws_handler(websocket):
        """Handle new WebSocket connections (websockets 15.x API)"""
        ws_clients.add(websocket)
        print(f"3D client connected. Total: {len(ws_clients)}")
        try:
            await asyncio.Future()
        except asyncio.CancelledError:
            pass
        finally:
            ws_clients.discard(websocket)
            print(f"3D client disconnected. Total: {len(ws_clients)}")

    async def ws_broadcaster():
        """Broadcast data to all connected clients - runs in WS event loop"""
        global ws_broadcast_queue
        ws_broadcast_queue = asyncio.Queue()
        print("WebSocket broadcaster started")
        while True:
            data = await ws_broadcast_queue.get()
            if ws_clients:
                dead = set()
                for client in ws_clients:
                    try:
                        await client.send(data)
                    except Exception as e:
                        print(f"WS send error: {e}")
                        dead.add(client)
                ws_clients -= dead

    async def ws_server():
        asyncio.create_task(ws_broadcaster())
        server = await websockets.serve(ws_handler, "0.0.0.0", WS_PORT)
        print(f"WebSocket server on ws://0.0.0.0:{WS_PORT}")
        await asyncio.Future()

    ws_thread = threading.Thread(target=lambda: ws_loop.run_until_complete(ws_server()), daemon=True)
    ws_thread.start()

    # Start GUI
    app = SeismoGUI()
    app.run()
