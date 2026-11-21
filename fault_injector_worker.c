/**
 * @file fault_injector_worker.c
 * @brief AquaGuard Core 3 Interactive Fault Injection CLI Worker Thread
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Verification Tool
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#if defined(__QNXNTO__)
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/dispatch.h>
#include <sys/select.h>
#include <sys/time.h>
#endif

#include "aquaguard_ipc.h"
#include "unified_workers.h"

static volatile bool s_stress_running = false;
static pthread_t s_stress_tid;

static void *cpu_stress_worker(void *arg) {
    (void)arg;
    printf("[STRESS HOG] Started 100%% CPU load on Core 3 with lowest priority (%d)...\n",
           PRIORITY_CPU_STRESS_HOG);

    struct sched_param param;
    param.sched_priority = PRIORITY_CPU_STRESS_HOG;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if defined(__QNXNTO__)
    unsigned runmask = CORE_MASK_3;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);
#endif

    volatile uint64_t dummy = 0;
    while (s_stress_running) {
        dummy++;
    }
    printf("[STRESS HOG] Stopped.\n");
    return NULL;
}

static void print_telemetry_hud(int sup_coid) {
    TelemetrySnapshot_t snap;
    memset(&snap, 0, sizeof(snap));
#if defined(__QNXNTO__)
    if (sup_coid != -1) {
        uint32_t req = MSG_TYPE_TELEMETRY_REQ;
        MsgSend(sup_coid, &req, sizeof(req), &snap, sizeof(snap));
    }
#else
    (void)sup_coid;
    snap.mode = MODE_NORMAL;
    snap.system_state = STATE_NORMAL;
    snap.tds_ppm[0] = 52.1f; snap.tds_ppm[1] = 50.8f; snap.tds_ppm[2] = 51.4f;
    snap.sensor_status[0] = 0; snap.sensor_status[1] = 0; snap.sensor_status[2] = 0;
    snap.sensor_health_pct[0] = 98.0f; snap.sensor_health_pct[1] = 97.0f; snap.sensor_health_pct[2] = 99.0f;
    snap.vote_consensus_ppm = 51;
    snap.pressure_kpa = 51.2f; snap.flow_rate_lpm = 0.0f;
    snap.clean_pump = 0; snap.filter_pump = 0;
    snap.avg_latency_us = 18; snap.max_wcet_us = 38; snap.jitter_us = 4;
#endif

    const char *st_str = "STATE_NORMAL";
    if (snap.system_state == STATE_DEGRADED_1) st_str = "STATE_DEGRADED_1 (2oo3 Active)";
    else if (snap.system_state == STATE_DEGRADED_2) st_str = "STATE_DEGRADED_2 (1oo1 Fallback)";
    else if (snap.system_state == STATE_SAFE_MODE) st_str = "STATE_SAFE_MODE (Diverted)";
    else if (snap.system_state == STATE_EMERGENCY_HALT) st_str = "STATE_EMERGENCY_HALT (Clamped)";

    const char *st_names[4] = {"ACTIVE", "PROBATION", "ISOLATED", "DEAD"};

    printf("\n");
    printf("========================================================================================\n");
    printf("       AQUAGUARD SIL-3 WATER TREATMENT & SAFETY CONTROLLER | QNX NEUTRINO RTOS 8.0      \n");
    printf("========================================================================================\n");
    printf(" [SYSTEM]  State: %-32s | Mode: %s\n", st_str, snap.mode == 0 ? "NORMAL CLEAN WATER" : "DIRTY WATER TEST");
    printf(" [STATUS]  Flow Inflow Gate: %-25s | Consensus Purity: %u ppm (Limit: <= 300 ppm)\n",
           snap.flow_gate_active ? "ACTIVE (Pumping Permitted)" : "STANDBY (Awaiting Inflow - Pumps Idle)",
           snap.vote_consensus_ppm);
    printf("----------------------------------------------------------------------------------------\n");
    printf(" [2oo3 TRIPLE MODULAR REDUNDANCY TDS PROBES (ADS1115 I2C Channels)]\n");
    for (int i = 0; i < 3; i++) {
        printf("   Probe %d (Pin A%d): %5.1f ppm  |  Status: %-9s  |  Health: %3.0f%%  |  Probation: %d/6\n",
               i, i, snap.tds_ppm[i], st_names[snap.sensor_status[i] % 4],
               snap.sensor_health_pct[i], snap.probation_counter[i]);
    }
    if (snap.pressure_kpa > 180.0f) {
        printf("----------------------------------------------------------------------------------------\n");
        printf(" [!] HYDRAULIC PRESSURE SPIKE DETECTED [!]\n");
        printf("   Line Pressure: %5.1f kPa  (Safety Trip Limit: 180.0 kPa) -> SAFETY CLAMP ENGAGED\n",
               snap.pressure_kpa);
        printf("   Action: OVERPRESSURE TRIP ACTIVE (Auto-reverting in 3.0s)\n");
    }
    printf("----------------------------------------------------------------------------------------\n");
    printf(" [ACTUATOR OUTPUTS (TB6612FNG Motor Driver)]\n");
    printf("   Clean Pump  (Pin 11 / PWMA): [%s] %s  |  Clean Valve:      %s\n",
           snap.clean_pump ? " RUNNING " : "   OFF   ", snap.clean_pump ? "3.3V" : "0.0V",
           snap.clean_valve ? "OPEN " : "CLOSED");
    printf("   Filter Pump (Pin 13 / PWMB): [%s] %s  |  Filtration Valve: %s\n",
           snap.filter_pump ? " RUNNING " : "   OFF   ", snap.filter_pump ? "3.3V" : "0.0V",
           snap.filtration_valve ? "OPEN " : "CLOSED");
    if (snap.fault_pump_remaining_ms > 0) {
        printf("   >>> 3.0s DEMO TIMER: %.1fs remaining until auto-revert <<<\n",
               (float)snap.fault_pump_remaining_ms / 1000.0f);
    }
    printf("----------------------------------------------------------------------------------------\n");
    printf(" [HARD REAL-TIME DETERMINISM METRICS (Core 1 SCHED_FIFO)]\n");
    printf("   Avg Latency: %u us  |  Max WCET: %u us  |  Jitter: ±%u us  |  Safety Envelope: 50,000 us\n",
           snap.avg_latency_us, snap.max_wcet_us, snap.jitter_us);
    printf("========================================================================================\n");
    printf(" DISTURBANCE MENU: [b] Bias Probe 1 (+150ppm) | [s] Freeze Probe 2 | [k] Kill Probe 0\n");
    printf("                  [p] Pressure Spike (>180kPa) | [m] Toggle Dirty   | [f] Trigger Flow (3s)\n");
    printf("                  [r] Restore/Heal             | [e] Emergency Halt | [q] Quit\n");
    printf("========================================================================================\n");
    printf("Enter command: ");
    fflush(stdout);
}

void *fault_injector_worker(void *arg) {
    (void)arg;

    struct sched_param param;
    param.sched_priority = PRIORITY_FAULT_INJECTOR;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if defined(__QNXNTO__)
    unsigned runmask = CORE_MASK_3;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);

    int sup_coid = -1;
    while (sup_coid == -1) {
        sup_coid = name_open(CHANNEL_NAME_SUPERVISOR, 0);
        if (sup_coid == -1) {
            usleep(200000);
        }
    }
#else
    int sup_coid = -1;
#endif

    /* Render initial Live Telemetry HUD */
    usleep(200000);
    print_telemetry_hud(sup_coid);

    char line[64];
    while (1) {
#if defined(__QNXNTO__)
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (sel == 0) {
            /* 1 second elapsed with no user keypress: automatically refresh HUD */
            print_telemetry_hud(sup_coid);
            continue;
        } else if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }
