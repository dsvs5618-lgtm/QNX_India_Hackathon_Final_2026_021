/**
 * @file aquaguard_config.h
 * @brief AquaGuard Master Centralized Universal Configuration
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Design
 *
 * All physical parameters, safety limits, sensor calibrations, timers, and
 * pinouts are centralized here. The core voting engine, state machine, probation
 * manager, and IPC layers are decoupled from hardware and remain invariant.
 */

#ifndef AQUAGUARD_CONFIG_H
#define AQUAGUARD_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 1. HARDWARE RUNTIME TARGET CONFIGURATION
 * ========================================================================= */
#define USE_PHYSICAL_HARDWARE             1     /* 1 = Real RPi 4B GPIO & I2C; 0 = Simulation */
#define HAVE_PHYSICAL_PUMPS               1     /* 1 = TB6612FNG Submersible Pumps Enabled */
#define CONFIG_BUZZER_ACTIVE_LOW          0     /* 0 = Standard Active-High (1=Sound, 0=Silent); 1 = Active-Low */

/* =========================================================================
 * 2. APPLICATION PURITY STANDARDS & VOTING ENGINE PROFILE
 * ========================================================================= */
#define CONFIG_PURITY_CLEAN_LIMIT_PPM     (300.0f) /* Safe drinking limit (WHO: <= 300 ppm) */
#define CONFIG_PURITY_TOLERANCE_PPM       (60.0f)  /* Max permissible delta between redundant sensors (Tolerates breadboard analog jitter) */
#define CONFIG_PROBATION_CYCLES           (6)      /* Consecutive stable cycles to re-admit sensor */
#define CONFIG_MAX_SINGLE_SENSOR_DRIFT_PPM (60.0f) /* Historical drift threshold in 1oo1 degraded mode */

/* Aliases for backwards compatibility with legacy macros */
#define TDS_CLEAN_WATER_THRESHOLD_PPM     CONFIG_PURITY_CLEAN_LIMIT_PPM
#define TDS_SENSOR_TOLERANCE_PPM          CONFIG_PURITY_TOLERANCE_PPM
#define PROBATION_REQUIRED_CYCLES         CONFIG_PROBATION_CYCLES
#define TDS_SINGLE_SENSOR_DRIFT_MAX_PPM   CONFIG_MAX_SINGLE_SENSOR_DRIFT_PPM

/* =========================================================================
 * 3. HYDRAULIC LIMITS & PIPE BURST PROTECTION
 * ========================================================================= */
#define CONFIG_PRESSURE_BURST_TRIP_KPA    (180.0f) /* Overpressure burst trip threshold */
#define CONFIG_FLOW_NOMINAL_LPM           (2.45f)  /* Expected baseline flow rate */
#define CONFIG_FLOW_TRIGGER_MIN_LPM       (0.05f)  /* Minimum flow (LPM) to engage pumps (Sensitive to quick funnel pours) */
#define CONFIG_FLOW_HOLD_AFTER_STOP_MS    (1000)   /* Keep pump active 1.0s after inflow stops to clear chamber (Prevents water dumping) */
#define PRESSURE_MAX_SAFE_KPA             CONFIG_PRESSURE_BURST_TRIP_KPA

/* =========================================================================
 * 4. REAL-TIME SAFETY TIMERS & CONSTRAINTS
 * ========================================================================= */
#define CONFIG_INIT_PUMP_MAX_BURST_MS     (1000)   /* Maximum 1.0s runtime during boot initialization */
#define CONFIG_FAULT_PUMP_MAX_BURST_MS    (3000)   /* Exactly 3.0s runtime for presentation fault-injected pumps */
#define CONFIG_WATCHDOG_TIMEOUT_MS        (500)    /* Supervisor deadman heartbeat timeout */
#define CONFIG_SENSOR_SAMPLE_PERIOD_MS    (50)     /* 20 Hz acquisition frequency */
#define CONFIG_HARD_CONTROL_DEADLINE_US   (50000)  /* 50 ms hard real-time safety envelope */
#define WATCHDOG_TIMEOUT_MS               CONFIG_WATCHDOG_TIMEOUT_MS

/* =========================================================================
 * 5. PHYSICAL PIN MAPPING (Raspberry Pi 4B 40-Pin Header)
 * ========================================================================= */
/* TB6612FNG Dual H-Bridge Motor Driver (5V DC Submersible Pumps) */
#define PIN_PUMP_CLEAN                    17       /* Header Pin 11: TB6612FNG PWMA (Clean Pump) */
#define PIN_PUMP_FILTER                   27       /* Header Pin 13: TB6612FNG PWMB (Filter Pump) */
#define PIN_VALVE_CLEAN                   PIN_PUMP_CLEAN
#define PIN_VALVE_FILTRATION              PIN_PUMP_FILTER

/* Status Indicators */
#define PIN_LED_GREEN                     21       /* Header Pin 40: Normal Clean Water */
#define PIN_LED_ORANGE                    20       /* Header Pin 38: Degraded / Outlier Isolated */
#define PIN_LED_RED                       16       /* Header Pin 36: Alarm / Safe State / Halt */
#define PIN_BUZZER                        26       /* Header Pin 37: Acoustic Alarm Buzzer */

/* I2C Contaminant Bus (ADS1115 ADC for 3x TDS Probes) */
#define ADS1115_I2C_ADDR                  0x48     /* I2C Address (ADDR grounded) */
#define I2C_DEV_PATH                      "/dev/i2c1"
#define PIN_ADS1115_SDA                   2        /* Header Pin 3: I2C1 SDA */
#define PIN_ADS1115_SCL                   3        /* Header Pin 5: I2C1 SCL */
#define ADS1115_CH_TDS0                   0        /* Channel A0: TDS Meter 1 */
#define ADS1115_CH_TDS1                   1        /* Channel A1: TDS Meter 2 */
#define ADS1115_CH_TDS2                   2        /* Channel A2: TDS Meter 3 */

/* Hydraulic Sensors (HX710B Pressure & YFS401 Flow Meter) */
#define PIN_HX710B_DOUT                   5        /* Header Pin 29: Pressure DOUT */
#define PIN_HX710B_SCK                    6        /* Header Pin 31: Pressure SCK */
#define PIN_YFS401_FLOW_PULSE             22       /* Header Pin 15: Flow Pulse */

#ifdef __cplusplus
}
#endif

#endif /* AQUAGUARD_CONFIG_H */
