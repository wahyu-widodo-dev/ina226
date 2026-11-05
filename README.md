# Panduan komponen INA226

Dokumen ini menjelaskan penggunaan driver INA226 di proyek ESP-IDF ini: nilai
default, arti konfigurasi pengukuran, urutan inisialisasi, kalibrasi, pembacaan,
alert, register, reset, dan penanganan error.

Driver berada di `include/ina226.h` dan `ina226.c`. Komponen ini menggunakan I2C master
ESP-IDF dan menerima handle bus yang dibuat aplikasi. Buat bus sekali, kemudian
gunakan bus tersebut untuk INA226 dan perangkat I2C lain yang alamatnya berbeda.
Untuk pengaturan port/controller, pin, pull-up, dan pembuatan bus terpisah,
lihat [dokumentasi komponen I2C bus](../i2c_bus/README.md).

Komponen ini ditujukan untuk ESP-IDF 5.3.0 atau lebih baru dan bukan driver
lintas Arduino/platform. Dependensi ESP-IDF dicantumkan di `CMakeLists.txt`,
sedangkan versi komponen, versi minimum ESP-IDF, dan lisensi MIT tercatat di
`idf_component.yml`.

## Konfigurasi yang dipakai contoh proyek

Contoh pada `main/main.c` menggunakan:

| Parameter | Nilai |
|---|---:|
| I2C address | `0x40` |
| Kecepatan I2C perangkat INA226 | `100000` Hz (100 kHz) |
| R-shunt efektif untuk kalibrasi | `0.00585` ohm (5.85 mOhm) |
| Rentang arus untuk kalibrasi | `20.0` A |
| Averaging | 16 sampel |
| Bus conversion time | 588 us |
| Shunt conversion time | 588 us |
| Mode | Shunt dan bus, continuous |

Pengaturan measurement diterapkan melalui `ina226_set_measurement_config()`
setelah `ina226_create()`. Jadi ketika contoh dijalankan, sensor mulai memakai
konfigurasi tabel di atas.

## Nilai default driver

`ina226_get_default_config()` mengisi konfigurasi driver, tetapi **tidak
membuat bus atau sensor**. Nilai ini dapat diedit sebelum memanggil
`ina226_create()`.

| Anggota `ina226_config_t` | Default | Keterangan |
|---|---:|---|
| `i2c_address` | `0x40` | Alamat I2C 7-bit INA226 (`0x40`–`0x4F`) |
| `i2c_frequency_hz` | `100000` | SCL untuk transaksi ke INA226 |
| `i2c_timeout_ms` | `1000` | Batas waktu transaksi I2C |
| `shunt_resistance_ohms` | `0.01` | Nilai shunt yang dipakai menghitung kalibrasi |
| `maximum_current_amps` | `20.0` | Rentang arus untuk menghitung current-LSB |
| `configuration` | `0x44DF` | Isi awal Configuration Register |

Default measurement yang dikodekan pada `0x44DF`:

| Bit register | Nilai | Dekode |
|---|---:|---|
| AVG `[11:9]` | `010` | 16 sampel |
| VBUSCT `[8:6]` | `011` | 588 us |
| VSHCT `[5:3]` | `011` | 588 us |
| MODE `[2:0]` | `111` | Shunt + bus continuous |

Bit reserved `[14:12]` pada nilai register dipertahankan. Nilai default
komponen ini sengaja disamakan dengan konfigurasi pengukuran yang diminta untuk
proyek. `ina226_set_measurement_config()` tetap dipanggil di `main.c` supaya
jelas dari kode aplikasi nilai apa yang dipakai dan supaya mudah diubah.

> **Penting:** `ina226_get_default_config()` awalnya mengisi R-shunt `0.01`
> ohm. Pastikan aplikasi menggantinya dengan nilai shunt sesuai modul sebelum
> membuat sensor. Contoh proyek menggantinya ke `0.00585` ohm berdasarkan
> kalibrasi sistem yang telah diuji. Nilai efektif tersebut bukan nilai fisik
> nominal resistor bertanda R004 (`0.004` ohm).

## Apa arti measurement configuration?

