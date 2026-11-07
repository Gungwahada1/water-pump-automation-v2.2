# Water Pump Automation V2.3 — Pump Controller

Firmware ESP32 Pump Controller untuk ESP32 DevKit V1 / ESP32-WROOM-32. Firmware menyediakan SoftAP, HTTP API pada task tersendiri, state machine AUTO/MANUAL, input tombol dan selector, serta output SSR dengan level OFF saat boot.

## Struktur

```text
pump_controller/
├── platformio.ini
├── include/
│   ├── config.h
│   ├── http_api.h
│   ├── input_manager.h
│   ├── pump_controller.h
│   ├── pump_types.h
│   └── status_display.h
└── src/
    ├── http_api.cpp
    ├── input_manager.cpp
    ├── main.cpp
    ├── pump_controller.cpp
    └── status_display.cpp
```

Kode dipisah menurut tanggung jawab: `InputManager` membaca tombol dan selector, `PumpController` menjalankan state machine dan output SSR, `HttpApi` menerima/melayani HTTP pada task terpisah, `StatusDisplay` menampilkan status LCD 20x4 pada task tersendiri, sementara `main.cpp` menginisialisasi dan mengorkestrasi modul. Sampel Tank masuk melalui FreeRTOS queue satu slot agar loop kontrol fisik tidak menunggu parsing HTTP atau penulisan LCD.

## Asumsi dan hal yang wajib diverifikasi sebelum pompa disambungkan

1. **SSR aktif-HIGH atau aktif-LOW.** `SSR_ACTIVE_LEVEL` default `HIGH`; cocokkan dengan modul SSR yang dipakai. Uji tegangan pin/output dan keadaan SSR tanpa pompa. Firmware mengatur output OFF sedini mungkin, tetapi keselamatan saat reset juga harus dibantu rangkaian hardware (pull-down/pull-up sesuai modul).
2. **Float switch sementara nonaktif.** `ENABLE_FLOAT_SWITCH_CUTOFF=false` membuat Pump Controller mengabaikan `float_full`. Aktifkan hanya setelah float switch dipasang dan logika kontaknya diverifikasi di konfigurasi kedua firmware.
3. **Skala ultrasonic.** Kontrak API memakai `level_percent` pada rentang 0–100. Konversi jarak ke persen dan validitas echo dilakukan Tank Controller. Nilai persentase harus diuji terhadap dimensi tangki.
4. **Prioritas MANUAL.** Untuk konfigurasi sementara, ultrasonic valid pada 100% mematikan pompa dalam AUTO maupun MANUAL. Tombol OFF dan timeout durasi juga menghentikan pompa. AUTO turut berhenti jika data stale atau ultrasonic invalid.
5. **Jaringan.** SoftAP tanpa router/internet; kredensial default berada di `include/config.h`. Perangkat Tank Controller harus bergabung ke AP ini dan POST heartbeat/data secara berkala.
6. **Interlock.** Float yang dikirim melalui Wi-Fi bukan interlock independen. Hilangnya daya, hang, gangguan radio, atau server gagal dapat membuat pesan tidak sampai. Untuk proteksi overflow nyata gunakan pemutus level independen/hardwired atau rangkaian keselamatan terpisah yang memutus koil/daya pompa. SSR dan instalasi listrik harus dipilih/dipasang oleh personel kompeten.

## Wiring / pin map (ESP32 DevKit V1)

| Fungsi | GPIO | Koneksi |
|---|---:|---|
| SSR input | 25 | Ke input kontrol SSR; GND bersama jika modul DC non-isolated memerlukannya |
| Tombol PUMP ON | 26 | Tombol NO antara GPIO dan GND |
| Tombol PUMP OFF | 27 | Tombol NO antara GPIO dan GND |
| Selector AUTO/MANUAL | 14 | Saklar menghubungkan GPIO ke GND untuk MANUAL; terbuka = AUTO |
| LCD SDA | 21 | Ke SDA melalui level shifter I²C bila backpack memakai pull-up 5 V |
| LCD SCL | 22 | Ke SCL melalui level shifter I²C bila backpack memakai pull-up 5 V |
| LED status (opsional) | 2 | LED onboard pada banyak DevKit; dapat dinonaktifkan/diubah |
| Ground | GND | Ground logika sesuai kebutuhan modul SSR |

