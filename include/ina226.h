#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Alamat 7-bit default dan rentang alamat yang dapat dipilih pada INA226. */
#define INA226_I2C_ADDRESS_DEFAULT 0x40
#define INA226_I2C_ADDRESS_MIN 0x40
#define INA226_I2C_ADDRESS_MAX 0x4F
/** Configuration Register default: AVG16, 588 us, shunt+bus kontinu. */
#define INA226_CONFIG_DEFAULT 0x44DF

/** Alamat register INA226; semua register berukuran 16 bit. */
#define INA226_REGISTER_CONFIGURATION 0x00
#define INA226_REGISTER_SHUNT_VOLTAGE 0x01
#define INA226_REGISTER_BUS_VOLTAGE 0x02
#define INA226_REGISTER_POWER 0x03
#define INA226_REGISTER_CURRENT 0x04
#define INA226_REGISTER_CALIBRATION 0x05
#define INA226_REGISTER_MASK_ENABLE 0x06
#define INA226_REGISTER_ALERT_LIMIT 0x07

/** Handle buram sensor; buat dan lepaskan melalui fungsi lifecycle INA226. */
typedef struct ina226_device *ina226_handle_t;

/** Pilihan jumlah sampel yang dirata-ratakan oleh ADC INA226. */
typedef enum {
    INA226_AVERAGING_1 = 0,     /**< Satu sampel. */
    INA226_AVERAGING_4,         /**< Empat sampel. */
    INA226_AVERAGING_16,        /**< Enam belas sampel. */
    INA226_AVERAGING_64,        /**< Enam puluh empat sampel. */
    INA226_AVERAGING_128,       /**< Seratus dua puluh delapan sampel. */
    INA226_AVERAGING_256,       /**< Dua ratus lima puluh enam sampel. */
    INA226_AVERAGING_512,       /**< Lima ratus dua belas sampel. */
    INA226_AVERAGING_1024,      /**< Seribu dua puluh empat sampel. */
} ina226_averaging_t;

/** Waktu konversi ADC yang dapat dipilih untuk kanal bus dan shunt. */
typedef enum {
    INA226_CONVERSION_TIME_140_US = 0, /**< 140 mikrodetik. */
    INA226_CONVERSION_TIME_204_US,     /**< 204 mikrodetik. */
    INA226_CONVERSION_TIME_332_US,     /**< 332 mikrodetik. */
    INA226_CONVERSION_TIME_588_US,     /**< 588 mikrodetik. */
    INA226_CONVERSION_TIME_1100_US,    /**< 1100 mikrodetik. */
    INA226_CONVERSION_TIME_2116_US,    /**< 2116 mikrodetik. */
    INA226_CONVERSION_TIME_4156_US,    /**< 4156 mikrodetik. */
    INA226_CONVERSION_TIME_8244_US,    /**< 8244 mikrodetik. */
} ina226_conversion_time_t;

/** Mode konversi yang didukung oleh Configuration Register INA226. */
typedef enum {
    INA226_MODE_POWER_DOWN = 0,            /**< Konversi berhenti. */
    INA226_MODE_SHUNT_TRIGGERED = 1,       /**< Konversi shunt saat dipicu. */
    INA226_MODE_BUS_TRIGGERED = 2,         /**< Konversi bus saat dipicu. */
    INA226_MODE_SHUNT_BUS_TRIGGERED = 3,   /**< Shunt lalu bus, satu rangkaian. */
    INA226_MODE_POWER_DOWN_ALTERNATE = 4,  /**< Encoding mode power-down alternatif. */
    INA226_MODE_SHUNT_CONTINUOUS = 5,      /**< Konversi shunt berulang. */
    INA226_MODE_BUS_CONTINUOUS = 6,        /**< Konversi bus berulang. */
    INA226_MODE_SHUNT_BUS_CONTINUOUS = 7,  /**< Shunt lalu bus berulang. */
} ina226_mode_t;

