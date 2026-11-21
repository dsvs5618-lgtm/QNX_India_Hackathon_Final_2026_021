/**
 * @file supervisor_worker.c
 * @brief AquaGuard Core 1 Validation & Control Supervisor Worker Thread
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Design
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#if defined(__QNXNTO__)
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/dispatch.h>
#endif

#include "aquaguard_ipc.h"
#include "supervisor.h"
#include "unified_workers.h"

SupervisorContext_t g_sup_ctx;

static float s_injected_bias[3] = {0.0f, 0.0f, 0.0f};
static bool s_sensor_frozen[3] = {false, false, false};
static float s_frozen_val[3] = {0.0f, 0.0f, 0.0f};

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

void audit_log_add(SupervisorContext_t *ctx, const char *tag, const char *fmt, ...) {
    if (!ctx) return;

    /* Shift older entries if buffer full */
    if (ctx->audit_log_count >= AUDIT_LOG_MAX_ENTRIES) {
        for (int i = 0; i < AUDIT_LOG_MAX_ENTRIES - 1; i++) {
            ctx->audit_log[i] = ctx->audit_log[i + 1];
        }
        ctx->audit_log_count = AUDIT_LOG_MAX_ENTRIES - 1;
    }

    AuditEvent_t *ev = &ctx->audit_log[ctx->audit_log_count++];
    ev->timestamp_ms = get_time_ns() / 1000000ULL;
    strncpy(ev->event_tag, tag, sizeof(ev->event_tag) - 1);
    ev->event_tag[sizeof(ev->event_tag) - 1] = '\0';

    va_list args;
    va_start(args, fmt);
    vsnprintf(ev->description, sizeof(ev->description), fmt, args);
    va_end(args);
}

static void send_watchdog_heartbeat(SupervisorContext_t *ctx) {
#if defined(__QNXNTO__)
    if (ctx->watchdog_coid == -1) {
        ctx->watchdog_coid = name_open(CHANNEL_NAME_WATCHDOG, 0);
        if (ctx->watchdog_coid == -1) return;
    }
    MsgSendPulse(ctx->watchdog_coid, PRIORITY_SUPERVISOR, PULSE_CODE_WATCHDOG_HEARTBEAT, 0);
#else
    (void)ctx;
#endif
}

static void send_valve_command(SupervisorContext_t *ctx) {
#if defined(__QNXNTO__)
    if (ctx->valve_coid == -1) {
        ctx->valve_coid = name_open(CHANNEL_NAME_VALVE, 0);
        if (ctx->valve_coid == -1) return;
    }

    uint64_t t_start = get_time_ns();
    uint64_t ack_ts = 0;
    int status = MsgSend(ctx->valve_coid, &ctx->pending_valve_cmd,
                         sizeof(ctx->pending_valve_cmd), &ack_ts, sizeof(ack_ts));
    uint64_t t_end = get_time_ns();

    if (status == -1) {
        perror("MsgSend to valve controller failed");
        ctx->deadline_misses++;
    } else {
        ctx->valve_response_latency_us = (uint32_t)((t_end - t_start) / 1000ULL);
    }
#else
    (void)ctx;
#endif
}

