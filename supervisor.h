/**
 * @file supervisor.h
 * @brief Internal Data Structures and Engine Declarations for AquaGuard Supervisor
 * @standard MISRA-C:2012 / IEC 61508 SIL-3
 */

#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "aquaguard_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Operational Mode */
    OperationMode_t mode;

    /* Current System State */
    SystemState_t state;

    /* Sensor Tracking */
    float current_tds[3];
    float last_raw_tds[3];
    SensorStatus_t sensor_status[3];
    uint8_t probation_counter[3];
    uint64_t last_sensor_time_ns[3];
    float sensor_health_pct[3];

    /* Consensus Tracking */
    float active_consensus_ppm;
    float last_trusted_consensus_ppm;
    bool consensus_valid;
    int active_sensor_count;

    /* Hydraulic Status */
    float current_pressure_kpa;
    float current_flow_lpm;
    uint64_t last_hydraulic_time_ns;
    bool flow_gate_active;
    bool flow_cycle_completed;
    bool pressure_spike_active;
    uint64_t last_flow_active_time_ns;

    /* Performance, WCET and Safety Metrics */
    uint64_t fault_detected_timestamp_ns;
    uint32_t classification_latency_us;
    uint32_t valve_response_latency_us;
    uint32_t min_latency_us;
    uint32_t max_wcet_us;
    uint32_t avg_latency_us;
    uint32_t jitter_us;
    uint32_t deadline_misses;

    /* 3-Second Fault Pump Timer */
    bool fault_pump_timer_active;
    uint64_t fault_pump_deadline_ns;

    /* Actuator Target */
    ValveCommandMsg_t pending_valve_cmd;

    /* Audit Ring Buffer */
    uint8_t audit_log_count;
    AuditEvent_t audit_log[AUDIT_LOG_MAX_ENTRIES];

    /* IPC Connection IDs */
    int valve_coid;
    int watchdog_coid;

} SupervisorContext_t;

/* Global Supervisor Context */
extern SupervisorContext_t g_sup_ctx;

/* Engine APIs */
void voting_engine_init(SupervisorContext_t *ctx);
bool voting_engine_evaluate(SupervisorContext_t *ctx);

void probation_mgr_init(SupervisorContext_t *ctx);
void probation_mgr_update(SupervisorContext_t *ctx);

void state_machine_init(SupervisorContext_t *ctx);
void state_machine_step(SupervisorContext_t *ctx);

void fault_handler_apply(SupervisorContext_t *ctx, const FaultInjectionMsg_t *fault);

void audit_log_add(SupervisorContext_t *ctx, const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SUPERVISOR_H */