/** Pilihan satu komparator Alert Limit INA226. Batas daya menggunakan watt. */
typedef enum {
    INA226_ALERT_SHUNT_OVER_VOLTAGE = 0x8000,  /**< Shunt melewati batas atas. */
    INA226_ALERT_SHUNT_UNDER_VOLTAGE = 0x4000, /**< Shunt melewati batas bawah. */
    INA226_ALERT_BUS_OVER_VOLTAGE = 0x2000,    /**< Bus melewati batas atas. */
    INA226_ALERT_BUS_UNDER_VOLTAGE = 0x1000,   /**< Bus melewati batas bawah. */
    INA226_ALERT_POWER_OVER_LIMIT = 0x0800,    /**< Daya melewati batas. */
    INA226_ALERT_CONVERSION_READY = 0x0400,    /**< Konversi baru selesai. */
} ina226_alert_t;

/**
 * @brief Pengaturan ADC dan mode konversi INA226.
 */
typedef struct {
    ina226_averaging_t averaging;               /**< Jumlah sampel yang dirata-ratakan. */
    ina226_conversion_time_t bus_conversion_time;   /**< Waktu konversi kanal bus. */
    ina226_conversion_time_t shunt_conversion_time; /**< Waktu konversi kanal shunt. */
    ina226_mode_t mode;                         /**< Mode pemicu atau kontinu. */
} ina226_measurement_config_t;

/**
 * @brief Status dan pengaturan Mask/Enable Register.
 */
typedef struct {
    uint16_t enabled_alerts;        /**< Bit komparator yang sedang diaktifkan. */
    bool alert_active_high;         /**< Polaritas aktif-tinggi jika true. */
    bool alert_latched;             /**< Mode latch alert jika true. */
    bool alert_function_active;     /**< Indikator fungsi alert sedang aktif. */
    bool conversion_ready;          /**< Flag konversi selesai. */
    bool math_overflow;             /**< Flag overflow perhitungan internal. */
} ina226_alert_status_t;

/**
 * @brief Parameter komunikasi dan kalibrasi sensor.
 */
typedef struct {
    uint8_t i2c_address;                /**< Alamat 7-bit, 0x40 sampai 0x4F. */
    uint32_t i2c_frequency_hz;          /**< Kecepatan SCL untuk device ini. */
    uint32_t i2c_timeout_ms;            /**< Batas waktu transaksi I2C. */
    float shunt_resistance_ohms;        /**< Nilai shunt yang dipakai dalam kalibrasi. */
    float maximum_current_amps;         /**< Rentang desain untuk current-LSB; bukan proteksi. */
    uint16_t configuration;             /**< Nilai awal Configuration Register. */
} ina226_config_t;

/**
 * @brief Hasil pembacaan yang telah dikonversi ke satuan fisik.
 */
typedef struct {
    float bus_voltage_v;   /**< Tegangan bus dalam volt. */
    float shunt_voltage_v; /**< Tegangan shunt bertanda dalam volt. */
    float current_a;       /**< Arus bertanda dalam ampere. */
    float power_w;         /**< Daya dalam watt. */
} ina226_measurements_t;

/**
 * @brief Menghasilkan konfigurasi sensor bawaan tanpa melakukan transaksi I2C.
 *
 * @return Konfigurasi awal yang dapat disunting sebelum `ina226_create()`.
 */
ina226_config_t ina226_get_default_config(void);

/**
 * @brief Mendaftarkan INA226 sebagai perangkat pada bus I2C yang sudah dibuat.
 *
 * Fungsi menghitung serta menulis calibration register dan configuration
 * register. Handle bus tetap dimiliki aplikasi dan harus tetap hidup selama
 * handle sensor digunakan.
 *
 * @param[in] bus Handle bus I2C master bersama.
 * @param[in] config Alamat, kecepatan, timeout, shunt, rentang arus, dan konfigurasi.
 * @param[out] out_sensor Menerima handle sensor jika berhasil.
 * @return `ESP_OK` jika berhasil; kode error ESP-IDF jika parameter, alokasi,
 *         atau transaksi gagal.
 */
esp_err_t ina226_create(i2c_master_bus_handle_t bus,
                        const ina226_config_t *config,
                        ina226_handle_t *out_sensor);

