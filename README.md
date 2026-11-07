# Water Pump Automation V2.2

Firmware untuk dua ESP32 DevKit V1 / ESP32-WROOM-32:

- [`pump_controller/`](pump_controller/README.md): Wi-Fi SoftAP, HTTP server, AUTO/MANUAL, tombol dan selector, SSR, serta LCD I²C 20x4.
- [`tank_controller/`](tank_controller/README.md): AJ-SR04M, perhitungan level, koneksi Wi-Fi client, dan HTTP POST.
- [`wiring-diagram-water-pump.md`](wiring-diagram-water-pump.md): wiring pin, sensor, LCD, SSR, dan komunikasi.

## Perilaku konfigurasi saat ini

- Pump Controller membuat AP `PumpCtrl-Setup` di `192.168.4.1`; Tank Controller mengirim data tiap detik ke `/api/v1/tank`.
- AUTO memulai pompa pada level valid `<=20%`; cutoff penuh saat ultrasonic valid mencapai `100%`.
- Cutoff float switch **dinonaktifkan sementara** di kedua firmware. Tank Controller mengirim `float_full:false`; Pump Controller juga mengabaikan nilai float yang diterima.
- Di MANUAL, tombol ON menyalakan dan tombol OFF mematikan. Ultrasonic valid 100% serta batas durasi maksimum tetap mematikan pompa.
- LCD menampilkan mode, status pompa, level, status/umur data Tank, dan alasan stop atau durasi berjalan.

## Build

Buka masing-masing folder controller secara terpisah di PlatformIO, pilih environment `esp32dev`, lalu Build dan Upload ke board yang sesuai. Masing-masing `platformio.ini` memuat dependency yang diperlukan.

Verifikasi kalibrasi jarak ultrasonic, active level dan rating SSR, serta level tegangan ECHO/I²C sebelum menyalakan beban. Uji logika output dengan indikator tegangan rendah terlebih dahulu. Diagram jalur listrik pompa hanya konseptual karena rating pompa dan model SSR belum ditentukan.
