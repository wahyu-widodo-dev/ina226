#include "ina226.h"

#include <math.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_rom_sys.h"

#define INA226_REG_CONFIG INA226_REGISTER_CONFIGURATION
#define INA226_REG_SHUNT INA226_REGISTER_SHUNT_VOLTAGE
#define INA226_REG_BUS INA226_REGISTER_BUS_VOLTAGE
#define INA226_REG_POWER INA226_REGISTER_POWER
#define INA226_REG_CURRENT INA226_REGISTER_CURRENT
#define INA226_REG_CALIBRATION INA226_REGISTER_CALIBRATION
#define INA226_REG_MASK_ENABLE INA226_REGISTER_MASK_ENABLE
#define INA226_REG_ALERT_LIMIT INA226_REGISTER_ALERT_LIMIT

#define INA226_CONFIG_RESET 0x8000
#define INA226_CONFIG_AVERAGING_MASK 0x0E00
#define INA226_CONFIG_BUS_CONVERSION_MASK 0x01C0
#define INA226_CONFIG_SHUNT_CONVERSION_MASK 0x0038
#define INA226_CONFIG_MODE_MASK 0x0007

#define INA226_MASK_ALERT_FUNCTION_ACTIVE 0x0010
#define INA226_MASK_CONVERSION_READY_FLAG 0x0008
#define INA226_MASK_MATH_OVERFLOW 0x0004
#define INA226_MASK_ALERT_POLARITY 0x0002
#define INA226_MASK_ALERT_LATCH_ENABLE 0x0001
#define INA226_MASK_ALERT_SELECTION 0xFC00

#define INA226_DEFAULT_TIMEOUT_MS 1000
#define INA226_SHUNT_VOLTAGE_LSB_V 0.0000025f
#define INA226_BUS_VOLTAGE_LSB_V 0.00125f
#define INA226_CALIBRATION_CONSTANT 0.00512f
#define INA226_CONVERSION_READY_DELAY_US 1000

struct ina226_device {
    i2c_master_dev_handle_t i2c_device;
    float shunt_resistance_ohms;
    float current_lsb_amps;
    uint32_t timeout_ms;
    uint16_t configuration;
    uint16_t calibration_register;
    uint16_t mask_enable;
    uint16_t alert_limit;
};

static esp_err_t write_register(ina226_handle_t sensor, uint8_t reg, uint16_t value)
{
    /* INA226 mengirim byte paling signifikan lebih dahulu pada setiap register. */
    const uint8_t data[] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };

    return i2c_master_transmit(sensor->i2c_device,
                               data,
                               sizeof(data),
                               sensor->timeout_ms);
}

static esp_err_t read_register(ina226_handle_t sensor, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t err = i2c_master_transmit_receive(sensor->i2c_device,
                                                 &reg,
                                                 sizeof(reg),
                                                 data,
                                                 sizeof(data),
                                                 sensor->timeout_ms);
    if (err == ESP_OK) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }
    return err;
}

static bool valid_conversion_time(ina226_conversion_time_t time)
{
    return (unsigned)time <= INA226_CONVERSION_TIME_8244_US;
}

static bool valid_measurement_config(const ina226_measurement_config_t *config)
{
    return config != NULL &&
           (unsigned)config->averaging <= INA226_AVERAGING_1024 &&
           valid_conversion_time(config->bus_conversion_time) &&
           valid_conversion_time(config->shunt_conversion_time) &&
           (unsigned)config->mode <= INA226_MODE_SHUNT_BUS_CONTINUOUS;
}

static esp_err_t configure_alert_register(ina226_handle_t sensor,
                                          uint16_t enabled_alerts,
                                          bool active_high,
                                          bool latched)
{
    uint16_t mask_enable = enabled_alerts & INA226_MASK_ALERT_SELECTION;
    if (latched) {
        mask_enable |= INA226_MASK_ALERT_LATCH_ENABLE;
    }
    if (active_high) {
        mask_enable |= INA226_MASK_ALERT_POLARITY;
    }

    esp_err_t err = write_register(sensor, INA226_REG_MASK_ENABLE, mask_enable);
    if (err == ESP_OK) {
        sensor->mask_enable = mask_enable;
    }
    return err;
}

