# Monitoring Bahaya Gempa - Kalurahan Pondokrejo, Sleman Yogyakarta

Sistem monitoring getaran berbasis ESP32 + LoRa untuk deteksi dini bahaya gempa. Data sensor dikirim dari node pemancar (TX) melalui LoRa ke node penerima (RX), lalu diteruskan ke server melalui UDP dan ditampilkan real-time di dashboard web melalui WebSocket.

## Arsitektur

1. ESP32 TX membaca sensor:
   - MPU6050 (akselerometer) untuk amplitudo getaran (m/s²)
   - SW-18010P (modul 3 pin, DO) untuk deteksi pulsa getaran (vibration pulses per second)
2. ESP32 TX mengirim payload melalui LoRa ke ESP32 RX.
3. ESP32 RX meneruskan payload ke server PC melalui UDP port 8888.
4. Server Python membaca UDP dan broadcast ke client browser via WebSocket (default port 8765/8766).
5. Dashboard HTML menampilkan nilai, indikator MMI, grafik, visual 3D, serta log event (retensi 7 hari di browser).

## Komponen Utama

- Firmware TX: [lora-seismo-tx/lora-seismo-tx.ino](nuon-esp32-shield-lora/lora-seismo-tx/lora-seismo-tx.ino)
- Firmware RX: [lora-seismo-rx/lora-seismo-rx.ino](nuon-esp32-shield-lora/lora-seismo-rx/lora-seismo-rx.ino)
- WebSocket server (UDP → WS): [websocket_server.py](nuon-esp32-shield-lora/websocket_server.py)
- Dashboard web: [seismo_dashboard_web.html](nuon-esp32-shield-lora/seismo_dashboard_web.html)

## Format Data

### LoRa (TX → RX)

Format CSV:

```
ax,ay,az,peak,mmi,vib
```

- `ax, ay, az`: percepatan linear (m/s²) setelah kompensasi gravitasi
- `peak`: puncak (rolling peak) dalam window (m/s²)
- `mmi`: skala intensitas (1–12) berdasarkan mapping sederhana dari `peak`
- `vib`: jumlah pulsa DO SW-18010P per detik (pps)

### UDP (RX → Server)

Format CSV:

```
ax,ay,az,peak,mmi,rssi,vib
```

- `rssi`: RSSI LoRa (dBm)

### WebSocket (Server → Browser)

JSON:

```json
{
  "type": "sensor",
  "ax": 0.0,
  "ay": 0.0,
  "az": 0.0,
  "peak": 0.0,
  "mmi": 1,
  "rssi": -50,
  "vib": 0
}
```

## Hardware

### ESP32 TX

- LoRa transceiver via NUON ESP32 Shield LoRa (pin default di sketch)
- MPU6050 (I2C):
  - SDA: GPIO21
  - SCL: GPIO22
- SW-18010P modul 3 pin (DO):
  - VCC → 3.3V
  - GND → GND
  - DO  → GPIO27 (default, bisa diubah di `VIB_DO_PIN`)

### ESP32 RX

- LoRa transceiver via NUON ESP32 Shield LoRa
- WiFi untuk kirim UDP ke server

## Menjalankan Sistem (Lokal / LAN)

### 1) Flash firmware ESP32

- Flash TX:
  - Buka [lora-seismo-tx.ino](nuon-esp32-shield-lora/lora-seismo-tx/lora-seismo-tx.ino)
  - Pastikan wiring MPU6050 dan SW-18010P sesuai
  - Upload ke ESP32 TX
- Flash RX:
  - Buka [lora-seismo-rx.ino](nuon-esp32-shield-lora/lora-seismo-rx/lora-seismo-rx.ino)
  - Set `ssid`, `password`, dan `pcIP` (IP server), port default `8888`
  - Upload ke ESP32 RX

### 2) Jalankan server WebSocket

Dari folder `nuon-esp32-shield-lora`:

```bash
python websocket_server.py
```

Server akan listen:
- UDP: `0.0.0.0:8888`
- WebSocket: `0.0.0.0:8765` (atau 8766 jika 8765 sudah dipakai)

### 3) Buka dashboard

Opsi A (langsung buka file):
- Buka [seismo_dashboard_web.html](nuon-esp32-shield-lora/seismo_dashboard_web.html) di browser

Opsi B (serve via HTTP untuk akses dari device lain):

```bash
cd nuon-esp32-shield-lora
python -m http.server 8000
```

Lalu buka:

```
http://<ip-server>:8000/seismo_dashboard_web.html
```

## Catatan Kalibrasi (MPU6050)

Firmware TX menggunakan pendekatan percepatan linear:
- Komponen gravitasi diestimasi dengan low-pass filter dan akan mengikuti orientasi baru ketika perangkat stabil.
- Tujuannya agar perubahan posisi/kemiringan tidak dianggap sebagai getaran gempa yang berkelanjutan.

## Log Event (7 Hari)

Dashboard menyimpan log event (WARN/ALERT) di browser (localStorage) dengan retensi 7 hari.
Log disembunyikan secara default dan bisa ditampilkan melalui tombol pada panel Log Alert.

## Deployment ke Server (Ringkas)

Untuk deployment ke server internet (VPS/CloudPanel):
- Pastikan server bisa menerima UDP 8888 dari ESP32 atau ubah jalur kirim data menjadi HTTPS/MQTT.
- Untuk akses dashboard via HTTPS, gunakan `wss://` (reverse proxy WebSocket) agar tidak terkena pembatasan mixed content.

