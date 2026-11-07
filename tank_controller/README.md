# Water Pump Automation V2.3 — Tank Controller

Firmware untuk ESP32 DevKit V1 / ESP32-WROOM-32 sebagai Wi-Fi client. Firmware membaca AJ-SR04M dan float switch, menghitung persentase level, lalu mengirim data ke HTTP server di Pump Controller Tahap 1.

## Struktur

```text
tank_controller/
├── platformio.ini
├── include/
│   ├── config.h
│   ├── tank_client.h
│   └── tank_sensors.h
└── src/
    ├── main.cpp
    ├── tank_client.cpp
    └── tank_sensors.cpp
```

`TankSensors` mengelola pembacaan dan validasi sensor; `TankClient` mengelola Wi-Fi dan HTTP POST; `main.cpp` mengatur jadwal pembacaan/pengiriman. Untuk sementara float switch dinonaktifkan (`ENABLE_FLOAT_SWITCH=false`); payload selalu mengirim `float_full:false` dan level penuh ditentukan ultrasonic.

## Pin dan wiring

| ESP32 Tank | Sambungan |
|---|---|
| GPIO25 | AJ-SR04M TRIG |
| GPIO26 | AJ-SR04M ECHO melalui pembagi tegangan/level shifter jika ECHO 5 V |
| GPIO27 | Satu kabel float switch; kabel lainnya ke GND |
| 3V3 / GND | Daya logika sensor sesuai spesifikasi modul |
| GND | Ground bersama pada sisi tegangan rendah |

ESP32 GPIO tidak tahan 5 V. Verifikasi tegangan ECHO AJ-SR04M; jika keluarannya 5 V, gunakan pembagi tegangan atau level shifter sebelum GPIO26. Catu AJ-SR04M sesuai spesifikasi modul, jangan menyimpulkan dari nama model saja. AJ-SR04M memiliki beberapa mode; firmware ini mengasumsikan mode trigger/echo yang kompatibel HC-SR04. Pada sejumlah varian, mode diatur resistor R19. Pastikan board sensor benar-benar berada pada mode tersebut. Datasheet AJ-SR04M mencantumkan jarak minimum sekitar 20 cm, jadi penempatan sensor dan jarak permukaan saat tangki penuh perlu mempertimbangkan zona buta tersebut.

Float switch belum dipakai. GPIO27 tidak dibaca dan payload `float_full` selalu false. Setelah switch dibeli dan wiring diuji, ubah `ENABLE_FLOAT_SWITCH` menjadi `true` di konfigurasi Tank serta `ENABLE_FLOAT_SWITCH_CUTOFF` menjadi `true` di konfigurasi Pump. Default asumsi kontaknya tertutup saat pelampung bawah dan terbuka saat penuh. Ukur kontinuitas sebelum mengaktifkan fitur; sesuaikan `FLOAT_FULL_LEVEL` bila logikanya terbalik.

## Kalibrasi yang wajib dilakukan

Di `include/config.h`, ubah:

```cpp
DISTANCE_AT_EMPTY_CM = 100.0f;
DISTANCE_AT_FULL_CM = 25.0f;
```

Nilai di atas hanya contoh untuk membantu rumus; ukur jarak dari muka sensor ke permukaan air pada posisi kosong dan penuh yang aman di tangki aktual. Jarak kosong harus lebih besar dari jarak penuh, dan jarak terdekat harus memenuhi batas minimum sensor. Firmware mengirim `sensor_valid:false` bila kalibrasi belum masuk akal atau pembacaan ultrasonic gagal. Jangan gunakan persentase untuk kontrol sebelum kalibrasi diverifikasi.

Persentase dihitung dari jarak sensor ke air:

`level_percent = (jarak_kosong - jarak_terukur) / (jarak_kosong - jarak_penuh) × 100`

Hasil dibatasi pada 0–100%. `sensor_valid` melaporkan validitas ultrasonic; `float_full` selalu false selama switch dinonaktifkan.

## Komunikasi

Tank Controller bergabung ke AP Pump Controller `PumpCtrl-Setup` dan POST tiap pembacaan sensor (default setiap satu detik) ke `http://192.168.4.1/api/v1/tank` dengan format yang sama dengan Tahap 1:

```json
{"level_percent":34.5,"float_full":false,"sensor_valid":true}
```

Jika Wi-Fi terputus, firmware mencoba reconnect. Selama koneksi tidak ada, Pump Controller akan menghentikan AUTO ketika data terakhir melewati timeout 15 detik. Data float melalui jaringan tetap bukan interlock independen.

## Build dan upload

1. Ganti jarak kalibrasi dan verifikasi `FLOAT_FULL_LEVEL` di `include/config.h`.
2. Buka folder ini di PlatformIO, pilih environment `esp32dev`, lalu Build dan Upload ke board Tank Controller.
3. Buka Serial Monitor 115200 baud. Periksa Wi-Fi, jarak, level, validitas ultrasonic, status float, dan HTTP status `200`.
4. Sebelum integrasi pompa, uji sensor secara terpisah: ubah jarak target dan gerakkan pelampung sambil memeriksa log serta `GET /api/v1/status` pada Pump Controller.

Jangan menghubungkan beban pompa selama uji sensor/komunikasi ini. Tahap berikutnya adalah integrasi dua board dan uji alur kontrol serta kegagalan komunikasi.


## Kompatibilitas V2.3

Payload tetap mengirim tiga field wajib V2.2 dan menambahkan `distance_cm`, `boot_id`, `seq`, serta `firmware`. Pump Controller lama tetap bisa menerima payload lama, sedangkan Pump V2.3 menolak sequence duplikat/mundur bila boot ID dan sequence disertakan. Respons HTTP Pump membawa konfigurasi sensor sementara yang diterapkan Tank Controller: kalibrasi jarak kosong/penuh, jarak sensor min/maks, interval baca, dan filter median. Tidak ada server/dashboard yang berjalan pada Tank.

Nilai konfigurasi Tank diterima dari Pump melalui koneksi POST keluar yang sudah ada; Tank tidak perlu IP statis dan tidak membuka koneksi balik. Jika konfigurasi belum diterima, nilai compile-time di `include/config.h` tetap menjadi default. Float sensor tetap dinonaktifkan di V2.3 (`ENABLE_FLOAT_SWITCH=false`), namun pembacaan dan field protokol tetap disediakan untuk aktivasi setelah hardware terverifikasi.

Spesifikasi endpoint dan konfigurasi dapat dilihat di [`../docs/api-v1.md`](../docs/api-v1.md). Password Wi-Fi di konfigurasi Tank harus sama dengan AP Pump.
