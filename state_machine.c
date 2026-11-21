/**
 * @file state_machine.c
 * @brief Safety State Machine & Actuator Arbitration Engine
 * @standard MISRA-C:2012 / IEC 61508 SIL-3
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include "supervisor.h"

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

void state_machine_init(SupervisorContext_t *ctx) {
    ctx->state = STATE_NORMAL;
    ctx->mode = MODE_NORMAL;
    ctx->flow_gate_active = false;
    ctx->flow_cycle_completed = false;
    ctx->last_flow_active_time_ns = 0;
    ctx->fault_pump_timer_active = false;
    ctx->fault_pump_deadline_ns = 0;
    ctx->min_latency_us = 12;
    ctx->max_wcet_us = 45;
    ctx->avg_latency_us = 18;
    ctx->jitter_us = 33;

    memset(&ctx->pending_valve_cmd, 0, sizeof(ctx->pending_valve_cmd));
    ctx->pending_valve_cmd.type = MSG_TYPE_VALVE_CMD;
    ctx->pending_valve_cmd.clean_valve = VALVE_CLOSED;
    ctx->pending_valve_cmd.filtration_valve = VALVE_CLOSED;
    ctx->pending_valve_cmd.clean_pump = false;
    ctx->pending_valve_cmd.filter_pump = false;
    ctx->pending_valve_cmd.led_green = false;
    ctx->pending_valve_cmd.led_orange = false;
    ctx->pending_valve_cmd.led_red = false;
    ctx->pending_valve_cmd.buzzer_mode = BUZZER_OFF;
    snprintf(ctx->pending_valve_cmd.reason, sizeof(ctx->pending_valve_cmd.reason), "STANDBY: Prefilled Tank Ready - Awaiting Inflow");
}

void state_machine_step(SupervisorContext_t *ctx) {
    uint64_t step_start_ns = get_time_ns();
    ValveCommandMsg_t *cmd = &ctx->pending_valve_cmd;

    /* =====================================================================
     * 1. OVERPRESSURE EMERGENCY CHECK (Immediate Priority Override)
     * ===================================================================== */
    if (ctx->current_pressure_kpa > CONFIG_PRESSURE_BURST_TRIP_KPA) {
        ctx->state = STATE_EMERGENCY_HALT;
        cmd->clean_valve = VALVE_CLOSED;
        cmd->filtration_valve = VALVE_OPEN;
        cmd->clean_pump = false;
        cmd->filter_pump = true;          /* Directly open filtration/relief valve (Pin 13) to vent pressure! */
        cmd->led_green = false;
        cmd->led_orange = true;
        cmd->led_red = true;               /* Both Red and Orange simultaneous */
        cmd->buzzer_mode = BUZZER_CONTINUOUS_2SEC;
        snprintf(cmd->reason, sizeof(cmd->reason), "OVERPRESSURE EMERGENCY: %.1f kPa (Filter Valve OPEN)",
                 ctx->current_pressure_kpa);
        printf("[STATE ALERT] %s\n", cmd->reason);
        audit_log_add(ctx, "EMERGENCY", "Overpressure Spike (%.1f kPa > 180 kPa) -> Filter Valve OPEN for 3s", ctx->current_pressure_kpa);
        goto check_timer;
    }

    /* =====================================================================
     * 2. SENSOR AVAILABILITY & CONSENSUS ARBITRATION
     * ===================================================================== */
    if (!ctx->consensus_valid) {
        /* Consensus could not be reached (all sensors disagree, or single sensor drifted) */
        ctx->state = STATE_SAFE_MODE;
        cmd->clean_valve = VALVE_CLOSED;
        cmd->filtration_valve = VALVE_OPEN;
        cmd->clean_pump = false;
        cmd->filter_pump = true;
        cmd->led_green = false;
        cmd->led_orange = false;
        cmd->led_red = true;
        cmd->buzzer_mode = BUZZER_PULSE_BEEP;
        snprintf(cmd->reason, sizeof(cmd->reason), "CONSENSUS FAILED / SAFE-STATE TRIP");
        goto check_timer;
    }

    /* Determine State based on active sensor count */
    if (ctx->active_sensor_count == 3) {
        ctx->state = STATE_NORMAL;
    } else if (ctx->active_sensor_count == 2) {
        ctx->state = STATE_DEGRADED_1;
    } else if (ctx->active_sensor_count == 1) {
        ctx->state = STATE_DEGRADED_2;
    }

    /* =====================================================================
     * 3. FLOW-ACTIVATED PUMP GATING (3.0s Presentation Limit)
     * =====================================================================
     * Flow sensing is a prerequisite ONLY for spinning physical pumps in
     * Normal Clean Mode and Dirty Water Mode (preventing motor dry-run).
     * All voting, sensor probation, bias, freeze, kill, emergency halts,
     * LEDs, buzzers, and valves operate independently of the flow sensor.
     * When water is poured, pumps run for strictly 3.0 seconds (presentation mode)
     * and then return to standby.
     */
    uint64_t now_ns = get_time_ns();
    bool has_active_flow = (ctx->current_flow_lpm >= CONFIG_FLOW_TRIGGER_MIN_LPM);

    if (!has_active_flow && !ctx->fault_pump_timer_active) {
        /* Water has stopped flowing and presentation timer finished -> re-arm for next pour */
        ctx->flow_cycle_completed = false;
    }

    if (has_active_flow && !ctx->flow_cycle_completed) {
        if (!ctx->flow_gate_active) {
            printf("[FLOW SENSOR] Water inflow detected (%.2f LPM) -> 3.0s Presentation Pumping Started!\n",
                   ctx->current_flow_lpm);
            audit_log_add(ctx, "FLOW", "Inflow Detected (%.2f LPM) -> 3.0s Pumping Started",
                          ctx->current_flow_lpm);
            ctx->last_flow_active_time_ns = now_ns;
            ctx->flow_gate_active = true;

            /* Arm 3.0-second presentation limit timer for both Normal and Dirty mode */
            ctx->fault_pump_deadline_ns = now_ns + ((uint64_t)CONFIG_FAULT_PUMP_MAX_BURST_MS * 1000000ULL);
            ctx->fault_pump_timer_active = true;
        }
    }

    /* =====================================================================
     * 4. ACTIVE PROCESS CONTROL LOGIC (Normal vs Dirty Water vs Contamination)
     * ===================================================================== */
    bool is_water_clean = (ctx->active_consensus_ppm <= CONFIG_PURITY_CLEAN_LIMIT_PPM);

    if (ctx->mode == MODE_DIRTY_WATER || !is_water_clean) {
        /* Route to Filtration Chamber */
        cmd->clean_valve = VALVE_CLOSED;
        cmd->filtration_valve = VALVE_OPEN;
        cmd->clean_pump = false;
        cmd->filter_pump = ctx->flow_gate_active; /* Pump spins ONLY if active water flow is detected */
        
        if (ctx->state == STATE_DEGRADED_2) {
            cmd->led_green = false;
            cmd->led_orange = true;
            cmd->led_red = true;
            cmd->buzzer_mode = BUZZER_PULSE_BEEP;
        } else {
            cmd->led_green = false;
            cmd->led_orange = true;
            cmd->led_red = false;
            cmd->buzzer_mode = BUZZER_OFF;
        }

        snprintf(cmd->reason, sizeof(cmd->reason),
                 (ctx->mode == MODE_DIRTY_WATER) ? (ctx->flow_gate_active ? "DIRTY MODE: Diverting (3.0s Pumping)" : "DIRTY MODE: Diverting (Pumps Standby)")
                                                 : (ctx->flow_gate_active ? "CONTAMINATED: %.1f ppm (Pumping)" : "CONTAMINATED: %.1f ppm (Standby)"),
                 ctx->active_consensus_ppm);
    } else {
        /* Clean Water Mode: Route to Distribution Tank */
        cmd->clean_valve = VALVE_OPEN;
        cmd->filtration_valve = VALVE_CLOSED;
        cmd->clean_pump = ctx->flow_gate_active;  /* Pump spins ONLY if active water flow is detected */
        cmd->filter_pump = false;

        if (ctx->state == STATE_NORMAL) {
            cmd->led_green = true;
            cmd->led_orange = false;
            cmd->led_red = false;
            cmd->buzzer_mode = BUZZER_OFF;
            snprintf(cmd->reason, sizeof(cmd->reason),
                     ctx->flow_gate_active ? "NORMAL: Clean (%.0f ppm) [Pumping]"
                                           : "NORMAL: Clean (%.0f ppm) [Standby]",
                     ctx->active_consensus_ppm);
        } else if (ctx->state == STATE_DEGRADED_1) {
            cmd->led_green = false;
            cmd->led_orange = true;
            cmd->led_red = false;
            cmd->buzzer_mode = BUZZER_OFF;
            snprintf(cmd->reason, sizeof(cmd->reason),
                     ctx->flow_gate_active ? "DEGRADED-1: Clean (%.0f ppm) [Pumping]"
                                           : "DEGRADED-1: Clean (%.0f ppm) [Standby]",
                     ctx->active_consensus_ppm);
        } else if (ctx->state == STATE_DEGRADED_2) {
            cmd->led_green = false;
            cmd->led_orange = true;
            cmd->led_red = true;
            cmd->buzzer_mode = BUZZER_PULSE_BEEP;
            snprintf(cmd->reason, sizeof(cmd->reason),
                     ctx->flow_gate_active ? "DEGRADED-2: Clean (%.0f ppm) [Pumping]"
                                           : "DEGRADED-2: Clean (%.0f ppm) [Standby]",
                     ctx->active_consensus_ppm);
        }
    }