void fault_handler_apply(SupervisorContext_t *ctx, const FaultInjectionMsg_t *fault) {
    int idx = fault->sensor_index;
    printf("\n[FAULT INJECTION RECEIVED] Action: %d | Target Sensor: %d\n",
           fault->fault_action, idx);

    /* Arm 3.0s fault pump limit timer for any fault */
    ctx->fault_pump_deadline_ns = get_time_ns() + ((uint64_t)CONFIG_FAULT_PUMP_MAX_BURST_MS * 1000000ULL);
    ctx->fault_pump_timer_active = true;

    switch (fault->fault_action) {
        case FAULT_BIAS:
            if (idx >= 0 && idx < 3) {
                s_injected_bias[idx] = 150.0f;
                ctx->current_tds[idx] += 150.0f;
                printf("[FAULT APPLIED] Injected +150 ppm bias onto Sensor %d (Now %.1f ppm)\n",
                       idx, ctx->current_tds[idx]);
                audit_log_add(ctx, "FAULT", "+150 ppm Bias Injected on Sensor %d (Now %.1f ppm)", idx, ctx->current_tds[idx]);
            }
            break;

        case FAULT_FREEZE_STUCK:
            if (idx >= 0 && idx < 3) {
                s_sensor_frozen[idx] = true;
                s_frozen_val[idx] = ctx->current_tds[idx];
                printf("[FAULT APPLIED] Frozen sensor %d at current value (%.1f ppm)\n",
                       idx, ctx->current_tds[idx]);
                audit_log_add(ctx, "FAULT", "Sensor %d Frozen/Stuck at %.1f ppm", idx, ctx->current_tds[idx]);
            }
            break;

        case FAULT_DELAY_STALE:
            if (idx >= 0 && idx < 3) {
                printf("[FAULT APPLIED] Induced artificial delay on sensor %d (packet dropped/stale)\n", idx);
                ctx->sensor_status[idx] = SENSOR_STATUS_ISOLATED;
                audit_log_add(ctx, "FAULT", "Sensor %d Delay/Stale (>500ms) -> Isolated", idx);
            }
            break;

        case FAULT_MANUAL_ISOLATE:
            if (idx >= 0 && idx < 3) {
                printf("[FAULT APPLIED] Manually isolated Sensor %d at will.\n", idx);
                ctx->sensor_status[idx] = SENSOR_STATUS_ISOLATED;
                ctx->probation_counter[idx] = 0;
                audit_log_add(ctx, "ISOLATE", "Operator Manually Isolated Sensor %d", idx);
            }
            break;

        case FAULT_KILL_SLAY:
            if (idx >= 0 && idx < 3) {
                printf("[FAULT APPLIED] Sensor %d SLAIN ('k'). Marked DEAD.\n", idx);
                ctx->sensor_status[idx] = SENSOR_STATUS_DEAD;
                ctx->probation_counter[idx] = 0;
                audit_log_add(ctx, "FAULT", "Sensor %d SLAIN ('k') -> Hardware Marked DEAD", idx);
            }
            break;

        case FAULT_PRESSURE_SPIKE:
            ctx->pressure_spike_active = true;
            ctx->current_pressure_kpa = 240.0f;
            ctx->fault_pump_deadline_ns = get_time_ns() + ((uint64_t)CONFIG_FAULT_PUMP_MAX_BURST_MS * 1000000ULL);
            ctx->fault_pump_timer_active = true;
            printf("[FAULT APPLIED] Overpressure spike injected (240.0 kPa > 180 kPa)! Filter valve venting for 3.0s.\n");
            audit_log_add(ctx, "EMERGENCY", "Pressure Spiked to 240 kPa -> Filter Valve OPEN for 3s", 0);
            break;

        case FAULT_TOGGLE_DIRTY_MODE:
            if (ctx->mode == MODE_NORMAL) {
                ctx->mode = MODE_DIRTY_WATER;
                printf("[MODE CHANGED] Switched to DIRTY WATER MODE (850 ppm injected - Awaiting Flow/Press 'f').\n");
                ctx->current_tds[0] = 850.0f;
                ctx->current_tds[1] = 860.0f;
                ctx->current_tds[2] = 845.0f;
                ctx->fault_pump_timer_active = false;
                ctx->flow_gate_active = false;
                audit_log_add(ctx, "DIRTY_MODE", "Switched to DIRTY WATER (850 ppm) [Awaiting Flow]", 0);
            } else {
                ctx->mode = MODE_NORMAL;
                printf("[MODE CHANGED] Switched back to NORMAL CLEAN MODE.\n");
                ctx->fault_pump_timer_active = false;
                ctx->flow_gate_active = false;
                audit_log_add(ctx, "NORMAL", "Returned to NORMAL CLEAN MODE", 0);
            }
            break;

        case FAULT_TRIGGER_FLOW:
            ctx->current_flow_lpm = 2.45f;
            ctx->flow_gate_active = true;
            ctx->flow_cycle_completed = false;
            ctx->last_flow_active_time_ns = get_time_ns();
            ctx->fault_pump_deadline_ns = get_time_ns() + ((uint64_t)CONFIG_FAULT_PUMP_MAX_BURST_MS * 1000000ULL);
            ctx->fault_pump_timer_active = true;
            printf("[FLOW TRIGGERED] 3.0s Simulated Water Inflow (2.45 LPM) -> Pumping Activated!\n");
            audit_log_add(ctx, "FLOW", "Simulated Inflow (2.45 LPM) -> 3.0s Pumping", 0);
            break;

        case FAULT_RESTORE_HEALTH:
            ctx->pressure_spike_active = false;
            ctx->current_pressure_kpa = 50.0f;
            for (int i = 0; i < 3; i++) {
                s_injected_bias[i] = 0.0f;
                s_sensor_frozen[i] = false;
                if (ctx->sensor_status[i] != SENSOR_STATUS_ACTIVE) {
                    ctx->sensor_status[i] = SENSOR_STATUS_PROBATION;
                    ctx->probation_counter[i] = 0;
                }
            }
            ctx->fault_pump_timer_active = false;
            printf("[FAULT RESTORE] Pressure reset, biases cleared, probes restored to probation.\n");
            audit_log_add(ctx, "RESTORE", "Self-Healing Triggered -> Observing 6-cycle probation", 0);
            break;

        case FAULT_EMERGENCY_HALT:
            ctx->state = STATE_EMERGENCY_HALT;
            ctx->fault_pump_timer_active = false;
            printf("[EMERGENCY] Manual Operator Emergency Halt Triggered!\n");
            audit_log_add(ctx, "EMERGENCY", "Operator Manual Emergency Stop Triggered!", 0);
            break;

        default:
            break;
    }
}