/**
 * @brief Melepaskan perangkat INA226 dari bus dan membebaskan handle.
 *
 * @param[in] sensor Handle yang dibuat oleh `ina226_create()`.
 * @return `ESP_OK` jika berhasil; kode error ESP-IDF jika pelepasan gagal.
 */
esp_err_t ina226_delete(ina226_handle_t sensor);

/**
 * @brief Membaca bus, shunt, arus, dan daya dalam satuan fisik.
 *
 * Pengukuran berasal dari register konversi terakhir dan tidak dijamin diambil
 * pada saat yang persis sama.
 *
 * @param[in] sensor Handle sensor.
 * @param[out] measurements Menerima hasil hanya jika seluruh pembacaan berhasil.
 * @return `ESP_OK` jika berhasil; kode error I2C jika transaksi gagal.
 */
esp_err_t ina226_read_measurements(ina226_handle_t sensor,
                                   ina226_measurements_t *measurements);

/** @brief Membaca tegangan bus dalam volt. */
esp_err_t ina226_read_bus_voltage(ina226_handle_t sensor, float *voltage_v);
/** @brief Membaca tegangan shunt bertanda dalam volt. */
esp_err_t ina226_read_shunt_voltage(ina226_handle_t sensor, float *voltage_v);
/** @brief Membaca arus bertanda dalam ampere. */
esp_err_t ina226_read_current(ina226_handle_t sensor, float *current_a);
/** @brief Membaca daya dalam watt. */
esp_err_t ina226_read_power(ina226_handle_t sensor, float *power_w);

/**
 * @brief Menghitung dan menulis ulang kalibrasi arus INA226.
 *
 * Rentang arus menentukan skala resolusi perangkat lunak; fungsi ini bukan
 * pembatas arus atau proteksi listrik.
 *
 * @param[in] sensor Handle sensor.
 * @param[in] shunt_resistance_ohms Resistansi shunt efektif dalam ohm.
 * @param[in] maximum_current_amps Rentang arus desain dalam ampere.
 * @return `ESP_OK` jika register berhasil diperbarui; error jika nilai tidak
 *         valid atau transaksi gagal.
 */
esp_err_t ina226_set_calibration(ina226_handle_t sensor,
                                 float shunt_resistance_ohms,
                                 float maximum_current_amps);

/**
 * @brief Mengambil satu atau lebih nilai kalibrasi yang dikelola handle.
 *
 * Setidaknya satu pointer keluaran harus bukan `NULL`.
 *
 * @param[in] sensor Handle sensor.
 * @param[out] shunt_resistance_ohms Resistansi shunt dalam ohm, opsional.
 * @param[out] current_lsb_amps Skala ampere per bit, opsional.
 * @param[out] calibration_register Nilai register kalibrasi, opsional.
 * @return `ESP_OK` jika berhasil; `ESP_ERR_INVALID_ARG` jika semua keluaran NULL.
 */
esp_err_t ina226_get_calibration(ina226_handle_t sensor,
                                 float *shunt_resistance_ohms,
                                 float *current_lsb_amps,
                                 uint16_t *calibration_register);

/**
 * @brief Mengubah averaging, waktu konversi, dan mode operasi.
 *
 * Bit Configuration Register yang tidak dikelola API ini dipertahankan.
 *
 * @param[in] sensor Handle sensor.
 * @param[in] config Konfigurasi pengukuran yang enum-nya valid.
 * @return `ESP_OK` jika berhasil; error jika argumen atau transaksi tidak valid.
 */
esp_err_t ina226_set_measurement_config(ina226_handle_t sensor,
                                        const ina226_measurement_config_t *config);

/**
 * @brief Membaca pengaturan pengukuran aktual dari sensor.
 *
 * @param[in] sensor Handle sensor.
 * @param[out] config Menerima averaging, waktu konversi, dan mode.
 * @return `ESP_OK` jika berhasil; kode error I2C jika pembacaan gagal.
 */
esp_err_t ina226_get_measurement_config(ina226_handle_t sensor,
                                        ina226_measurement_config_t *config);