#endif
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        char input = line[0];
        if (input == '\n' || input == '\r' || input == ' ') {
            print_telemetry_hud(sup_coid);
            continue;
        }

        if (input == 'q') {
            printf("\n[EXIT] AquaGuard shutting down cleanly...\n");
            fflush(stdout);
#if defined(__QNXNTO__)
            if (sup_coid != -1) {
                FaultInjectionMsg_t fmsg;
                memset(&fmsg, 0, sizeof(fmsg));
                fmsg.type = MSG_TYPE_FAULT_CMD;
                fmsg.fault_action = FAULT_EMERGENCY_HALT;
                MsgSend(sup_coid, &fmsg, sizeof(fmsg), NULL, 0);
                usleep(50000);
                name_close(sup_coid);
            }
#endif
            exit(0);
        }

        FaultInjectionMsg_t fmsg;
        memset(&fmsg, 0, sizeof(fmsg));
        fmsg.type = MSG_TYPE_FAULT_CMD;
        bool send = true;

        switch (input) {
            case 't':
                send = false;
                break;
            case 'b':
                fmsg.sensor_index = 1;
                fmsg.fault_action = FAULT_BIAS;
                printf("\n>>> [FAULT] INJECTING +150 ppm BIAS ON PROBE 1 <<<\n");
                break;
            case 's':
                fmsg.sensor_index = 2;
                fmsg.fault_action = FAULT_FREEZE_STUCK;
                printf("\n>>> [FAULT] FREEZING PROBE 2 VALUE <<<\n");
                break;
            case 'd':
                fmsg.sensor_index = 2;
                fmsg.fault_action = FAULT_DELAY_STALE;
                printf("\n>>> [FAULT] INDUCING ARTIFICIAL DELAY ON PROBE 2 <<<\n");
                break;
            case 'k':
                fmsg.sensor_index = 0;
                fmsg.fault_action = FAULT_KILL_SLAY;
                printf("\n>>> [FAULT] KILLING / SLAYING PROBE 0 ('k') <<<\n");
                break;
            case 'i':
                fmsg.sensor_index = 1;
                fmsg.fault_action = FAULT_MANUAL_ISOLATE;
                printf("\n>>> [OPERATOR] MANUALLY ISOLATING PROBE 1 <<<\n");
                break;
            case 'p':
                fmsg.sensor_index = -1;
                fmsg.fault_action = FAULT_PRESSURE_SPIKE;
                printf("\n>>> [FAULT] INJECTING OVERPRESSURE SPIKE (240 kPa > 180 kPa LIMIT) <<<\n");
                break;
            case 'm':
                fmsg.sensor_index = -1;
                fmsg.fault_action = FAULT_TOGGLE_DIRTY_MODE;
                printf("\n>>> [MODE] TOGGLING DIRTY WATER MODE (850 ppm injected - Press 'f' or pour water to pump) <<<\n");
                break;
            case 'f':
                fmsg.sensor_index = -1;
                fmsg.fault_action = FAULT_TRIGGER_FLOW;
                printf("\n>>> [INFLOW] TRIGGERING 3.0s WATER INFLOW CYCLE <<<\n");
                break;
            case 'r':
                fmsg.sensor_index = -1;
                fmsg.fault_action = FAULT_RESTORE_HEALTH;
                printf("\n>>> [RESTORE] TRIGGERING 6-CYCLE PROBATION SELF-HEALING RECOVERY <<<\n");
                break;
            case 'e':
                fmsg.sensor_index = -1;
                fmsg.fault_action = FAULT_EMERGENCY_HALT;
                printf("\n>>> [OPERATOR] EMERGENCY SAFE-STATE HALT ENGAGED <<<\n");
                break;
            case 'c':
                s_stress_running = !s_stress_running;
                if (s_stress_running) {
                    pthread_create(&s_stress_tid, NULL, cpu_stress_worker, NULL);
                    printf("\n>>> [CPU STRESS] 100%% CPU LOAD ACTIVE ON CORE 3 <<<\n");
                } else {
                    pthread_join(s_stress_tid, NULL);
                    printf("\n>>> [CPU STRESS] CPU LOAD STOPPED <<<\n");
                }
                send = false;
                break;
            default:
                printf("\nUnknown command '%c'. Press Enter to refresh HUD.\n", input);
                send = false;
                break;
        }

        if (send) {
#if defined(__QNXNTO__)
            MsgSend(sup_coid, &fmsg, sizeof(fmsg), NULL, 0);
#endif
        }

        /* Brief 150ms pause to let supervisor and valve controllers execute, then render updated HUD */
        usleep(150000);
        print_telemetry_hud(sup_coid);
    }

    if (s_stress_running) {
        s_stress_running = false;
        pthread_join(s_stress_tid, NULL);
    }

#if defined(__QNXNTO__)
    name_close(sup_coid);
#endif
    exit(0);
    return NULL;
}
