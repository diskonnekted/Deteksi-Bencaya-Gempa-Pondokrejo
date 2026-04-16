#!/usr/bin/env python3
"""
Simple WebSocket Server for 3D Visualization
Listens on UDP 8888 and broadcasts to WebSocket clients on port 8765
"""

import socket
import asyncio
import json
import threading
import websockets

UDP_PORT = 8888
WS_PORT = 8765

ws_clients = set()
latest_data = None

def udp_listener():
    """Listen for UDP data"""
    global latest_data
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_PORT))
    print(f"UDP listener on port {UDP_PORT}")
    
    while True:
        data, addr = sock.recvfrom(4096)
        msg = data.decode('utf-8', errors='ignore').strip()
        print(f"UDP RAW: {msg}")
        parts = msg.split(',')
        if len(parts) in (6, 7):
            try:
                payload = {
                    'type': 'sensor',
                    'ax': float(parts[0]),
                    'ay': float(parts[1]),
                    'az': float(parts[2]),
                    'peak': float(parts[3]),
                    'mmi': int(parts[4]),
                    'rssi': int(parts[5]),
                    'vib': int(parts[6]) if len(parts) == 7 else 0
                }
                latest_data = json.dumps(payload)
                print(f"UDP: ax={parts[0]} ay={parts[1]} az={parts[2]} vib={payload['vib']}")
            except Exception as e:
                print(f"UDP PARSE ERROR: {e}")
        else:
            print(f"UDP INVALID: {len(parts)} parts")

async def handler(websocket, path=None):
    """Handle WebSocket connections"""
    ws_clients.add(websocket)
    print(f"Client connected ({len(ws_clients)} total)")
    send_count = 0
    try:
        # Send initial data if available
        if latest_data:
            await websocket.send(latest_data)
            print(f"Sent initial data: {latest_data}")
        # Keep connection alive and send updates
        while True:
            if latest_data:
                try:
                    await websocket.send(latest_data)
                    send_count += 1
                    if send_count % 20 == 0:
                        print(f"Sent {send_count} updates, latest: {latest_data[:50]}...")
                except Exception as e:
                    print(f"Send error: {e}")
                    break
            else:
                print("No data to send yet")
            await asyncio.sleep(0.05)  # 20 FPS
    except websockets.exceptions.ConnectionClosed:
        print("Connection closed by client")
    finally:
        ws_clients.discard(websocket)
        print(f"Client disconnected ({len(ws_clients)} total)")

async def main():
    # Start UDP listener
    t = threading.Thread(target=udp_listener, daemon=True)
    t.start()
    
    port = WS_PORT
    while True:
        try:
            server = await websockets.serve(handler, "0.0.0.0", port)
            break
        except OSError as e:
            if getattr(e, "winerror", None) == 10048 or getattr(e, "errno", None) == 10048:
                port += 1
                continue
            raise

    print(f"WebSocket server on ws://0.0.0.0:{port}")
    await server.serve_forever()

if __name__ == "__main__":
    print("Starting WebSocket bridge server...")
    asyncio.run(main())