void *supervisor_worker(void *arg) {
    (void)arg;

    printf("====================================================\n");
    printf(" AquaGuard Core 1 Validation Supervisor Starting...\n");
    printf(" Priority: %d (SCHED_FIFO) | Core Affinity: 0x%02X (Core 1)\n",
           PRIORITY_SUPERVISOR, CORE_MASK_1);
    printf(" Clean Limit: <= %.1f ppm | Tolerance: +/- %.1f ppm | Burst Trip: %.1f kPa\n",
           CONFIG_PURITY_CLEAN_LIMIT_PPM, CONFIG_PURITY_TOLERANCE_PPM, CONFIG_PRESSURE_BURST_TRIP_KPA);
    printf("====================================================\n");

    struct sched_param param;
    param.sched_priority = PRIORITY_SUPERVISOR;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if defined(__QNXNTO__)
    unsigned runmask = CORE_MASK_1;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);

    name_attach_t *attach = name_attach(NULL, CHANNEL_NAME_SUPERVISOR, 0);
    if (attach == NULL) {
        fprintf(stderr, "Failed to name_attach channel %s: %s\n",
                CHANNEL_NAME_SUPERVISOR, strerror(errno));
        return NULL;
    }

    printf("[SUPERVISOR] Channel registered at %s (chid=%d)\n",
           CHANNEL_NAME_SUPERVISOR, attach->chid);
    fflush(stdout);

    g_sup_ctx.valve_coid = -1;
    g_sup_ctx.watchdog_coid = -1;
    g_sup_ctx.mode = MODE_NORMAL;
    g_sup_ctx.state = STATE_NORMAL;
    g_sup_ctx.current_pressure_kpa = 50.0f;
    g_sup_ctx.current_flow_lpm = 0.0f; /* Standby: Initial flow is 0.0 LPM until water is poured */
    g_sup_ctx.audit_log_count = 0;

    voting_engine_init(&g_sup_ctx);
    probation_mgr_init(&g_sup_ctx);
    state_machine_init(&g_sup_ctx);

    audit_log_add(&g_sup_ctx, "SYSTEM", "AquaGuard SIL-3 Controller Booted on Core 1");
    audit_log_add(&g_sup_ctx, "STANDBY", "Pumps Clamped OFF (0V) - Awaiting Inflow");

    for (int i = 0; i < 3; i++) {
        g_sup_ctx.current_tds[i] = 50.0f;
        g_sup_ctx.last_raw_tds[i] = 50.0f;
        g_sup_ctx.sensor_status[i] = SENSOR_STATUS_ACTIVE;
        g_sup_ctx.probation_counter[i] = 0;
        g_sup_ctx.sensor_health_pct[i] = 98.0f;
    }

    while (1) {
        struct _msg_info info;
        uint8_t buffer[2048];
        int rcvid = MsgReceive(attach->chid, buffer, sizeof(buffer), &info);

        if (rcvid == -1) {
            perror("Supervisor MsgReceive failed");
            continue;
        }

        if (rcvid == 0) {
            continue;
        }

        uint32_t msg_type = *(uint32_t *)buffer;

        switch (msg_type) {
            case MSG_TYPE_SENSOR_DATA: {
                SensorDataMsg_t *smsg = (SensorDataMsg_t *)buffer;
                for (int i = 0; i < 3; i++) {
                    if (g_sup_ctx.mode == MODE_DIRTY_WATER) {
                        g_sup_ctx.current_tds[i] = 850.0f;
                    } else if (g_sup_ctx.sensor_status[i] == SENSOR_STATUS_DEAD) {
                        g_sup_ctx.current_tds[i] = 0.0f;
                    } else if (s_sensor_frozen[i]) {
                        g_sup_ctx.current_tds[i] = s_frozen_val[i];
                    } else {
                        g_sup_ctx.current_tds[i] = smsg->tds_ppm[i] + s_injected_bias[i];
                    }
                }

                voting_engine_evaluate(&g_sup_ctx);
                probation_mgr_update(&g_sup_ctx);
                state_machine_step(&g_sup_ctx);
                send_valve_command(&g_sup_ctx);
                send_watchdog_heartbeat(&g_sup_ctx);

                MsgReply(rcvid, EOK, NULL, 0);
                break;
            }

            case MSG_TYPE_PRESSURE_FLOW: {
                PressureFlowMsg_t *pfmsg = (PressureFlowMsg_t *)buffer;
                if (!g_sup_ctx.pressure_spike_active) {
                    g_sup_ctx.current_pressure_kpa = pfmsg->pressure_kpa;
                }
                g_sup_ctx.current_flow_lpm = pfmsg->flow_rate_lpm;

                voting_engine_evaluate(&g_sup_ctx);
                state_machine_step(&g_sup_ctx);
                send_valve_command(&g_sup_ctx);

                MsgReply(rcvid, EOK, NULL, 0);
                break;
            }

            case MSG_TYPE_FAULT_CMD: {
                FaultInjectionMsg_t *fmsg = (FaultInjectionMsg_t *)buffer;
                fault_handler_apply(&g_sup_ctx, fmsg);

                voting_engine_evaluate(&g_sup_ctx);
                state_machine_step(&g_sup_ctx);
                send_valve_command(&g_sup_ctx);

                MsgReply(rcvid, EOK, NULL, 0);
                break;
            }

            case MSG_TYPE_TELEMETRY_REQ: {
                TelemetrySnapshot_t snap;
                memset(&snap, 0, sizeof(snap));
                snap.type = MSG_TYPE_TELEMETRY_RESP;
                snap.timestamp_ns = get_time_ns();
                snap.mode = g_sup_ctx.mode;
                snap.system_state = g_sup_ctx.state;
                for (int i = 0; i < 3; i++) {
                    snap.tds_ppm[i] = g_sup_ctx.current_tds[i];
                    snap.sensor_status[i] = g_sup_ctx.sensor_status[i];
                    snap.probation_counter[i] = g_sup_ctx.probation_counter[i];
                    snap.sensor_health_pct[i] = g_sup_ctx.sensor_health_pct[i];
                }
                snap.pressure_kpa = g_sup_ctx.current_pressure_kpa;
                snap.flow_rate_lpm = g_sup_ctx.current_flow_lpm;
                snap.clean_valve = g_sup_ctx.pending_valve_cmd.clean_valve;
                snap.filtration_valve = g_sup_ctx.pending_valve_cmd.filtration_valve;
                snap.clean_pump = g_sup_ctx.pending_valve_cmd.clean_pump;
                snap.filter_pump = g_sup_ctx.pending_valve_cmd.filter_pump;
                snap.vote_consensus_ppm = (uint32_t)g_sup_ctx.active_consensus_ppm;
                snap.classification_latency_us = g_sup_ctx.classification_latency_us;
                snap.valve_response_latency_us = g_sup_ctx.valve_response_latency_us;
                snap.min_latency_us = g_sup_ctx.min_latency_us;
                snap.max_wcet_us = g_sup_ctx.max_wcet_us;
                snap.avg_latency_us = g_sup_ctx.avg_latency_us;
                snap.jitter_us = g_sup_ctx.jitter_us;
                snap.deadline_misses = g_sup_ctx.deadline_misses;
                snap.flow_gate_active = g_sup_ctx.flow_gate_active;

                if (g_sup_ctx.fault_pump_timer_active) {
                    uint64_t now_ns = get_time_ns();
                    if (now_ns < g_sup_ctx.fault_pump_deadline_ns) {
                        snap.fault_pump_remaining_ms = (uint32_t)((g_sup_ctx.fault_pump_deadline_ns - now_ns) / 1000000ULL);
                    } else {
                        snap.fault_pump_remaining_ms = 0;
                    }
                } else {
                    snap.fault_pump_remaining_ms = 0;
                }

                snap.audit_log_count = g_sup_ctx.audit_log_count;
                for (int i = 0; i < g_sup_ctx.audit_log_count && i < AUDIT_LOG_MAX_ENTRIES; i++) {
                    snap.audit_log[i] = g_sup_ctx.audit_log[i];
                }

                MsgReply(rcvid, EOK, &snap, sizeof(snap));
                break;
            }

            default:
                MsgError(rcvid, ENOSYS);
                break;
        }
    }

    name_detach(attach, 0);
#else
    while (1) {
        sleep(1);
    }
#endif
    return NULL;
}