ina226_config_t ina226_get_default_config(void)
{
    return (ina226_config_t) {
        .i2c_address = INA226_I2C_ADDRESS_DEFAULT,
        .i2c_frequency_hz = 100000,
        .i2c_timeout_ms = INA226_DEFAULT_TIMEOUT_MS,
        .shunt_resistance_ohms = 0.01f,
        .maximum_current_amps = 20.0f,
        .configuration = INA226_CONFIG_DEFAULT,
    };
}

esp_err_t ina226_create(i2c_master_bus_handle_t bus,
                        const ina226_config_t *config,
                        ina226_handle_t *out_sensor)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, "ina226", "bus is NULL");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, "ina226", "config is NULL");
    ESP_RETURN_ON_FALSE(out_sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "out_sensor is NULL");
    *out_sensor = NULL;
    ESP_RETURN_ON_FALSE(config->i2c_address >= INA226_I2C_ADDRESS_MIN &&
                            config->i2c_address <= INA226_I2C_ADDRESS_MAX,
                        ESP_ERR_INVALID_ARG,
                        "ina226",
                        "I2C address must be in the INA226 range 0x40-0x4F");
    ESP_RETURN_ON_FALSE(config->i2c_frequency_hz > 0 && config->i2c_frequency_hz <= 400000,
                        ESP_ERR_INVALID_ARG, "ina226", "I2C frequency must be between 1 Hz and 400 kHz");
    ESP_RETURN_ON_FALSE(config->i2c_timeout_ms > 0,
                        ESP_ERR_INVALID_ARG, "ina226", "I2C timeout must be greater than zero");
    ESP_RETURN_ON_FALSE(isfinite(config->shunt_resistance_ohms) &&
                            config->shunt_resistance_ohms > 0.0f,
                        ESP_ERR_INVALID_ARG, "ina226", "shunt resistance must be positive");
    ESP_RETURN_ON_FALSE(isfinite(config->maximum_current_amps) &&
                            config->maximum_current_amps > 0.0f,
                        ESP_ERR_INVALID_ARG, "ina226", "maximum current must be positive");
    ESP_RETURN_ON_FALSE((config->configuration & INA226_CONFIG_RESET) == 0,
                        ESP_ERR_INVALID_ARG, "ina226", "reset bit cannot be set in configuration");

    const float requested_current_lsb = config->maximum_current_amps / 32768.0f;
    const float calibration_float =
        INA226_CALIBRATION_CONSTANT /
        (requested_current_lsb * config->shunt_resistance_ohms);
    ESP_RETURN_ON_FALSE(isfinite(calibration_float) &&
                            calibration_float >= 1.0f &&
                            calibration_float <= 65535.0f,
                        ESP_ERR_INVALID_ARG, "ina226", "calibration value is out of range");
    const uint16_t calibration_register = (uint16_t)(calibration_float + 0.5f);

    ina226_handle_t sensor = calloc(1, sizeof(*sensor));
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_NO_MEM, "ina226", "failed to allocate sensor");
    sensor->timeout_ms = config->i2c_timeout_ms;
    sensor->shunt_resistance_ohms = config->shunt_resistance_ohms;
    sensor->calibration_register = calibration_register;
    sensor->current_lsb_amps =
        INA226_CALIBRATION_CONSTANT /
        ((float)calibration_register * config->shunt_resistance_ohms);
    sensor->configuration = config->configuration;

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = config->i2c_frequency_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &sensor->i2c_device);
    if (err == ESP_OK) {
        err = write_register(sensor, INA226_REG_CALIBRATION, sensor->calibration_register);
    }
    if (err == ESP_OK) {
        err = write_register(sensor, INA226_REG_CONFIG, sensor->configuration);
    }
    if (err != ESP_OK) {
        if (sensor->i2c_device != NULL) {
            esp_err_t remove_err = i2c_master_bus_rm_device(sensor->i2c_device);
            if (remove_err != ESP_OK) {
                free(sensor);
                return remove_err;
            }
        }
        free(sensor);
        return err;
    }

    *out_sensor = sensor;
    return ESP_OK;
}