INA226 mempunyai satu Configuration Register. Empat pilihan di
`ina226_measurement_config_t` mengubah cara ADC mengumpulkan sampel:

### Averaging

`averaging` memilih banyaknya sampel internal yang dirata-ratakan untuk satu
hasil pengukuran:

| Enum | Jumlah sampel |
|---|---:|
| `INA226_AVERAGING_1` | 1 |
| `INA226_AVERAGING_4` | 4 |
| `INA226_AVERAGING_16` | 16 |
| `INA226_AVERAGING_64` | 64 |
| `INA226_AVERAGING_128` | 128 |
| `INA226_AVERAGING_256` | 256 |
| `INA226_AVERAGING_512` | 512 |
| `INA226_AVERAGING_1024` | 1024 |

Lebih banyak sampel biasanya mengurangi noise, tetapi memperlambat pembaruan
hasil. Averaging adalah pemrosesan internal INA226; bukan rata-rata beberapa
panggilan `ina226_read_measurements()` di aplikasi.

### Waktu konversi

Bus dan shunt mempunyai pengaturan waktu konversi terpisah. Masing-masing
menerima enum:

| Enum | Waktu |
|---|---:|
| `INA226_CONVERSION_TIME_140_US` | 140 us |
| `INA226_CONVERSION_TIME_204_US` | 204 us |
| `INA226_CONVERSION_TIME_332_US` | 332 us |
| `INA226_CONVERSION_TIME_588_US` | 588 us |
| `INA226_CONVERSION_TIME_1100_US` | 1100 us |
| `INA226_CONVERSION_TIME_2116_US` | 2116 us |
| `INA226_CONVERSION_TIME_4156_US` | 4156 us |
| `INA226_CONVERSION_TIME_8244_US` | 8244 us |

Waktu konversi lebih panjang memberi ADC waktu lebih lama untuk mengukur dan
umumnya menekan noise, tetapi menurunkan laju pembaruan. Pada mode pengukuran
shunt+bus, kedua konversi dilakukan berurutan.

### Mode operasi

| Enum | Perilaku |
|---|---|
| `INA226_MODE_POWER_DOWN` | Mematikan konversi untuk mengurangi konsumsi daya |
| `INA226_MODE_SHUNT_TRIGGERED` | Konversi shunt saat dipicu, lalu berhenti |
| `INA226_MODE_BUS_TRIGGERED` | Konversi bus saat dipicu, lalu berhenti |
| `INA226_MODE_SHUNT_BUS_TRIGGERED` | Satu rangkaian konversi shunt lalu bus |
| `INA226_MODE_SHUNT_CONTINUOUS` | Mengukur shunt terus-menerus |
| `INA226_MODE_BUS_CONTINUOUS` | Mengukur bus terus-menerus |
| `INA226_MODE_SHUNT_BUS_CONTINUOUS` | Mengukur shunt lalu bus terus-menerus |

Kode mode `INA226_MODE_POWER_DOWN_ALTERNATE` bernilai 4 mengikuti mode
power-down alternatif/reserved pada encoding perangkat; untuk pemakaian umum
gunakan `INA226_MODE_POWER_DOWN`.

### Default versus memanggil setter

Jika aplikasi membuat sensor dari `ina226_get_default_config()` dan tidak
memanggil `ina226_set_measurement_config()`, pengukuran tetap berjalan memakai
register `configuration` yang diberikan saat `ina226_create()`. Pada default
proyek saat ini, itu berarti AVG 16, bus 588 us, shunt 588 us, continuous.

Panggil `ina226_set_measurement_config()` bila ingin mengubah pengaturan itu
atau agar pilihan pengukuran tampak eksplisit di aplikasi. Fungsi ini hanya
mengubah bit AVG, waktu konversi, dan mode; field lain yang tidak dikelola API
dipertahankan.

## Perkiraan laju pengukuran untuk pengaturan saat ini

Untuk konfigurasi contoh:

```text
AVG = 16
tBUS = 588 us
tSHUNT = 588 us
MODE = shunt + bus continuous
```

Waktu satu pasangan konversi secara pendekatan:

