import argparse
import json
import os
import socket
import sys
import time
from datetime import datetime, timezone


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--udp-ip", default=os.getenv("UDP_IP", "0.0.0.0"))
    p.add_argument("--udp-port", type=int, default=int(os.getenv("UDP_PORT", "8888")))
    p.add_argument("--mqtt-host", default=os.getenv("MQTT_HOST", "localhost"))
    p.add_argument("--mqtt-port", type=int, default=int(os.getenv("MQTT_PORT", "1883")))
    p.add_argument("--mqtt-user", default=os.getenv("MQTT_USER", ""))
    p.add_argument("--mqtt-pass", default=os.getenv("MQTT_PASS", ""))
    p.add_argument("--mqtt-topic", default=os.getenv("MQTT_TOPIC", "pondokrejo/seismo/sensor"))
    p.add_argument("--client-id", default=os.getenv("MQTT_CLIENT_ID", "pondokrejo-seismo-relay"))
    p.add_argument("--qos", type=int, default=int(os.getenv("MQTT_QOS", "0")))
    p.add_argument("--retain", action="store_true", default=os.getenv("MQTT_RETAIN", "0") == "1")
    p.add_argument("--tls", action="store_true", default=os.getenv("MQTT_TLS", "0") == "1")
    p.add_argument("--keepalive", type=int, default=int(os.getenv("MQTT_KEEPALIVE", "30")))
    return p.parse_args()


def parse_udp_message(msg: str):
    parts = [p.strip() for p in msg.split(",")]
    if len(parts) not in (6, 7):
        return None
    try:
        ax = float(parts[0])
        ay = float(parts[1])
        az = float(parts[2])
        peak = float(parts[3])
        mmi = int(parts[4])
        rssi = int(parts[5])
        vib = int(parts[6]) if len(parts) == 7 else 0
        mag = (ax * ax + ay * ay + az * az) ** 0.5
        return {
            "type": "sensor",
            "ax": ax,
            "ay": ay,
            "az": az,
            "mag": mag,
            "peak": peak,
            "mmi": mmi,
            "rssi": rssi,
            "vib": vib,
        }
    except Exception:
        return None


def iso_utc_now():
    return datetime.now(timezone.utc).isoformat()


def main():
    args = parse_args()

    try:
        import paho.mqtt.client as mqtt
    except Exception:
        sys.stderr.write("Module paho-mqtt belum terpasang. Install: python3 -m pip install paho-mqtt\n")
        sys.exit(2)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.udp_ip, args.udp_port))
    sys.stdout.write(f"UDP listen: {args.udp_ip}:{args.udp_port}\n")
    sys.stdout.write(f"MQTT target: {args.mqtt_host}:{args.mqtt_port} topic={args.mqtt_topic}\n")
    sys.stdout.flush()

    connected = False

    def on_connect(client, userdata, flags, rc):
        nonlocal connected
        connected = rc == 0
        sys.stdout.write(f"MQTT connected={connected} rc={rc}\n")
        sys.stdout.flush()

    def on_disconnect(client, userdata, rc):
        nonlocal connected
        connected = False
        sys.stdout.write(f"MQTT disconnected rc={rc}\n")
        sys.stdout.flush()

    client = mqtt.Client(client_id=args.client_id, clean_session=True)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

    if args.mqtt_user:
        client.username_pw_set(args.mqtt_user, args.mqtt_pass)
    if args.tls:
        client.tls_set()

    client.connect_async(args.mqtt_host, args.mqtt_port, keepalive=args.keepalive)
    client.loop_start()

    last_print = 0.0
    while True:
        data, addr = sock.recvfrom(4096)
        raw = data.decode("utf-8", errors="ignore").strip()
        payload = parse_udp_message(raw)
        if payload is None:
            continue

        payload["ts"] = iso_utc_now()
        payload["src"] = {"ip": addr[0], "port": addr[1]}
        body = json.dumps(payload, separators=(",", ":"))

        if connected:
            client.publish(args.mqtt_topic, body, qos=args.qos, retain=args.retain)

        now = time.time()
        if now - last_print > 2:
            last_print = now
            sys.stdout.write(f"RX {payload['ts']} mmi={payload['mmi']} peak={payload['peak']:.3f} vib={payload['vib']}\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