/**
 * @brief Mengubah mode operasi tanpa mengubah pilihan ADC lainnya.
 *
 * Mode triggered memulai rangkaian konversi ketika ditulis.
 *
 * @param[in] sensor Handle sensor.
 * @param[in] mode Mode operasi INA226 yang valid.
 * @return `ESP_OK` jika berhasil; error jika mode atau transaksi tidak valid.
 */
esp_err_t ina226_set_mode(ina226_handle_t sensor, ina226_mode_t mode);

/**
 * @brief Mengatur satu komparator alert beserta limit dan polaritasnya.
 *
 * `limit` memakai volt untuk alert tegangan dan watt untuk alert daya.
 * Untuk `INA226_ALERT_CONVERSION_READY`, nilai `limit` diabaikan.
 *
 * @param[in] sensor Handle sensor.
 * @param[in] alert Jenis satu-satunya alert yang akan diaktifkan.
 * @param[in] limit Ambang tegangan atau daya sesuai jenis alert.
 * @param[in] active_high Polaritas sinyal aktif pada pin ALERT.
 * @param[in] latched Mengaktifkan mode latch bila true.
 * @return `ESP_OK` jika register berhasil ditulis; error jika parameter atau
 *         transaksi tidak valid.
 */
esp_err_t ina226_set_alert(ina226_handle_t sensor,
                           ina226_alert_t alert,
                           float limit,
                           bool active_high,
                           bool latched);

/** @brief Menonaktifkan komparator alert dan mengembalikan polaritas/latch default. */
esp_err_t ina226_disable_alert(ina226_handle_t sensor);

/**
 * @brief Membaca status Mask/Enable Register.
 *
 * Pembacaan register ini dapat menghapus flag conversion-ready dan melepas
 * alert yang berada dalam mode latch.
 *
 * @param[in] sensor Handle sensor.
 * @param[out] status Menerima bit aktif, flag, polaritas, dan mode latch.
 * @return `ESP_OK` jika berhasil; kode error I2C jika pembacaan gagal.
 */
esp_err_t ina226_read_alert_status(ina226_handle_t sensor,
                                   ina226_alert_status_t *status);

/**
 * @brief Mereset INA226 dan memulihkan konfigurasi yang dikelola handle.
 *
 * Setelah reset perangkat, fungsi menulis ulang kalibrasi, konfigurasi,
 * alert limit, dan mask/enable yang terakhir diketahui oleh driver.
 *
 * @param[in] sensor Handle sensor.
 * @return `ESP_OK` jika seluruh langkah berhasil; error I2C pada kegagalan.
 */
esp_err_t ina226_reset(ina226_handle_t sensor);

/**
 * @brief Membaca register INA226 mentah pada alamat 0x00 sampai 0x07.
 *
 * Membaca register Mask/Enable memiliki efek samping yang sama dengan
 * `ina226_read_alert_status()`.
 *
 * @param[in] sensor Handle sensor.
 * @param[in] register_address Alamat register perangkat.
 * @param[out] value Menerima nilai register 16-bit.
 * @return `ESP_OK` jika berhasil; error jika argumen atau transaksi gagal.
 */
esp_err_t ina226_read_register(ina226_handle_t sensor,
                               uint8_t register_address,
                               uint16_t *value);

/**
 * @brief Menulis register konfigurasi INA226 yang dapat ditulis.
 *
 * Register pengukuran read-only ditolak. Untuk mengubah kalibrasi atau alert,
 * API tingkat tinggi lebih disarankan agar state internal tetap konsisten.
 *
 * @param[in] sensor Handle sensor.
 * @param[in] register_address Alamat Configuration, Calibration, Mask/Enable,
 *            atau Alert Limit.
 * @param[in] value Nilai register 16-bit.
 * @return `ESP_OK` jika berhasil; error jika register, nilai, atau transaksi
 *         tidak valid.
 */
esp_err_t ina226_write_register(ina226_handle_t sensor,
                                uint8_t register_address,
                                uint16_t value);

#ifdef __cplusplus
}
#endif
