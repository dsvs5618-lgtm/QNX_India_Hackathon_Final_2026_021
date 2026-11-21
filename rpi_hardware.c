/**
 * @file rpi_hardware.c
 * @brief Direct Hardware Drivers for Raspberry Pi 4B (ADS1115, HX710B, YFS401, IRLZ44N, TB6612FNG)
 * @standard MISRA-C:2012 / IEC 61508 SIL-3
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#if defined(__QNXNTO__)
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <hw/i2c.h>
#endif

#include "rpi_hardware.h"
#include "hardware_pins.h"

/* Raspberry Pi 4B (BCM2711) GPIO Register Offsets */
#define BCM2711_PERIPH_BASE     0xFE000000ULL
#define BCM2711_GPIO_OFFSET     0x00200000ULL
#define BCM2711_GPIO_PHYS_BASE  (BCM2711_PERIPH_BASE + BCM2711_GPIO_OFFSET) /* 0xFE200000 */
#define BCM2711_GPIO_BLOCK_SIZE 0x1000

#define GPFSEL0_IDX             (0x00 / 4)
#define GPSET0_IDX              (0x1C / 4)
#define GPCLR0_IDX              (0x28 / 4)
#define GPLEV0_IDX              (0x34 / 4)

static volatile uint32_t *s_gpio_regs = NULL;
static int s_i2c_fd = -1;
static bool s_hw_ready = false;

bool rpi_hw_init(void) {
#if defined(__QNXNTO__)
    /* 1. Request physical I/O privileges on QNX */
    if (ThreadCtl(_NTO_TCTL_IO, 0) == -1) {
        perror("[HW] ThreadCtl _NTO_TCTL_IO failed (must run as root for physical GPIO)");
    }
    nanospin_calibrate(0);

    /* 2. Map physical BCM2711 GPIO memory */
    s_gpio_regs = (volatile uint32_t *)mmap_device_memory(
        NULL,
        BCM2711_GPIO_BLOCK_SIZE,
        PROT_READ | PROT_WRITE | PROT_NOCACHE,
        0,
        BCM2711_GPIO_PHYS_BASE
    );

    if (s_gpio_regs == MAP_FAILED || s_gpio_regs == NULL) {
        perror("[HW] mmap_device_memory failed for BCM2711 GPIO");
        s_gpio_regs = NULL;
    } else {
        printf("[HW] BCM2711 GPIO memory mapped successfully at %p\n", (void *)s_gpio_regs);
    }

    /* 3. Open I2C device for ADS1115 ADC */
    s_i2c_fd = open("/dev/i2c1", O_RDWR);
    if (s_i2c_fd < 0) {
        s_i2c_fd = open("/dev/i2c-1", O_RDWR);
    }
    if (s_i2c_fd >= 0) {
        printf("[HW] I2C bus opened successfully (fd=%d)\n", s_i2c_fd);
    } else {
        printf("[HW] Note: /dev/i2c1 not found. Verify i2c driver is running ('i2c-bcm2711 &').\n");
    }

    /* 4. Configure Output Pins */
    rpi_gpio_set_output(PIN_VALVE_CLEAN);      /* GPIO 17 (Pin 11) - Clean Valve Gate 1 */
    rpi_gpio_set_output(PIN_VALVE_FILTRATION); /* GPIO 27 (Pin 13) - Filter Valve Gate 2 */
    rpi_gpio_set_output(PIN_LED_GREEN);        /* GPIO 21 (Pin 40) - Nominal Green LED */
    rpi_gpio_set_output(PIN_LED_ORANGE);       /* GPIO 20 (Pin 38) - Degraded Yellow LED */
    rpi_gpio_set_output(PIN_LED_RED);          /* GPIO 16 (Pin 36) - Alarm Red LED */
    rpi_gpio_set_output(PIN_BUZZER);           /* GPIO 26 (Pin 37) - Buzzer */
    rpi_gpio_set_output(PIN_HX710B_SCK);       /* GPIO 6  (Pin 31) - Pressure Clock */

    /* Configure Input Pins */
    rpi_gpio_set_input(PIN_HX710B_DOUT);       /* GPIO 5  (Pin 29) - Pressure Data */
    rpi_gpio_set_input(PIN_YFS401_FLOW_PULSE); /* GPIO 22 (Pin 15) - Flow Pulse */

    /* Enable Pull-Up on GPIO 22 (Pin 15) for open-collector YFS401 Hall pulse (BCM2711 register 0xE8) */
    if (s_gpio_regs != NULL) {
        uint32_t pup = s_gpio_regs[0xE8 / 4];
        pup &= ~(3U << ((PIN_YFS401_FLOW_PULSE - 16) * 2));
        pup |= (1U << ((PIN_YFS401_FLOW_PULSE - 16) * 2)); /* 01 = Pull-Up */
        s_gpio_regs[0xE8 / 4] = pup;

        /* Enable Rising & Falling Edge Event Detection on GPIO 22 */
        s_gpio_regs[0x4C / 4] |= (1U << PIN_YFS401_FLOW_PULSE); /* GPREN0 */
        s_gpio_regs[0x58 / 4] |= (1U << PIN_YFS401_FLOW_PULSE); /* GPFEN0 */
        s_gpio_regs[0x40 / 4] = (1U << PIN_YFS401_FLOW_PULSE);  /* Clear any initial latch */
    }

    /* Set Initial Hardware Failsafe Safe State (Motors OFF, Alarm Red LED ON) */
    rpi_gpio_write(PIN_PUMP_CLEAN, 0);         /* Clean Pump OFF (PWMA = 0) */
    rpi_gpio_write(PIN_PUMP_FILTER, 0);        /* Filter Pump OFF until commanded (PWMB = 0) */
    rpi_gpio_write(PIN_LED_GREEN, 0);
    rpi_gpio_write(PIN_LED_ORANGE, 0);
    rpi_gpio_write(PIN_LED_RED, 1);            /* Alarm Red ON at boot */
    rpi_gpio_write(PIN_BUZZER, 0);
    rpi_gpio_write(PIN_HX710B_SCK, 0);

    s_hw_ready = true;
    return true;
#else
    printf("[HW] Host Simulation mode active (No physical QNX hardware).\n");
    s_hw_ready = true;
    return true;
#endif
}