Semua input tombol memakai pull-up internal dan aktif-LOW. Jangan hubungkan tegangan listrik/tegangan koil pompa ke GPIO. Ground bersama hanya untuk sisi kontrol tegangan rendah dan mengikuti isolasi/modul SSR. Float switch belum dipasang pada board ini.

### LCD I²C 20x4

Sambungkan LCD backpack ke SDA GPIO21 dan SCL GPIO22. Beri daya LCD sesuai spesifikasi backpack (umumnya 5 V), dan satukan GND sisi logika. Banyak backpack menarik SDA/SCL ke VCC; ukur levelnya. Jika pull-up berada pada 5 V, gunakan level shifter I²C dua arah agar GPIO ESP32 hanya menerima 3,3 V. Kelas `StatusDisplay` memakai library `hd44780_I2Cexp`, yang mendeteksi alamat dan mapping backpack umum secara otomatis. Bila LCD tidak terdeteksi, cek Serial Monitor untuk `[LCD][ERROR]`, periksa supply/wiring, lalu jalankan contoh `I2CexpDiag` dari library.

Isi layar yang ditampilkan:

| Baris | Informasi |
|---|---|
| 1 | Mode AUTO/MANUAL dan output pompa ON/OFF |
| 2 | Persentase air; tanda LOW pada ambang start atau FULL pada 100% |
| 3 | Data Tank OK/STALE/NO DATA/SENSOR ERROR dan umur data |
| 4 | Durasi pompa berjalan atau alasan stop/menunggu/lockout; cutoff 100% tampil sebagai `STOP: ULTRASONIC` |

LCD diperbarui setiap 500 ms di task terpisah dan hanya menulis baris yang berubah. Gangguan atau LCD yang tidak terpasang tidak menghentikan logika pompa.

## Perilaku kontrol

- AUTO: data valid dan segar diperlukan. Pompa mulai ketika `level_percent <= AUTO_START_LEVEL_PERCENT`. Setelah mulai, pompa tetap ON meski level melewati ambang bawah; untuk konfigurasi sementara, berhenti saat pembacaan ultrasonic valid mencapai `ULTRASONIC_FULL_STOP_PERCENT` (100%). Float cutoff dinonaktifkan.
- AUTO mematikan pompa jika data belum pernah diterima, data lebih lama dari timeout, atau ultrasonic invalid.
- MANUAL: tombol ON menyalakan dan tombol OFF mematikan (latch) selama selector pada MANUAL. Tombol OFF bekerja di kedua mode. Ultrasonic valid 100% dan proteksi durasi maksimum tetap mematikan pompa. Komunikasi tidak diperlukan untuk memulai MANUAL jika belum ada status penuh yang diterima.
- Input fisik dibaca oleh loop kontrol utama dengan debounce 15 ms dan jeda loop 2 ms. HTTP berjalan pada task FreeRTOS core 0; penanganan request HTTP tidak menahan loop tombol. Loop menerima data sensor secara non-blocking. Jika ON dan OFF terbaca bersamaan, OFF menang. Untuk pemutusan darurat yang harus tetap bekerja saat ESP32 hang atau kehilangan daya, gunakan pemutus hardware terpisah.
- Mengubah mode ke AUTO memulai evaluasi otomatis dari kondisi sensor terbaru.
- Proteksi durasi: setelah `MAX_PUMP_RUNTIME_MS`, pompa mati dan terkunci sampai board di-reset. Tombol OFF saja tidak menghapus lockout.
- Pada boot, output SSR diset OFF sebelum Wi-Fi/server dimulai.

## HTTP API Tahap 1

`POST http://192.168.4.1/api/v1/tank` dengan `Content-Type: application/json`:

```json
{
  "level_percent": 34.5,
  "float_full": false,
  "sensor_valid": true
}
```

POST yang berhasil memperbarui waktu penerimaan data. `sensor_valid:false` diterima sebagai status invalid dan mematikan pompa pada AUTO. Rentang level yang diterima 0–100. Endpoint `GET /api/v1/status` juga melaporkan `ultrasonic_full` dan `stop_reason`.

## Build dan upload

