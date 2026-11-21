/**
 * @file aquaguard_main.c
 * @brief Master Unified Entry Point for AquaGuard Water Treatment & Safety Controller
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Architecture
 *
 * Spawns all 6 deterministic subsystems across Raspberry Pi 4B Cores (0..3):
 *   - Core 0 (Prio 60): Valve Controller & Safe State Actuator
 *   - Core 1 (Prio 55): Validation Supervisor & Voting Engine
 *   - Core 2 (Prio 45): Safety Watchdog (500ms Deadman Pulse Monitor)
 *   - Core 2 (Prio 35): Sensor Acquisition HAL Streamer
 *   - Core 3 (Prio 10): HTTP Real-Time Telemetry Dashboard (Port 8080)
 *   - Core 3 (Prio  5): Interactive CLI Fault Injection Console
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "aquaguard_ipc.h"
#include "unified_workers.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("\n");
    printf("===================================================================\n");
    printf("       AQUAGUARD SIL-3 WATER TREATMENT & SAFETY CONTROLLER         \n");
    printf("   Target: Raspberry Pi 4B | Operating System: QNX Neutrino RTOS   \n");
    printf("===================================================================\n\n");

    pthread_t tid_valve;
    pthread_t tid_supervisor;
    pthread_t tid_watchdog;
    pthread_t tid_sensors;

    /* 1. Spawn Core 0 Highest Priority Valve Controller (Priority 60) */
    printf("[1/4] Launching Core 0 Valve Controller (SCHED_FIFO Prio 60)...\n");
    if (pthread_create(&tid_valve, NULL, valve_actuator_worker, NULL) != 0) {
        perror("Failed to create valve controller thread");
        return EXIT_FAILURE;
    }
    usleep(100000); /* 100ms for channel registration */

    /* 2. Spawn Core 1 Validation Supervisor (Priority 55) */
    printf("[2/4] Launching Core 1 Validation Supervisor (SCHED_FIFO Prio 55)...\n");
    if (pthread_create(&tid_supervisor, NULL, supervisor_worker, NULL) != 0) {
        perror("Failed to create supervisor thread");
        return EXIT_FAILURE;
    }
    usleep(100000);

    /* 3. Spawn Core 2 Safety Watchdog (Priority 45) */
    printf("[3/4] Launching Core 2 Safety Watchdog (SCHED_FIFO Prio 45)...\n");
    if (pthread_create(&tid_watchdog, NULL, watchdog_worker, NULL) != 0) {
        perror("Failed to create watchdog thread");
        return EXIT_FAILURE;
    }
    usleep(100000);

    /* 4. Spawn Core 2 Sensor Acquisition HAL (Priority 35) */
    printf("[4/4] Launching Core 2 Sensor Acquisition HAL (SCHED_FIFO Prio 35)...\n");
    if (pthread_create(&tid_sensors, NULL, sensor_adapter_worker, NULL) != 0) {
        perror("Failed to create sensor acquisition thread");
        return EXIT_FAILURE;
    }
    usleep(100000);

    printf("\n>>> All AquaGuard SIL-3 subsystems online! Launching Live Console HUD...\n\n");

    /* 5. Run Live Telemetry HUD & Interactive Disturbance Console on Core 3 */
    fault_injector_worker(NULL);

    printf("[MAIN] AquaGuard process terminating cleanly.\n");
    exit(0);
    return EXIT_SUCCESS;
}
