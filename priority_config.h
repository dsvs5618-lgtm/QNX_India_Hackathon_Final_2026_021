/**
 * @file priority_config.h
 * @brief AquaGuard QNX SDP 8.0 Real-Time Priority Hierarchy & Core Pinning Allocations
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Design
 *
 * In QNX RTOS Neutrino, threads with identical SCHED_FIFO priority are non-preemptive.
 * To enforce strict determinism and eliminate scheduling race conditions, every
 * safety-critical process is assigned a strictly UNIQUE priority.
 */

#ifndef PRIORITY_CONFIG_H
#define PRIORITY_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * QNX PROCESS SCHED_FIFO PRIORITIES (Scale: 1 to 63)
 * ========================================================================= */
#define PRIO_WATCHDOG              ((int32_t)60)  /**< Safety Watchdog: Heartbeat supervisor & failsafe */
#define PRIO_VALVE_CONTROLLER      ((int32_t)55)  /**< Actuation Server: Emergency isolation & relay */
#define PRIO_SUPERVISOR            ((int32_t)50)  /**< Validation & Control Supervisor: Sensor fusion */
#define PRIO_PF_MONITOR            ((int32_t)28)  /**< Pressure & Flow Monitor: Hydraulic integrity */
#define PRIO_SENSOR_ADAPTER_3      ((int32_t)27)  /**< Contamination Sensor Adapter 3 */
#define PRIO_SENSOR_ADAPTER_2      ((int32_t)26)  /**< Contamination Sensor Adapter 2 */
#define PRIO_SENSOR_ADAPTER_1      ((int32_t)25)  /**< Contamination Sensor Adapter 1 */
#define PRIO_TELEMETRY_LOGGER      ((int32_t)12)  /**< Telemetry Logger: Non-blocking disk persistence */
#define PRIO_MONITORING_CLI        ((int32_t)10)  /**< Monitoring CLI: Low-priority visualization */
#define PRIO_CPU_STRESS_HOG        ((int32_t)1)   /**< CPU Stress Hog: Best-effort preemption test */

/* =========================================================================
 * RASPBERRY PI 4 / 5 MULTI-CORE RUNMASK AFFINITIES (4 Cores: 0 to 3)
 * ========================================================================= */
#define CORE_MASK_CORE0            ((unsigned)0x01) /**< Core 0: CLI, Logging, Management & Stress */
#define CORE_MASK_CORE1            ((unsigned)0x02) /**< Core 1: Sensor Acquisition & Pressure/Flow */
#define CORE_MASK_CORE2            ((unsigned)0x04) /**< Core 2: High-Priority Validation & State Engine */
#define CORE_MASK_CORE3            ((unsigned)0x08) /**< Core 3: Valve Actuation & Safety Watchdog */
#define CORE_MASK_ALL              ((unsigned)0x0F) /**< All 4 cores */

/* Affinity Assignments */
#define CORE_AFFINITY_CLI          CORE_MASK_CORE0
#define CORE_AFFINITY_LOGGER       CORE_MASK_CORE0
#define CORE_AFFINITY_SENSORS      CORE_MASK_CORE1
#define CORE_AFFINITY_SUPERVISOR   CORE_MASK_CORE2
#define CORE_AFFINITY_ACTUATION    CORE_MASK_CORE3
#define CORE_AFFINITY_WATCHDOG     CORE_MASK_CORE3

/* =========================================================================
 * NAMED CHANNEL PATHS (QNX Global / Local Name Service)
 * ========================================================================= */
#define AQUAGUARD_NAME_SUPERVISOR  "aquaguard_supervisor"
#define AQUAGUARD_NAME_VALVE       "aquaguard_valve"
#define AQUAGUARD_NAME_WATCHDOG    "aquaguard_watchdog"
#define AQUAGUARD_NAME_CLI         "aquaguard_cli"

#ifdef __cplusplus
}
#endif

#endif /* PRIORITY_CONFIG_H */