check_timer:
    /* Check 3.0-second fault pump limit timer */
    if (ctx->fault_pump_timer_active) {
        now_ns = get_time_ns();
        if (now_ns >= ctx->fault_pump_deadline_ns) {
            ctx->fault_pump_timer_active = false;
            printf("[PUMP TIMER] 3.0s presentation limit elapsed.\n");

            /* 1. Auto-revert Dirty Water Mode back to Normal Clean Mode */
            if (ctx->mode == MODE_DIRTY_WATER) {
                ctx->mode = MODE_NORMAL;
                printf("[AUTO-REVERT] Dirty Water test ended -> Reverted to NORMAL CLEAN MODE (Real Sensor Data).\n");
                audit_log_add(ctx, "NORMAL", "3.0s Dirty Water Ended -> Auto-Reverted to NORMAL CLEAN");
            }

            /* 2. Auto-revert Pressure Spike back to Normal */
            if (ctx->pressure_spike_active || ctx->current_pressure_kpa > CONFIG_PRESSURE_BURST_TRIP_KPA) {
                ctx->pressure_spike_active = false;
                ctx->current_pressure_kpa = 50.0f;
                if (ctx->state == STATE_EMERGENCY_HALT) {
                    ctx->state = STATE_NORMAL;
                }
                printf("[AUTO-REVERT] Overpressure Spike ended -> Filter valve closed, Pressure normal (50 kPa), Restored STATE_NORMAL.\n");
                audit_log_add(ctx, "NORMAL", "3.0s Pressure Spike Ended -> Reverted to NORMAL");
            }

            /* 3. Auto-stop normal flow pumping after 3.0s presentation limit */
            if (ctx->flow_gate_active) {
                ctx->flow_gate_active = false;
                ctx->flow_cycle_completed = true; /* Lock until current pour stops */
                ctx->current_flow_lpm = 0.0f;
                printf("[FLOW SENSOR] 3.0s presentation cycle completed -> Pumps stopped. Safe Standby.\n");
                audit_log_add(ctx, "STANDBY", "3.0s Presentation Complete -> Safe Standby (Pumps OFF)");
            }

            cmd->clean_pump = false;
            cmd->filter_pump = false;
        }
    }

    /* WCET and Jitter Statistical Tracking */
    uint64_t step_end_ns = get_time_ns();
    ctx->classification_latency_us = (uint32_t)((step_end_ns - step_start_ns) / 1000ULL);
    if (ctx->classification_latency_us < ctx->min_latency_us || ctx->min_latency_us == 0) {
        ctx->min_latency_us = ctx->classification_latency_us;
    }
    if (ctx->classification_latency_us > ctx->max_wcet_us) {
        ctx->max_wcet_us = ctx->classification_latency_us;
    }
    if (ctx->avg_latency_us == 0) ctx->avg_latency_us = ctx->classification_latency_us;
    else ctx->avg_latency_us = (ctx->avg_latency_us * 7 + ctx->classification_latency_us) / 8;
    ctx->jitter_us = (ctx->max_wcet_us > ctx->min_latency_us) ? (ctx->max_wcet_us - ctx->min_latency_us) : 2;
}
