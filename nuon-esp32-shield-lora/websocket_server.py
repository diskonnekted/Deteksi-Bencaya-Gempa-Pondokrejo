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
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

UDP_PORT = 8888
WS_PORT = 8765
HTTP_PORT = int(os.getenv("HTTP_PORT", "18080"))
HTTP_BIND = os.getenv("HTTP_BIND", "0.0.0.0")
API_KEY = os.getenv("API_KEY", "")

ws_clients = set()
latest_data = None

data_lock = threading.Lock()

def set_latest_payload(payload: dict, source: str):
    global latest_data
    payload = dict(payload)
    payload["type"] = "sensor"
    payload["source"] = source
    with data_lock:
        latest_data = json.dumps(payload, separators=(",", ":"))

def udp_listener():
    """Listen for UDP data"""
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
                    'ax': float(parts[0]),
                    'ay': float(parts[1]),
                    'az': float(parts[2]),
                    'peak': float(parts[3]),
                    'mmi': int(parts[4]),
                    'rssi': int(parts[5]),
                    'vib': int(parts[6]) if len(parts) == 7 else 0
                }
                set_latest_payload(payload, source=f"udp:{addr[0]}:{addr[1]}")
                print(f"UDP: ax={parts[0]} ay={parts[1]} az={parts[2]} vib={payload['vib']}")
            except Exception as e:
                print(f"UDP PARSE ERROR: {e}")
        else:
            print(f"UDP INVALID: {len(parts)} parts")

class ApiHandler(BaseHTTPRequestHandler):
    server_version = "SeismoHTTP/1.0"

    def log_message(self, fmt, *args):
        return

    def _send_json(self, status, obj):
        body = json.dumps(obj, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        p = urlparse(self.path)
        if p.path != "/api/sensor":
            self._send_json(404, {"ok": False, "error": "not_found"})
            return

        if API_KEY:
            key = self.headers.get("X-API-Key", "")
            if key != API_KEY:
                self._send_json(401, {"ok": False, "error": "unauthorized"})
                return

        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0 or length > 8192:
            self._send_json(400, {"ok": False, "error": "bad_length"})
            return

        raw = self.rfile.read(length)
        try:
            data = json.loads(raw.decode("utf-8"))
        except Exception:
            self._send_json(400, {"ok": False, "error": "bad_json"})
            return

        try:
            payload = {
                "ax": float(data.get("ax", 0)),
                "ay": float(data.get("ay", 0)),
                "az": float(data.get("az", 0)),
                "peak": float(data.get("peak", 0)),
                "mmi": int(data.get("mmi", 1)),
                "rssi": int(data.get("rssi", 0)),
                "vib": int(data.get("vib", 0)),
            }
        except Exception:
            self._send_json(400, {"ok": False, "error": "bad_fields"})
            return

        src = self.client_address[0]
        set_latest_payload(payload, source=f"http:{src}")
        self._send_json(200, {"ok": True})

    def do_GET(self):
        p = urlparse(self.path)
        if p.path == "/api/health":
            with data_lock:
                has_data = latest_data is not None
            self._send_json(200, {"ok": True, "has_data": has_data})
            return
        if p.path == "/api/sensor":
            self._send_json(405, {"ok": False, "error": "method_not_allowed", "allowed": ["POST"]})
            return
        self._send_json(404, {"ok": False, "error": "not_found"})

def http_server_thread():
    try:
        httpd = ThreadingHTTPServer((HTTP_BIND, HTTP_PORT), ApiHandler)
    except Exception as e:
        print(f"HTTP bind failed on {HTTP_BIND}:{HTTP_PORT} ({e})")
        print("Coba ganti port: set env HTTP_PORT=18080 (atau port lain yang kosong)")
        return
    print(f"HTTP API on http://{HTTP_BIND}:{HTTP_PORT}/api/sensor")
    httpd.serve_forever()

async def handler(websocket, path=None):
    """Handle WebSocket connections"""
    ws_clients.add(websocket)
    print(f"Client connected ({len(ws_clients)} total)")
    send_count = 0
    try:
        # Send initial data if available
        with data_lock:
            init = latest_data
        if init:
            await websocket.send(init)
        # Keep connection alive and send updates
        while True:
            with data_lock:
                data = latest_data
            if data:
                try:
                    await websocket.send(data)
                    send_count += 1
                    if send_count % 20 == 0:
                        print(f"Sent {send_count} updates, latest: {data[:50]}...")
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

    ht = threading.Thread(target=http_server_thread, daemon=True)
    ht.start()
    
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