esp_err_t ina226_delete(ina226_handle_t sensor)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    esp_err_t err = i2c_master_bus_rm_device(sensor->i2c_device);
    if (err == ESP_OK) {
        free(sensor);
    }
    return err;
}

esp_err_t ina226_read_register(ina226_handle_t sensor,
                               uint8_t register_address,
                               uint16_t *value)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(value != NULL, ESP_ERR_INVALID_ARG, "ina226", "value is NULL");
    ESP_RETURN_ON_FALSE(register_address <= INA226_REG_ALERT_LIMIT,
                        ESP_ERR_INVALID_ARG, "ina226", "invalid register address");
    return read_register(sensor, register_address, value);
}

esp_err_t ina226_write_register(ina226_handle_t sensor,
                                uint8_t register_address,
                                uint16_t value)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(register_address == INA226_REG_CONFIG ||
                            register_address == INA226_REG_CALIBRATION ||
                            register_address == INA226_REG_MASK_ENABLE ||
                            register_address == INA226_REG_ALERT_LIMIT,
                        ESP_ERR_INVALID_ARG, "ina226", "register is read-only or invalid");
    ESP_RETURN_ON_FALSE(register_address != INA226_REG_CONFIG ||
                            (value & INA226_CONFIG_RESET) == 0,
                        ESP_ERR_INVALID_ARG, "ina226", "use ina226_reset to reset the device");
    ESP_RETURN_ON_FALSE(register_address != INA226_REG_CALIBRATION || value != 0,
                        ESP_ERR_INVALID_ARG, "ina226", "calibration register must be nonzero");

    esp_err_t err = write_register(sensor, register_address, value);
    if (err == ESP_OK) {
        if (register_address == INA226_REG_CONFIG) {
            sensor->configuration = value;
        } else if (register_address == INA226_REG_CALIBRATION) {
            sensor->calibration_register = value;
            sensor->current_lsb_amps =
                INA226_CALIBRATION_CONSTANT /
                ((float)value * sensor->shunt_resistance_ohms);
        } else if (register_address == INA226_REG_MASK_ENABLE) {
            sensor->mask_enable = value & 0xFC03;
        } else if (register_address == INA226_REG_ALERT_LIMIT) {
            sensor->alert_limit = value;
        }
    }
    return err;
}

esp_err_t ina226_read_measurements(ina226_handle_t sensor,
                                   ina226_measurements_t *measurements)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(measurements != NULL, ESP_ERR_INVALID_ARG, "ina226", "measurements is NULL");

    uint16_t bus_raw;
    uint16_t shunt_raw;
    uint16_t current_raw;
    uint16_t power_raw;
    esp_err_t err = read_register(sensor, INA226_REG_BUS, &bus_raw);
    if (err != ESP_OK) {
        return err;
    }
    err = read_register(sensor, INA226_REG_SHUNT, &shunt_raw);
    if (err != ESP_OK) {
        return err;
    }
    err = read_register(sensor, INA226_REG_CURRENT, &current_raw);
    if (err != ESP_OK) {
        return err;
    }
    err = read_register(sensor, INA226_REG_POWER, &power_raw);
    if (err != ESP_OK) {
        return err;
    }

    const ina226_measurements_t result = {
        .bus_voltage_v = bus_raw * INA226_BUS_VOLTAGE_LSB_V,
        .shunt_voltage_v = ((int16_t)shunt_raw) * INA226_SHUNT_VOLTAGE_LSB_V,
        .current_a = ((int16_t)current_raw) * sensor->current_lsb_amps,
        .power_w = power_raw * 25.0f * sensor->current_lsb_amps,
    };
    *measurements = result;
    return ESP_OK;
}