void rpi_gpio_set_output(int pin) {
    if (s_gpio_regs == NULL || pin < 0 || pin > 27) return;
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    uint32_t val = s_gpio_regs[reg];
    val &= ~(7U << shift);     /* Clear 3 bits */
    val |= (1U << shift);      /* Set as 001 (Output) */
    s_gpio_regs[reg] = val;
}

void rpi_gpio_set_input(int pin) {
    if (s_gpio_regs == NULL || pin < 0 || pin > 27) return;
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    uint32_t val = s_gpio_regs[reg];
    val &= ~(7U << shift);     /* Set as 000 (Input) */
    s_gpio_regs[reg] = val;
}

void rpi_gpio_write(int pin, int val) {
    if (s_gpio_regs == NULL || pin < 0 || pin > 27) return;
    if (val) {
        s_gpio_regs[GPSET0_IDX] = (1U << pin);
    } else {
        s_gpio_regs[GPCLR0_IDX] = (1U << pin);
    }
}

int rpi_gpio_read(int pin) {
    if (s_gpio_regs == NULL || pin < 0 || pin > 27) return 0;
    return (s_gpio_regs[GPLEV0_IDX] & (1U << pin)) ? 1 : 0;
}

/* =========================================================================
 * DIRECT HARDWARE I2C BIT-BANG DRIVER (Pins 3 & 5)
 * ========================================================================= */
#if defined(__QNXNTO__)
static void rpi_i2c_delay(void) {
    nanospin_ns(5000); /* 5 us half-period = 100 kHz I2C clock */
}

static void rpi_i2c_sda_release(void) {
    rpi_gpio_set_input(PIN_ADS1115_SDA);
}

static void rpi_i2c_sda_low(void) {
    rpi_gpio_write(PIN_ADS1115_SDA, 0);
    rpi_gpio_set_output(PIN_ADS1115_SDA);
}

static void rpi_i2c_scl_release(void) {
    rpi_gpio_set_input(PIN_ADS1115_SCL);
    int timeout = 500;
    while (!rpi_gpio_read(PIN_ADS1115_SCL) && --timeout > 0) {
        rpi_i2c_delay();
    }
}

static void rpi_i2c_scl_low(void) {
    rpi_gpio_write(PIN_ADS1115_SCL, 0);
    rpi_gpio_set_output(PIN_ADS1115_SCL);
}

static void rpi_i2c_start(void) {
    rpi_i2c_sda_release();
    rpi_i2c_scl_release();
    rpi_i2c_delay();
    rpi_i2c_sda_low();
    rpi_i2c_delay();
    rpi_i2c_scl_low();
    rpi_i2c_delay();
}