```text
tPAIR = AVG × (tSHUNT + tBUS)
      = 16 × (588 us + 588 us)
      = 18,816 us, atau sekitar 18.8 ms
```

Ini setara dengan batas teoritis sekitar 53 pasangan hasil per detik sebelum
memperhitungkan overhead internal dan akses I2C. Aplikasi contoh membaca setiap
1000 ms, jadi log berubah sekitar sekali per detik walaupun sensor mengukur
jauh lebih sering. Membaca register berkali-kali sebelum konversi berikutnya
selesai dapat mengembalikan sampel yang sama.

## Urutan penggunaan yang disarankan

1. Buat bus I2C satu kali.
2. Ambil konfigurasi default, kemudian atur alamat, kecepatan, timeout, R-shunt,
   dan rentang arus sesuai perangkat keras.
3. Buat handle dengan `ina226_create()`.
4. Opsional: atur AVG, waktu konversi, dan mode dengan
   `ina226_set_measurement_config()`.
5. Baca data secara periodik dan cek setiap nilai balik fungsi.
6. Saat tidak lagi digunakan, hapus sensor terlebih dahulu lalu hapus bus.

## Contoh lengkap

Contoh berikut menunjukkan inisialisasi, konfigurasi pengukuran, pembacaan
berkala, serta pesan error. Pada ESP-IDF aplikasi biasanya melakukan langkah
yang sama di `app_main()`.