esp_err_t ina226_read_bus_voltage(ina226_handle_t sensor, float *voltage_v)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(voltage_v != NULL, ESP_ERR_INVALID_ARG, "ina226", "voltage_v is NULL");
    uint16_t raw;
    esp_err_t err = read_register(sensor, INA226_REG_BUS, &raw);
    if (err == ESP_OK) {
        *voltage_v = raw * INA226_BUS_VOLTAGE_LSB_V;
    }
    return err;
}

esp_err_t ina226_read_shunt_voltage(ina226_handle_t sensor, float *voltage_v)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(voltage_v != NULL, ESP_ERR_INVALID_ARG, "ina226", "voltage_v is NULL");
    uint16_t raw;
    esp_err_t err = read_register(sensor, INA226_REG_SHUNT, &raw);
    if (err == ESP_OK) {
        *voltage_v = ((int16_t)raw) * INA226_SHUNT_VOLTAGE_LSB_V;
    }
    return err;
}

esp_err_t ina226_read_current(ina226_handle_t sensor, float *current_a)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(current_a != NULL, ESP_ERR_INVALID_ARG, "ina226", "current_a is NULL");
    uint16_t raw;
    esp_err_t err = read_register(sensor, INA226_REG_CURRENT, &raw);
    if (err == ESP_OK) {
        *current_a = ((int16_t)raw) * sensor->current_lsb_amps;
    }
    return err;
}

esp_err_t ina226_read_power(ina226_handle_t sensor, float *power_w)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(power_w != NULL, ESP_ERR_INVALID_ARG, "ina226", "power_w is NULL");
    uint16_t raw;
    esp_err_t err = read_register(sensor, INA226_REG_POWER, &raw);
    if (err == ESP_OK) {
        *power_w = raw * 25.0f * sensor->current_lsb_amps;
    }
    return err;
}

esp_err_t ina226_set_calibration(ina226_handle_t sensor,
                                 float shunt_resistance_ohms,
                                 float maximum_current_amps)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(isfinite(shunt_resistance_ohms) && shunt_resistance_ohms > 0.0f,
                        ESP_ERR_INVALID_ARG, "ina226", "shunt resistance must be positive");
    ESP_RETURN_ON_FALSE(isfinite(maximum_current_amps) && maximum_current_amps > 0.0f,
                        ESP_ERR_INVALID_ARG, "ina226", "maximum current must be positive");

    const float requested_lsb = maximum_current_amps / 32768.0f;
    const float calibration_float =
        INA226_CALIBRATION_CONSTANT / (requested_lsb * shunt_resistance_ohms);
    ESP_RETURN_ON_FALSE(isfinite(calibration_float) &&
                            calibration_float >= 1.0f &&
                            calibration_float <= 65535.0f,
                        ESP_ERR_INVALID_ARG, "ina226", "calibration value is out of range");
    const uint16_t calibration = (uint16_t)(calibration_float + 0.5f);

    esp_err_t err = write_register(sensor, INA226_REG_CALIBRATION, calibration);
    if (err == ESP_OK) {
        sensor->shunt_resistance_ohms = shunt_resistance_ohms;
        sensor->calibration_register = calibration;
        sensor->current_lsb_amps =
            INA226_CALIBRATION_CONSTANT / ((float)calibration * shunt_resistance_ohms);
    }
    return err;
}

