/**
 * @file rpi_hardware.h
 * @brief Direct Hardware Abstraction Layer for Raspberry Pi 4B under QNX Neutrino RTOS
 * @standard MISRA-C:2012 / IEC 61508 SIL-3
 */

#ifndef RPI_HARDWARE_H
#define RPI_HARDWARE_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Raspberry Pi 4B BCM2711 GPIO memory mapping and I2C bus
 * @return true if hardware initialized successfully, false otherwise
 */
bool rpi_hw_init(void);

/**
 * @brief Configure GPIO pin mode
 */
void rpi_gpio_set_output(int pin);
void rpi_gpio_set_input(int pin);

/**
 * @brief Set GPIO pin state (1 = HIGH, 0 = LOW)
 */
void rpi_gpio_write(int pin, int val);

/**
 * @brief Read digital GPIO level
 */
int rpi_gpio_read(int pin);

/**
 * @brief Read TDS probe analog channel from ADS1115 via I2C and convert to PPM
 * @param channel 0 (A0), 1 (A1), or 2 (A2)
 * @return TDS value in ppm
 */
float rpi_ads1115_read_tds_ppm(int channel);

/**
 * @brief Read HX710B pressure sensor via 24-bit bit-bang and convert to kPa
 * @return Water pressure in kPa
 */
float rpi_hx710b_read_pressure_kpa(void);

/**
 * @brief Read YFS401 water flow meter rate
 * @return Flow rate in LPM
 */
float rpi_yfs401_read_flow_lpm(void);

#ifdef __cplusplus
}
#endif

#endif /* RPI_HARDWARE_H */