1. Install VS Code + ekstensi PlatformIO, atau PlatformIO Core.
2. Buka folder `outputs/pump_controller` sebagai proyek PlatformIO.
3. Edit `include/config.h`: SSID/password AP, ambang, timeout, runtime maksimum, dan active level SSR.
4. Sambungkan board dengan kabel USB data, pilih port serial, lalu Build dan Upload environment `esp32dev`.
5. Buka Serial Monitor 115200 baud. Catat alamat AP `192.168.4.1` dan alamat tersebut sebagai alamat API.

### Debugging lewat Serial Monitor

Firmware mencetak log pada 115200 baud. Setelah upload, tekan tombol EN/RESET bila perlu. Log yang diharapkan:

```text
========================================
 Water Pump Automation V2.2
 ESP32 Pump Controller - booting
========================================
[BOOT] SSR output initialized OFF ...
[WIFI] SoftAP READY: SSID=PumpCtrl-Setup IP=192.168.4.1 ...
[HTTP] server task ready: POST /api/v1/tank, GET /api/v1/status
[READY] HTTP server starting on port 80 (separate task)
[STATUS] mode=AUTO pump=OFF ... tank_received=no ... stop_reason=no_data
```

Setelah Tank Controller mengirim data, log `[HTTP] tank data accepted` akan menampilkan level, validitas ultrasonic, dan status float. Setiap 3 detik `[STATUS]` merangkum koneksi client, umur data, mode, status pompa, lockout, dan alasan stop. Perubahan mode/tombol dan output pompa dicatat sebagai `[INPUT]`, `[CONTROL]`, `[PUMP]`, atau `[SAFETY]`.

Jika monitor kosong: pastikan board/COM port yang benar dipilih, baud rate **115200**, kabel USB mendukung data (bukan hanya charging), dan firmware terbaru sudah di-upload. Tutup monitor serial lain yang sedang memakai port tersebut, lalu tekan EN/RESET. Bila boot log muncul tetapi tidak ada `[HTTP] tank data accepted`, pastikan client tersambung ke `PumpCtrl-Setup` dan melakukan POST ke `192.168.4.1/api/v1/tank`.

## Pengujian Tahap 1 (tanpa pompa terlebih dahulu)

1. Dengan SSR belum tersambung ke beban, reset board dan pastikan output kontrol berada pada level OFF sesuai modul SSR.
2. Hubungkan ponsel/laptop ke Wi-Fi `PumpCtrl-Setup` (password awal `pumpcontrol22`), lalu buka `http://192.168.4.1/api/v1/status`.
3. Dengan API client, kirim payload contoh di atas. Pastikan respons HTTP 200 dan status sensor menjadi fresh.
4. Set selector AUTO, kirim `level_percent` di atas ambang lalu pastikan pompa tetap OFF; kirim nilai di bawah/sama ambang dan pastikan output menjadi ON.
5. Kirim level di atas ambang dengan `float_full:false`: output harus tetap ON. Karena float cutoff nonaktif, `float_full:true` harus diabaikan. Kirim `level_percent:100` dengan `sensor_valid:true`: output harus OFF.
6. Uji `sensor_valid:false`, payload invalid, dan hentikan POST melewati timeout: AUTO harus OFF.
7. Set selector MANUAL. Tombol ON harus ON, tombol OFF harus OFF. `float_full:true` harus diabaikan sementara; `level_percent:100` dengan `sensor_valid:true` harus mematikan pompa di MANUAL.
8. Saat POST data dikirim berulang, tekan tombol OFF. Output harus merespons tanpa menunggu request selesai; tombol OFF menang bila ON dan OFF ditekan bersama.
9. Uji batas runtime dengan konfigurasi pendek hanya pada beban uji tegangan rendah atau LED; pastikan pompa terkunci OFF setelah batas tercapai.
10. Baru setelah semua uji logika lulus, teknisi yang kompeten dapat memverifikasi SSR dan instalasi beban secara terpisah.

Contoh uji POST memakai curl:

```sh
curl -i -X POST http://192.168.4.1/api/v1/tank \
  -H 'Content-Type: application/json' \
  -d '{"level_percent":34.5,"float_full":false,"sensor_valid":true}'
```


## V2.3 Local Dashboard