```c
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "ina226.h"

#define APP_I2C_SDA GPIO_NUM_15
#define APP_I2C_SCL GPIO_NUM_16
#define APP_INA226_ADDRESS 0x40
#define APP_SHUNT_OHMS 0.00585f
#define APP_MAX_CURRENT_A 20.0f

static const char *TAG = "ina226";

void app_main(void)
{
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_bus_create(APP_I2C_SDA, APP_I2C_SCL, &bus));

    ina226_config_t config = ina226_get_default_config();
    config.i2c_address = APP_INA226_ADDRESS;
    config.i2c_frequency_hz = 100000;
    config.i2c_timeout_ms = 1000;
    config.shunt_resistance_ohms = APP_SHUNT_OHMS;
    config.maximum_current_amps = APP_MAX_CURRENT_A;

    ina226_handle_t sensor = NULL;
    ESP_ERROR_CHECK(ina226_create(bus, &config, &sensor));

    const ina226_measurement_config_t measurement_config = {
        .averaging = INA226_AVERAGING_16,
        .bus_conversion_time = INA226_CONVERSION_TIME_588_US,
        .shunt_conversion_time = INA226_CONVERSION_TIME_588_US,
        .mode = INA226_MODE_SHUNT_BUS_CONTINUOUS,
    };
    ESP_ERROR_CHECK(ina226_set_measurement_config(sensor, &measurement_config));

    while (true) {
        ina226_measurements_t m;
        esp_err_t err = ina226_read_measurements(sensor, &m);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Vbus=%.3f V Vshunt=%.6f V I=%.3f A P=%.3f W",
                     m.bus_voltage_v, m.shunt_voltage_v,
                     m.current_a, m.power_w);
        } else {
            ESP_LOGE(TAG, "INA226 read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

`ESP_ERROR_CHECK()` menghentikan aplikasi jika inisialisasi gagal. Pada aplikasi
produksi yang perlu recovery tanpa restart, periksa `esp_err_t` secara manual
seperti pada loop pembacaan.

## Referensi API

### `ina226_get_default_config()`

Mengembalikan nilai awal `ina226_config_t`. Tidak melakukan I2C. Edit field
konfigurasi sebelum memanggil `ina226_create()`.

### `ina226_create(bus, config, &sensor)`

Menambahkan INA226 sebagai device pada bus yang sudah dibuat, menghitung dan
menulis Calibration Register, lalu menulis Configuration Register. `bus` harus
tetap hidup selama handle sensor dipakai.

Konfigurasi yang divalidasi meliputi alamat 7-bit INA226 (`0x40`–`0x4F`), frekuensi
I2C (`1`–`400000` Hz), timeout bukan nol, R-shunt positif, arus maksimum
positif, dan nilai kalibrasi yang muat pada register INA226. Output handle
diinisialisasi `NULL`; setelah sukses, handle siap dipakai.

### `ina226_read_measurements(sensor, &measurements)`

Membaca register bus, shunt, current, dan power, lalu mengubahnya ke satuan
volt, ampere, dan watt. Struktur output hanya disalin setelah semua transaksi
berhasil. Nilai-nilai tersebut berasal dari konversi terakhir sensor, bukan
pengukuran simultan pada satu saat persis.

### Pembacaan satu nilai

- `ina226_read_bus_voltage(sensor, &voltage_v)` — tegangan bus dalam volt.
- `ina226_read_shunt_voltage(sensor, &voltage_v)` — tegangan shunt bertanda
  dalam volt.
- `ina226_read_current(sensor, &current_a)` — arus bertanda dalam ampere.
- `ina226_read_power(sensor, &power_w)` — daya dalam watt.

Gunakan pembacaan satu nilai jika aplikasi hanya memerlukan satu register.
Setiap pemanggilan tetap melakukan transaksi I2C sendiri.

### `ina226_set_calibration()` dan `ina226_get_calibration()`

`ina226_set_calibration(sensor, shunt_ohms, max_current_a)` menulis ulang
Calibration Register. Jika berhasil, driver memperbarui current-LSB internal
yang dipakai untuk mengubah Current dan Power Register ke satuan fisik.

`max_current_a` adalah nilai desain untuk menentukan skala arus dan resolusi,
bukan pembatas atau proteksi arus. Pilih nilai di atas arus maksimum yang
diharapkan. Mengubah kalibrasi tidak mengubah waktu konversi atau mode.

`ina226_get_calibration()` menerima satu atau lebih output non-NULL untuk
membaca nilai R-shunt tersimpan, current-LSB dalam A/bit, dan nilai raw
Calibration Register.

Perhitungan internal memilih:

```text
requested current-LSB = max_current_a / 32768
CAL                   = 0.00512 / (requested current-LSB × R-shunt)
actual current-LSB    = 0.00512 / (CAL-register × R-shunt)
```

Karena CAL harus bilangan bulat, current-LSB aktual dapat sedikit berbeda dari
nilai permintaan. Untuk INA226, tegangan shunt terukur berada pada rentang
±81.92 mV; pastikan batas itu dan daya resistor shunt tidak terlampaui.

### Pengaturan measurement

`ina226_set_measurement_config(sensor, &config)` memvalidasi enum dan mengubah
AVG, VBUSCT, VSHCT, serta MODE. `ina226_get_measurement_config(sensor, &out)`
membaca konfigurasi aktual dari chip.

`ina226_set_mode(sensor, mode)` mengubah mode saja. Gunakan mode triggered bila
ingin memulai konversi sesuai permintaan: menulis salah satu mode triggered
memulai konversi. Untuk mengetahui kapan selesai, aktifkan alert
`INA226_ALERT_CONVERSION_READY` dan baca status conversion-ready, atau tunggu
setidaknya waktu konversi berdasarkan datasheet, termasuk faktor averaging.
Pembacaan Mask/Enable Register untuk status akan menghapus flag
conversion-ready.

### Alert

INA226 hanya memiliki **satu Alert Limit Register**. Pilih satu komparator
limit pada satu waktu. `ina226_set_alert(sensor, jenis, limit, active_high,
latched)` menulis nilai limit dan Mask/Enable Register.

| Jenis | Satuan `limit` | Catatan |
|---|---|---|
| `INA226_ALERT_SHUNT_OVER_VOLTAGE` | Volt | Limit shunt dapat bertanda |
| `INA226_ALERT_SHUNT_UNDER_VOLTAGE` | Volt | Limit shunt dapat bertanda |
| `INA226_ALERT_BUS_OVER_VOLTAGE` | Volt | Nilai bus non-negatif |
| `INA226_ALERT_BUS_UNDER_VOLTAGE` | Volt | Nilai bus non-negatif |
| `INA226_ALERT_POWER_OVER_LIMIT` | Watt | Limit daya non-negatif |
| `INA226_ALERT_CONVERSION_READY` | Tidak dipakai | Alert ketika conversion-ready |

Contoh:

```c
ESP_ERROR_CHECK(ina226_set_alert(sensor,
                                 INA226_ALERT_BUS_OVER_VOLTAGE,
                                 15.0f,  /* volt */
                                 true,   /* active high */
                                 false));/* non-latched */