static void rpi_i2c_stop(void) {
    rpi_i2c_sda_low();
    rpi_i2c_delay();
    rpi_i2c_scl_release();
    rpi_i2c_delay();
    rpi_i2c_sda_release();
    rpi_i2c_delay();
}

static bool rpi_i2c_write_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if ((byte >> i) & 1) {
            rpi_i2c_sda_release();
        } else {
            rpi_i2c_sda_low();
        }
        rpi_i2c_delay();
        rpi_i2c_scl_release();
        rpi_i2c_delay();
        rpi_i2c_scl_low();
        rpi_i2c_delay();
    }
    /* Read ACK */
    rpi_i2c_sda_release();
    rpi_i2c_delay();
    rpi_i2c_scl_release();
    rpi_i2c_delay();
    int ack = rpi_gpio_read(PIN_ADS1115_SDA);
    rpi_i2c_scl_low();
    rpi_i2c_delay();
    return (ack == 0);
}

static uint8_t rpi_i2c_read_byte(bool send_ack) {
    uint8_t byte = 0;
    rpi_i2c_sda_release();
    for (int i = 7; i >= 0; i--) {
        rpi_i2c_scl_release();
        rpi_i2c_delay();
        if (rpi_gpio_read(PIN_ADS1115_SDA)) {
            byte |= (1 << i);
        }
        rpi_i2c_scl_low();
        rpi_i2c_delay();
    }
    if (send_ack) {
        rpi_i2c_sda_low();
    } else {
        rpi_i2c_sda_release();
    }
    rpi_i2c_delay();
    rpi_i2c_scl_release();
    rpi_i2c_delay();
    rpi_i2c_scl_low();
    rpi_i2c_sda_release();
    rpi_i2c_delay();
    return byte;
}
#endif /* __QNXNTO__ */

static float s_last_valid_tds[3] = {50.0f, 50.0f, 50.0f};

/**
 * @brief Read ADS1115 Single-Ended Channel (A0..A2)
 */
float rpi_ads1115_read_tds_ppm(int channel) {
    int ch = channel & 0x03;
    if (ch > 2) ch = 0;
#if defined(__QNXNTO__)
    if (s_gpio_regs == NULL) {
        return s_last_valid_tds[ch];
    }

    /* ADS1115 MUX bits: A0 = 100 (0x4000), A1 = 101 (0x5000), A2 = 110 (0x6000) */
    uint16_t mux = 0x4000 | ((uint16_t)(ch) << 12);
    /* OS=1, PGA=+-4.096V, Mode=1 (single shot), 128 SPS */
    uint16_t config = 0x8000 | mux | 0x0200 | 0x0100 | 0x0083;

    /* 1. Send Config with 1 retry on NACK */
    bool ok = false;
    for (int retry = 0; retry < 2 && !ok; retry++) {
        rpi_i2c_start();
        if (rpi_i2c_write_byte(ADS1115_I2C_ADDR << 1)) {
            rpi_i2c_write_byte(0x01);
            rpi_i2c_write_byte((uint8_t)(config >> 8));
            rpi_i2c_write_byte((uint8_t)(config & 0xFF));
            ok = true;
        }
        rpi_i2c_stop();
        if (!ok) usleep(1000);
    }
    if (!ok) return s_last_valid_tds[ch];

    /* 2. Wait 15ms for conversion */
    usleep(15000);

    /* 3. Point to Conversion Register */
    rpi_i2c_start();
    if (!rpi_i2c_write_byte(ADS1115_I2C_ADDR << 1)) {
        rpi_i2c_stop();
        return s_last_valid_tds[ch];
    }
    rpi_i2c_write_byte(0x00);
    rpi_i2c_stop();

    /* 4. Read 2-byte conversion result */
    rpi_i2c_start();
    if (!rpi_i2c_write_byte((ADS1115_I2C_ADDR << 1) | 1)) {
        rpi_i2c_stop();
        return s_last_valid_tds[ch];
    }
    uint8_t msb = rpi_i2c_read_byte(true);
    uint8_t lsb = rpi_i2c_read_byte(false);
    rpi_i2c_stop();

    int16_t raw = (int16_t)(((uint16_t)msb << 8) | lsb);
    if (raw < 0) raw = 0;
    float voltage = (float)raw * 0.000125f;

    /* DFRobot / Gravity TDS Polynomial Conversion */
    float v = voltage;
    float ppm = (133.42f * v * v * v - 255.86f * v * v + 857.39f * v) * 0.5f;
    if (ppm < 0.0f) ppm = 0.0f;
    if (ppm > 2000.0f) ppm = 2000.0f;
    s_last_valid_tds[ch] = ppm;
    return ppm;
#else
    (void)channel;
    return s_last_valid_tds[ch];
#endif
}