esp_err_t ina226_get_calibration(ina226_handle_t sensor,
                                 float *shunt_resistance_ohms,
                                 float *current_lsb_amps,
                                 uint16_t *calibration_register)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(shunt_resistance_ohms != NULL ||
                            current_lsb_amps != NULL ||
                            calibration_register != NULL,
                        ESP_ERR_INVALID_ARG, "ina226", "at least one output is required");

    if (shunt_resistance_ohms != NULL) {
        *shunt_resistance_ohms = sensor->shunt_resistance_ohms;
    }
    if (current_lsb_amps != NULL) {
        *current_lsb_amps = sensor->current_lsb_amps;
    }
    if (calibration_register != NULL) {
        *calibration_register = sensor->calibration_register;
    }
    return ESP_OK;
}

esp_err_t ina226_set_measurement_config(ina226_handle_t sensor,
                                        const ina226_measurement_config_t *config)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(valid_measurement_config(config),
                        ESP_ERR_INVALID_ARG, "ina226", "measurement configuration is invalid");

    const uint16_t mask = INA226_CONFIG_AVERAGING_MASK |
                          INA226_CONFIG_BUS_CONVERSION_MASK |
                          INA226_CONFIG_SHUNT_CONVERSION_MASK |
                          INA226_CONFIG_MODE_MASK;
    const uint16_t value =
        ((uint16_t)config->averaging << 9) |
        ((uint16_t)config->bus_conversion_time << 6) |
        ((uint16_t)config->shunt_conversion_time << 3) |
        (uint16_t)config->mode;
    const uint16_t new_configuration =
        (sensor->configuration & ~mask) | (value & mask);

    esp_err_t err = write_register(sensor, INA226_REG_CONFIG, new_configuration);
    if (err == ESP_OK) {
        sensor->configuration = new_configuration;
    }
    return err;
}

esp_err_t ina226_get_measurement_config(ina226_handle_t sensor,
                                        ina226_measurement_config_t *config)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, "ina226", "config is NULL");

    uint16_t value;
    esp_err_t err = read_register(sensor, INA226_REG_CONFIG, &value);
    if (err == ESP_OK) {
        sensor->configuration = value & ~INA226_CONFIG_RESET;
        config->averaging = (ina226_averaging_t)((value & INA226_CONFIG_AVERAGING_MASK) >> 9);
        config->bus_conversion_time =
            (ina226_conversion_time_t)((value & INA226_CONFIG_BUS_CONVERSION_MASK) >> 6);
        config->shunt_conversion_time =
            (ina226_conversion_time_t)((value & INA226_CONFIG_SHUNT_CONVERSION_MASK) >> 3);
        config->mode = (ina226_mode_t)(value & INA226_CONFIG_MODE_MASK);
    }
    return err;
}

esp_err_t ina226_set_mode(ina226_handle_t sensor, ina226_mode_t mode)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE((unsigned)mode <= INA226_MODE_SHUNT_BUS_CONTINUOUS,
                        ESP_ERR_INVALID_ARG, "ina226", "mode is invalid");
    const uint16_t configuration =
        (sensor->configuration & ~INA226_CONFIG_MODE_MASK) | (uint16_t)mode;
    esp_err_t err = write_register(sensor, INA226_REG_CONFIG, configuration);
    if (err == ESP_OK) {
        sensor->configuration = configuration;
    }
    return err;
}

