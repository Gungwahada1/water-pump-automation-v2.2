# Water Pump Automation V2.3

Firmware untuk dua ESP32 DevKit V1 / ESP32-WROOM-32:

- [`pump_controller/`](pump_controller/README.md): Wi-Fi SoftAP, dashboard lokal dan API HTTP, AUTO/MANUAL, tombol dan selector, SSR, serta LCD I²C 20x4.
- [`tank_controller/`](tank_controller/README.md): AJ-SR04M, perhitungan level, koneksi Wi-Fi client, dan HTTP POST.
- [`wiring-diagram-water-pump.md`](wiring-diagram-water-pump.md): wiring pin, sensor, LCD, SSR, dan komunikasi.

## Perilaku konfigurasi saat ini

- Pump Controller membuat AP `PumpCtrl-Setup` di `192.168.4.1`; Tank Controller mengirim pembacaan baru ke `/api/v1/tank` pada interval sensor yang dikonfigurasi (default 1 detik).
- AUTO memulai pompa pada level valid di bawah ambang konfigurasi (default `<=20%`) dan berhenti pada ambang AUTO stop yang bisa diatur (default `100%`, harus lebih besar dari ambang start). MANUAL tetap punya cutoff ultrasonic tetap pada `100%`.
- Cutoff float switch **dinonaktifkan sementara** di kedua firmware. Tank Controller mengirim `float_full:false`; Pump Controller juga mengabaikan nilai float yang diterima.
- Di MANUAL, tombol fisik ON/OFF mempertahankan perilaku V2.2. Dashboard ON mensyaratkan selector MANUAL, data Tank segar/valid, level di bawah penuh, dan tidak ada lockout. Dashboard OFF selalu tersedia.
- LCD menampilkan mode, status pompa, level, status/umur data Tank, dan alasan stop atau durasi berjalan.

## Build

Buka masing-masing folder controller secara terpisah di PlatformIO, pilih environment `esp32dev`, lalu Build dan Upload ke board yang sesuai. Masing-masing `platformio.ini` memuat dependency yang diperlukan.

Verifikasi kalibrasi jarak ultrasonic, active level dan rating SSR, serta level tegangan ECHO/I²C sebelum menyalakan beban. Uji logika output dengan indikator tegangan rendah terlebih dahulu. Diagram jalur listrik pompa hanya konseptual karena rating pompa dan model SSR belum ditentukan.

## Dashboard V2.3

Hubungkan HP/laptop ke AP `PumpCtrl-Setup`, lalu buka `http://192.168.4.1`. Dashboard menyajikan Overview, Pump, Tank, Configuration, Device, dan System Logs tanpa internet. Konfigurasi yang diedit lewat API hanya tersimpan di RAM dan kembali ke default sesudah reboot. Riwayat log memakai ring buffer RAM terbatas.

API: `GET /api/v1/status`, `POST /api/v1/tank`, `POST /api/v1/control`, `GET/POST /api/v1/config`, `GET /api/v1/device`, `GET /api/v1/logs`, dan `POST /api/v1/device/restart`. Endpoint Tank lama tetap diterima; data baru menambahkan distance, boot ID, dan sequence number.

Dashboard tidak mengendalikan SSR langsung. Semua perintah masuk antrean dan diperiksa lagi oleh loop kontrol. Float switch tetap nonaktif pada V2.3, tetapi jalur status dan cutoff dipertahankan untuk aktivasi setelah perangkat fisik diverifikasi. Float melalui Wi-Fi bukan proteksi overflow independen.

Dokumentasi request, respons, batas konfigurasi, dan alasan penolakan API ada di [`docs/api-v1.md`](docs/api-v1.md). Ganti password AP bawaan di konfigurasi Pump dan Tank Controller sebelum membuka kontrol dashboard ke pengguna lain.