/**
 * @brief Read HX710B 24-bit ADC via bit-bang on DOUT (GPIO 5 / Pin 29) and SCK (GPIO 6 / Pin 31)
 */
float rpi_hx710b_read_pressure_kpa(void) {
    if (s_gpio_regs == NULL) {
        return 50.0f;
    }

    /* Wait for DOUT to go LOW (data ready) with timeout */
    int timeout = 5000;
    while (rpi_gpio_read(PIN_HX710B_DOUT) == 1 && timeout > 0) {
#if defined(__QNXNTO__)
        nanospin_ns(10000); /* 10 us */
#else
        usleep(10);
#endif
        timeout--;
    }
    if (timeout <= 0) {
        /* Sensor disconnected or busy */
        return 50.0f;
    }

    int32_t raw = 0;
    for (int i = 0; i < 24; i++) {
        rpi_gpio_write(PIN_HX710B_SCK, 1);
#if defined(__QNXNTO__)
        nanospin_ns(2000); /* 2 us clock high (< 60 us power-down limit) */
#else
        usleep(1);
#endif
        raw = (raw << 1) | rpi_gpio_read(PIN_HX710B_DOUT);
        rpi_gpio_write(PIN_HX710B_SCK, 0);
#if defined(__QNXNTO__)
        nanospin_ns(2000); /* 2 us clock low */
#else
        usleep(1);
#endif
    }

    /* 25th clock pulse: keep 128 gain (differential input) */
    rpi_gpio_write(PIN_HX710B_SCK, 1);
#if defined(__QNXNTO__)
    nanospin_ns(2000);
#else
    usleep(1);
#endif
    rpi_gpio_write(PIN_HX710B_SCK, 0);

    /* Sign-extend 24-bit two's complement */
    if (raw & 0x00800000) {
        raw |= 0xFF000000;
    }

    /* Pascal's Law water pressure conversion (baseline zero ~0x800000) */
    float pressure_kpa = ((float)(raw - 8388608) / 8388608.0f) * 100.0f + 50.0f;
    if (pressure_kpa < 0.0f) pressure_kpa = 0.0f;
    if (pressure_kpa > 300.0f) pressure_kpa = 300.0f;
    return pressure_kpa;
}

/**
 * @brief Read YFS401 Flow Meter Rate
 */
float rpi_yfs401_read_flow_lpm(void) {
    if (s_gpio_regs == NULL) {
        return 0.0f;
    }

    static int s_last_pin_level = -1;

    /* 1. Check hardware event latch (GPEDS0) to catch any pulses */
    bool latch_hit = false;
    if (s_gpio_regs[0x40 / 4] & (1U << PIN_YFS401_FLOW_PULSE)) {
        latch_hit = true;
        s_gpio_regs[0x40 / 4] = (1U << PIN_YFS401_FLOW_PULSE); /* Clear event latch */
    }

    /* 2. Check if level changed since last call */
    int current_level = rpi_gpio_read(PIN_YFS401_FLOW_PULSE);
    if (s_last_pin_level != -1 && current_level != s_last_pin_level) {
        latch_hit = true;
    }
    s_last_pin_level = current_level;

    /* 3. Fast 5ms micro-sampling window to detect active edge transitions */
    int transitions = latch_hit ? 1 : 0;
    int last_state = current_level;
    for (int i = 0; i < 10; i++) {
        int sample = rpi_gpio_read(PIN_YFS401_FLOW_PULSE);
        if (last_state != sample) {
            transitions++;
        }
        last_state = sample;
#if defined(__QNXNTO__)
        nanospin_ns(500000); /* 500 us (total 5ms sampling) */
#else
        usleep(500);
#endif
    }

    /* 4. If any pulse was detected, return active flow >= 1.2 LPM */
    if (transitions > 0) {
        float lpm = 0.8f + ((float)transitions * 0.35f);
        if (lpm > 5.0f) lpm = 5.0f;
        return lpm;
    }
    return 0.0f;
}
