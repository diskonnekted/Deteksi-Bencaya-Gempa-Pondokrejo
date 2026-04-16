#!/usr/bin/env python3
"""
WebSocket Bridge Server
Forwards UDP seismograph data to WebSocket clients for browser visualization
"""

import asyncio
import json
import socket
import threading
import websockets

# ─── Configuration ───
UDP_IP = "0.0.0.0"
UDP_PORT = 8888
WS_HOST = "localhost"
WS_PORT = 8765
BUFFER_SIZE = 4096

# ─── Connected Clients ───
connected_clients = set()

# ─── Latest Sensor Data ───
sensor_data = {
    'type': 'sensor',
    'ax': 0,
    'ay': 0,
    'az': 0,
    'peak': 0,
    'mmi': 1,
    'rssi': 0,
    'vib': 0
}


# ─── UDP Listener ───
def udp_listener():
    """Listen for UDP data from ESP32"""
    global sensor_data
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    print(f"Listening for UDP data on port {UDP_PORT}...")

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

                sensor_data = {
                    'type': 'sensor',
                    'ax': ax,
                    'ay': ay,
                    'az': az,
                    'peak': peak,
                    'mmi': mmi,
                    'rssi': rssi,
                    'vib': vib
                }
        except Exception as e:
            print(f"UDP Error: {e}")


# ─── WebSocket Handler ───
async def websocket_handler(websocket, path=None):
    """Handle WebSocket connections from browser"""
    connected_clients.add(websocket)
    print(f"Client connected. Total clients: {len(connected_clients)}")
    
    try:
        while True:
            # Send current sensor data to client
            if connected_clients:
                message = json.dumps(sensor_data)
                await asyncio.gather(
                    *[client.send(message) for client in connected_clients]
                )
            await asyncio.sleep(0.05)  # 20 FPS
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.discard(websocket)
        print(f"Client disconnected. Total clients: {len(connected_clients)}")


# ─── Main ───
async def main():
    # Start UDP listener in separate thread
    udp_thread = threading.Thread(target=udp_listener, daemon=True)
    udp_thread.start()

    # Start WebSocket server
    print(f"WebSocket bridge starting on ws://{WS_HOST}:{WS_PORT}")
    print("Open seismo_3d_visualization.html in your browser")
    
    await websockets.serve(websocket_handler, WS_HOST, WS_PORT)
    await asyncio.Future()  # Run forever


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nBridge server stopped")