ina226_alert_status_t status;
ESP_ERROR_CHECK(ina226_read_alert_status(sensor, &status));
if (status.alert_function_active) {
    ESP_LOGW(TAG, "Alert condition is active");
}
```

`ina226_read_alert_status()` melakukan read pada register `0x06`; operasi ini
mempunyai efek samping: flag conversion-ready dibersihkan, dan alert latched
dapat dilepas. `ina226_disable_alert()` menonaktifkan komparator dan
mengembalikan latch/polaritas ke nilai default.

Pin ALERT adalah keluaran open-drain/open-collector pada perangkat/modul.
Hubungkan ke GPIO input dengan pull-up ke tegangan logika yang aman untuk
ESP32-S3. Pastikan polaritas, rangkaian modul, dan kebutuhan pull-up sesuai
datasheet sebelum menghubungkan pin.

### Register mentah

`ina226_read_register()` dapat membaca alamat register `0x00`–`0x07`.
`ina226_write_register()` hanya mengizinkan Configuration, Calibration,
Mask/Enable, dan Alert Limit; register pengukuran bersifat read-only.

Utamakan API tingkat tinggi daripada menulis register mentah. Driver hanya
dapat menjaga state internal untuk pengaturan yang ditulis melalui API yang
sesuai. Sebagai contoh, gunakan `ina226_set_calibration()` alih-alih menulis
Calibration Register langsung. Membaca register `0x06` memiliki efek samping
yang sama dengan `ina226_read_alert_status()`.

### Reset

`ina226_reset(sensor)` mengirim reset perangkat, menunggu, kemudian
mengembalikan Configuration, Calibration, Alert Limit, dan Mask/Enable
berdasarkan nilai yang terakhir dikelola handle driver. Jika suatu register
diubah langsung lewat API raw, driver juga memperbarui state untuk register
tersebut.

### Hapus sensor dan bus

Hapus device INA226 sebelum menghapus bus:

```c
ESP_ERROR_CHECK(ina226_delete(sensor));
sensor = NULL;
ESP_ERROR_CHECK(i2c_bus_delete(bus));
bus = NULL;
```

Jangan gunakan handle setelah `ina226_delete()` sukses. Jika transaksi device
lain masih berjalan, hapus bus dapat gagal; tangani error tersebut sesuai
kebutuhan aplikasi.

## Penanganan error

Semua fungsi mengembalikan `esp_err_t`. Nilai umum:

| Error | Makna |
|---|---|
| `ESP_OK` | Operasi berhasil |
| `ESP_ERR_INVALID_ARG` | Handle, pointer, enum, alamat, atau nilai konfigurasi tidak valid |
| `ESP_ERR_NO_MEM` | Alokasi handle gagal |
| Error I2C lainnya | Transaksi atau pengelolaan device bus gagal |

Gunakan `esp_err_to_name(err)` untuk mencetak nama error. Jangan memakai data
output setelah fungsi pembacaan gagal; hanya gunakan hasil setelah status
`ESP_OK`.

## Catatan perangkat keras dan aplikasi

- Alamat yang diberikan adalah alamat I2C 7-bit (default `0x40`), bukan alamat
  yang sudah digeser dengan bit read/write.
- `i2c_frequency_hz` berlaku untuk INA226 sebagai device. Perangkat lain pada
  bus yang sama dapat memakai kecepatan device masing-masing sesuai kemampuan
  ESP-IDF dan seluruh perangkat/rangkaian bus.
- Pastikan SDA/SCL memiliki pull-up yang sesuai; pull-up internal ESP32 tidak
  selalu cukup.
- Arus maksimum kalibrasi bukan proteksi. Rancang shunt, jalur PCB, konektor,
  dan pendinginan sesuai arus nyata.
- Driver memakai operasi I2C blocking. Panggil dari task, bukan ISR.
- Jika satu handle diakses dari beberapa task, serialisasikan akses di aplikasi.
