# Water Pump Automation V2.4 — Configuration Manager + NVS

Firmware untuk dua ESP32 DevKit V1 / ESP32-WROOM-32:

- [`pump_controller/`](pump_controller/README.md): Wi-Fi SoftAP, dashboard lokal dan API HTTP, AUTO/MANUAL, tombol ON/OFF/reset dan selector, SSR, serta LCD I²C 20x4.
- [`tank_controller/`](tank_controller/README.md): AJ-SR04M, perhitungan level, tombol reset, koneksi Wi-Fi client, dan HTTP POST.
- [`wiring-diagram-water-pump.md`](wiring-diagram-water-pump.md): wiring pin, sensor, LCD, SSR, dan komunikasi.

## Perilaku konfigurasi

- Pump Controller membuat AP `PumpCtrl-Setup` di `192.168.4.1` untuk maksimal lima client (Tank Controller dan hingga empat perangkat dashboard); Tank mengirim pembacaan baru ke `/api/v1/tank` pada interval sensor yang dikonfigurasi (default 1 detik).
- AUTO memulai pompa pada level valid di atau di bawah ambang konfigurasi (default `<=20%`) dan berhenti pada ambang stop yang bisa diatur (default `100%`, harus lebih besar dari ambang start). AUTO dan MANUAL memakai ambang stop ultrasonic yang sama.
- Cutoff float switch **dinonaktifkan sementara** di kedua firmware. Tank Controller mengirim `float_full:false`; Pump Controller juga mengabaikan nilai float yang diterima.
- Di MANUAL, tombol fisik ON/OFF mempertahankan perilaku V2.2. Dashboard ON mensyaratkan selector MANUAL, data Tank segar/valid, level di bawah penuh, dan tidak ada lockout. Dashboard OFF selalu tersedia.
- LCD menampilkan mode, status pompa, level, status/umur data Tank, dan alasan stop atau durasi berjalan.

## Build

Buka masing-masing folder controller secara terpisah di PlatformIO, pilih environment `esp32dev`, lalu Build dan Upload ke board yang sesuai. Masing-masing `platformio.ini` memuat dependency yang diperlukan.

Verifikasi kalibrasi jarak ultrasonic, active level dan rating SSR, serta level tegangan ECHO/I²C sebelum menyalakan beban. Uji logika output dengan indikator tegangan rendah terlebih dahulu. Diagram jalur listrik pompa hanya konseptual karena rating pompa dan model SSR belum ditentukan.

## Dashboard V2.4

Hubungkan HP/laptop ke AP `PumpCtrl-Setup`, lalu buka `http://192.168.4.1`. Dashboard menyajikan Overview, Pump, Tank, Configuration, Device, dan System Logs tanpa internet. Tombol Save dan Restore Default menulis ke NVS masing-masing ESP32; Save dinyatakan berhasil hanya setelah Tank mengonfirmasi versinya dan Pump menyimpan konfigurasi yang sama. Riwayat log memakai ring buffer RAM terbatas.

API: `GET /api/v1/status`, `POST /api/v1/tank`, `POST /api/v1/control`, `GET/POST /api/v1/config`, `POST /api/v1/config/restore-defaults`, `GET /api/v1/device`, `GET /api/v1/logs`, dan `POST /api/v1/device/restart`. Endpoint Tank tetap menerima field V2.2; Tank V2.4 menambahkan distance, boot ID, sequence number, dan config version acknowledgment.

Dashboard tidak mengendalikan SSR langsung. Semua perintah masuk antrean dan diperiksa lagi oleh loop kontrol. Float switch tetap nonaktif pada V2.4, tetapi jalur status dan cutoff dipertahankan untuk aktivasi setelah perangkat fisik diverifikasi. Float melalui Wi-Fi bukan proteksi overflow independen.

Dokumentasi request, respons, rentang parameter, namespace NVS, dan proses konfirmasi dua perangkat ada di [`docs/api-v1.md`](docs/api-v1.md). Ganti password AP bawaan di konfigurasi Pump dan Tank Controller sebelum membuka kontrol dashboard ke pengguna lain.

## Pengujian manual V2.4

1. Flash kedua firmware V2.4. Hubungkan Tank ke Pump dan pastikan data valid/fresh.
2. Simpan beberapa nilai konfigurasi yang valid. Dashboard harus menampilkan sukses hanya sesudah respons/status mengonfirmasi NVS pada kedua ESP32.
3. Restart Pump lalu Tank secara terpisah; buka Configuration dan pastikan nilai tersimpan tetap muncul.
4. Matikan dan nyalakan kembali daya masing-masing board, lalu periksa nilai yang sama.
5. Coba nilai di luar rentang, stop <= start, kalibrasi terbalik, timeout terlalu kecil, dan tipe data salah; setiap perubahan harus ditolak dan konfigurasi aktif tidak berubah.
6. Tekan Restore Default, konfirmasi, lalu pastikan seluruh nilai kembali ke tabel default di `docs/api-v1.md` dan tetap demikian setelah restart.
7. Putuskan Tank sebelum Save; Pump harus menolak. Putuskan Tank setelah save mulai; dashboard tidak boleh menyatakan sukses dan status akhirnya gagal/timeout.
8. Pastikan AUTO/MANUAL, batas runtime default 25 menit, batas stop yang dapat diedit, SSR OFF setelah boot, dan float switch tetap disabled seperti sebelum V2.4.