Buka `http://192.168.4.1` setelah tersambung ke SoftAP. Dashboard dipisahkan di `src/web_dashboard.cpp`; HTTP API tetap di `src/http_api.cpp`; ring log RAM di `src/system_log.cpp`. UI memakai polling 1,5 detik dan tidak membutuhkan CDN/internet.

Dashboard ON hanya diterima pada MANUAL fisik dengan data Tank segar/valid, level belum 100%, dan tanpa lockout. Persyaratan dicek di API dan dicek ulang oleh state machine; jika data menjadi stale/invalid saat permintaan dashboard memegang pompa, pompa dihentikan. OFF tidak bergantung pada mode dan diprioritaskan di antrean perintah dashboard. Tombol OFF fisik tetap diproses langsung oleh loop kontrol.

`GET/POST /api/v1/config` mengubah konfigurasi Pump dan kalibrasi sensor Tank sementara di RAM, dengan rentang tervalidasi. Tank menerima konfigurasi kalibrasi, jarak sensor, interval pembacaan, dan filter dalam respons POST pembacaan sensor berikutnya; tidak dibutuhkan koneksi balik ke alamat DHCP Tank. Nilai belum disimpan ke Preferences/NVS dan hilang setelah reboot.

Endpoint tambahan: `POST /api/v1/control`, `GET /api/v1/device`, `GET /api/v1/logs`, dan `POST /api/v1/device/restart`. Log berisi maksimum 24 event berbasis uptime. Restart diarahkan ke loop kontrol untuk mengatur SSR OFF sebelum reboot.

Float switch sengaja tetap nonaktif melalui `ENABLE_FLOAT_SWITCH_CUTOFF=false`. Payload dan logika cutoff tetap dipertahankan untuk aktivasi kemudian; verifikasi kontak dan wiring sebelum diaktifkan.

Endpoint dan rentang validasi rinci tersedia di [`../docs/api-v1.md`](../docs/api-v1.md). Ganti password AP bawaan di `include/config.h` dan samakan nilainya pada `tank_controller/include/config.h` sebelum menggunakan kontrol dashboard di luar pengujian pribadi.


### Checklist tambahan dashboard V2.3

1. Sambungkan satu HP/laptop ke AP bersama Tank Controller dan buka `http://192.168.4.1`; pastikan halaman tampil tanpa akses internet. AP mengizinkan maksimal dua client, sehingga Tank dan satu client dashboard dapat tersambung bersamaan.
2. Pastikan level, jarak, status sensor, mode, pompa, dan umur data berubah dari pembacaan Tank. Putuskan Tank dan pastikan status menjadi STALE setelah timeout serta nilai level tidak ditampilkan sebagai nilai terkini.
3. Uji dashboard ON pada AUTO (harus ditolak), pada MANUAL dengan data segar/valid (dapat diterima), dan dengan data tidak ada/stale/invalid/100% (harus ditolak). Lakukan tanpa beban pompa, gunakan indikator tegangan rendah pada sisi output SSR.
4. Saat permintaan dashboard menyalakan pompa, putuskan data Tank; pastikan Pump Controller mematikan output. Uji dashboard OFF pada kedua mode. Uji juga tombol OFF fisik saat dashboard melakukan polling.
5. Ubah konfigurasi dengan nilai valid, cek nilai kembali melalui `GET /api/v1/config`, lalu pastikan Tank menerapkan kalibrasi/interval/filter setelah pembacaan selanjutnya. Kirim nilai di luar rentang dan pastikan ditolak. Restart dan pastikan konfigurasi RAM kembali ke default.
6. Periksa Device dan Logs, lalu uji restart dengan output SSR tanpa beban dan ukur bahwa OFF sebelum boot berikutnya. Jangan menguji restart saat beban pompa terpasang.
7. Tutup browser selama AUTO berjalan dan pastikan kontrol otomatis tetap aktif. Uji koneksi ulang AP dan komunikasi Tank. Pengujian operasi 72 jam/7 hari belum dilakukan dan perlu dijalankan pada hardware sebelum operasi permanen.

LCD line 4 now reads the active runtime limit and AUTO threshold from the same live settings snapshot as the controller. Example while running: `RUN:02:15 / 05:00`; lockout also shows the configured minute value.