esp_err_t ina226_set_alert(ina226_handle_t sensor,
                           ina226_alert_t alert,
                           float limit,
                           bool active_high,
                           bool latched)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(alert == INA226_ALERT_SHUNT_OVER_VOLTAGE ||
                            alert == INA226_ALERT_SHUNT_UNDER_VOLTAGE ||
                            alert == INA226_ALERT_BUS_OVER_VOLTAGE ||
                            alert == INA226_ALERT_BUS_UNDER_VOLTAGE ||
                            alert == INA226_ALERT_POWER_OVER_LIMIT ||
                            alert == INA226_ALERT_CONVERSION_READY,
                        ESP_ERR_INVALID_ARG, "ina226", "alert type is invalid");

    uint16_t limit_register = 0;
    if (alert != INA226_ALERT_CONVERSION_READY) {
        ESP_RETURN_ON_FALSE(isfinite(limit), ESP_ERR_INVALID_ARG, "ina226", "alert limit must be finite");
        float raw_limit;
        if (alert == INA226_ALERT_SHUNT_OVER_VOLTAGE ||
            alert == INA226_ALERT_SHUNT_UNDER_VOLTAGE) {
            raw_limit = limit / INA226_SHUNT_VOLTAGE_LSB_V;
            ESP_RETURN_ON_FALSE(raw_limit >= -32768.0f && raw_limit <= 32767.0f,
                                ESP_ERR_INVALID_ARG, "ina226", "shunt alert limit is out of range");
            limit_register = (uint16_t)(int16_t)lroundf(raw_limit);
        } else if (alert == INA226_ALERT_BUS_OVER_VOLTAGE ||
                   alert == INA226_ALERT_BUS_UNDER_VOLTAGE) {
            raw_limit = limit / INA226_BUS_VOLTAGE_LSB_V;
            ESP_RETURN_ON_FALSE(raw_limit >= 0.0f && raw_limit <= 65535.0f,
                                ESP_ERR_INVALID_ARG, "ina226", "bus alert limit is out of range");
            limit_register = (uint16_t)lroundf(raw_limit);
        } else {
            raw_limit = limit / (25.0f * sensor->current_lsb_amps);
            ESP_RETURN_ON_FALSE(raw_limit >= 0.0f && raw_limit <= 65535.0f,
                                ESP_ERR_INVALID_ARG, "ina226", "power alert limit is out of range");
            limit_register = (uint16_t)lroundf(raw_limit);
        }

        esp_err_t err = write_register(sensor, INA226_REG_ALERT_LIMIT, limit_register);
        if (err != ESP_OK) {
            return err;
        }
        sensor->alert_limit = limit_register;
    }

    return configure_alert_register(sensor, (uint16_t)alert, active_high, latched);
}

esp_err_t ina226_disable_alert(ina226_handle_t sensor)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    return configure_alert_register(sensor, 0, false, false);
}

esp_err_t ina226_read_alert_status(ina226_handle_t sensor,
                                   ina226_alert_status_t *status)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, "ina226", "status is NULL");

    uint16_t value;
    esp_err_t err = read_register(sensor, INA226_REG_MASK_ENABLE, &value);
    if (err == ESP_OK) {
        *status = (ina226_alert_status_t) {
            .enabled_alerts = value & INA226_MASK_ALERT_SELECTION,
            .alert_active_high = (value & INA226_MASK_ALERT_POLARITY) != 0,
            .alert_latched = (value & INA226_MASK_ALERT_LATCH_ENABLE) != 0,
            .alert_function_active = (value & INA226_MASK_ALERT_FUNCTION_ACTIVE) != 0,
            .conversion_ready = (value & INA226_MASK_CONVERSION_READY_FLAG) != 0,
            .math_overflow = (value & INA226_MASK_MATH_OVERFLOW) != 0,
        };
        sensor->mask_enable = value & 0xFC03;
    }
    return err;
}

esp_err_t ina226_reset(ina226_handle_t sensor)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ina226", "sensor is NULL");
    esp_err_t err = write_register(sensor, INA226_REG_CONFIG, INA226_CONFIG_RESET);
    if (err != ESP_OK) {
        return err;
    }

    esp_rom_delay_us(INA226_CONVERSION_READY_DELAY_US);
    err = write_register(sensor, INA226_REG_CALIBRATION, sensor->calibration_register);
    if (err == ESP_OK) {
        err = write_register(sensor, INA226_REG_CONFIG, sensor->configuration);
    }
    if (err == ESP_OK) {
        err = write_register(sensor, INA226_REG_ALERT_LIMIT, sensor->alert_limit);
    }
    if (err == ESP_OK) {
        err = write_register(sensor, INA226_REG_MASK_ENABLE, sensor->mask_enable);
    }
    return err;
}
