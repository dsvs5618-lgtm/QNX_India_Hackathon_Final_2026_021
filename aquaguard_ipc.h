/**
 * @file aquaguard_ipc.h
 * @brief AquaGuard Master IPC Message Definitions, Channels, and Data Types
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Compliant Architecture
 */

#ifndef AQUAGUARD_IPC_H
#define AQUAGUARD_IPC_H

#include <stdint.h>
#include <stdbool.h>

#include "aquaguard_config.h"

#if defined(__QNXNTO__)
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <sys/dispatch.h>
#else
/* Non-QNX Host Compatibility Layer for Unit Testing */
#define _PULSE_CODE_MINAVAIL 0
#define _PULSE_CODE_MAXAVAIL 127
#define EOK 0
struct _pulse {
    int16_t type;
    int16_t subtype;
    int8_t  code;
    uint8_t zero[3];
    int32_t value;
    int32_t scoid;
};
struct _msg_info {
    int nd;
    int srcnd;
    int pid;
    int tid;
    int chid;
    int scoid;
    int coid;
    int msglen;
    int srcmsglen;
    int dstmsglen;
    int priority;
    int flags;
};
typedef struct {
    int chid;
} name_attach_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * NAMED CHANNEL REGISTRATION PATHS (QNX Name Locator)
 * ========================================================================= */
#define CHANNEL_NAME_SUPERVISOR   "aquaguard_supervisor"
#define CHANNEL_NAME_VALVE        "aquaguard_valve"
#define CHANNEL_NAME_WATCHDOG     "aquaguard_watchdog"
#define CHANNEL_NAME_TELEMETRY    "aquaguard_telemetry"

/* =========================================================================
 * PULSE CODES FOR ASYNCHRONOUS EVENTS (_PULSE_CODE_MINAVAIL to MAXAVAIL)
 * ========================================================================= */
#define PULSE_CODE_WATCHDOG_HEARTBEAT    (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_CODE_FLOW_TICK             (_PULSE_CODE_MINAVAIL + 2)
#define PULSE_CODE_FAULT_INJECT          (_PULSE_CODE_MINAVAIL + 3)
#define PULSE_CODE_EMERGENCY_SAFE_STATE  (_PULSE_CODE_MINAVAIL + 4)

/* =========================================================================
 * REAL-TIME SCHED_FIFO PRIORITIES (QNX Scale: 1 to 63)
 * ========================================================================= */
#define PRIORITY_VALVE_ACTUATOR          ((int32_t)60)  /* Core 0: Highest priority decision & valve shutoff */
#define PRIORITY_SUPERVISOR              ((int32_t)55)  /* Core 1: Sensor fusion & voting engine */
#define PRIORITY_SENSOR_RESTORER         ((int32_t)50)  /* Core 1: Probation & hysteresis re-integrator */
#define PRIORITY_WATCHDOG                ((int32_t)45)  /* Core 2: Deadman pulse supervisor */
#define PRIORITY_SENSOR_ACQUISITION      ((int32_t)35)  /* Core 2: ADS1115 I2C & HX710B bit-bang timers */
#define PRIORITY_PRESSURE_FLOW           ((int32_t)30)  /* Core 2: Hydraulic monitor & interrupt handler */
#define PRIORITY_HTTP_DASHBOARD          ((int32_t)10)  /* Core 3: Web server & real-time telemetry socket */
#define PRIORITY_FAULT_INJECTOR          ((int32_t)5)   /* Core 3: Keyboard/CLI test injection harness */
#define PRIORITY_CPU_STRESS_HOG          ((int32_t)1)   /* Core 3: Lowest: CPU load demonstration */

/* =========================================================================
 * MULTI-CORE CPU RUNMASK AFFINITIES (RPi 4B: 4 Cores, 0 to 3)
 * ========================================================================= */
#define CORE_MASK_0                      ((unsigned)0x01) /* Core 0: Valve Actuator / Failsafe Decision */
#define CORE_MASK_1                      ((unsigned)0x02) /* Core 1: Supervisor, Voting Engine, Restorer */
#define CORE_MASK_2                      ((unsigned)0x04) /* Core 2: Watchdog, Sensor HAL & Timers */
#define CORE_MASK_3                      ((unsigned)0x08) /* Core 3: HTTP Dashboard, CLI, Fault Injection */

/* =========================================================================
 * SYSTEM STATE AND STATUS ENUMS
 * ========================================================================= */
typedef enum {
    MODE_NORMAL = 0,         /* Regular operation (clean water recirculated) */
    MODE_DIRTY_WATER = 1     /* Test mode simulating dirty input water */
} OperationMode_t;

typedef enum {
    STATE_NORMAL = 0,        /* All 3 TDS concordant, valves operating per mode */
    STATE_DEGRADED_1 = 1,    /* 1 TDS sensor isolated/faulty; operating on 2-sensor consensus */
    STATE_DEGRADED_2 = 2,    /* 2 TDS sensors isolated/killed; operating on single verified sensor */
    STATE_SAFE_MODE = 3,     /* EMERGENCY/SAFE STATE: Clean Valve CLOSED, Filtration Valve OPEN */
    STATE_EMERGENCY_HALT = 4 /* Total lock: Overpressure / Watchdog trip */
} SystemState_t;

typedef enum {
    SENSOR_STATUS_ACTIVE = 0,    /* Healthy and trusted in consensus */
    SENSOR_STATUS_PROBATION = 1, /* In isolation, undergoing 6-cycle stability observation */
    SENSOR_STATUS_ISOLATED = 2,  /* Excluded from voting (bias/stale detected or manual isolate) */
    SENSOR_STATUS_DEAD = 3       /* Slain/killed ('k') or no pulses received (hardware lost) */
} SensorStatus_t;

typedef enum {
    VALVE_CLOSED = 0,
    VALVE_OPEN = 1
} ValveState_t;

typedef enum {
    BUZZER_OFF = 0,
    BUZZER_PULSE_BEEP = 1,       /* Periodic short beeps (sensor dead/degraded) */
    BUZZER_CONTINUOUS_2SEC = 2   /* Solid 2-second blast (overpressure emergency) */
} BuzzerPattern_t;

/* =========================================================================
 * BLACK BOX EVENT AUDIT LOG DATA STRUCTURES
 * ========================================================================= */
#define AUDIT_LOG_MAX_ENTRIES 16
#define AUDIT_EVENT_DESC_LEN  64

typedef struct {
    uint64_t timestamp_ms;
    char event_tag[16];          /* "NORMAL", "FAULT", "ISOLATE", "PROBATION", "RESTORE", "EMERGENCY", "TIMER" */
    char description[AUDIT_EVENT_DESC_LEN];
} AuditEvent_t;

/* =========================================================================
 * IPC MESSAGE PAYLOAD STRUCTURES
 * ========================================================================= */
typedef enum {
    MSG_TYPE_SENSOR_DATA = 0x100,
    MSG_TYPE_PRESSURE_FLOW = 0x101,
    MSG_TYPE_VALVE_CMD = 0x200,
    MSG_TYPE_VALVE_ACK = 0x201,
    MSG_TYPE_FAULT_CMD = 0x300,
    MSG_TYPE_TELEMETRY_REQ = 0x400,
    MSG_TYPE_TELEMETRY_RESP = 0x401
} MsgType_t;

/* Sensor Data Packet */
typedef struct {
    uint32_t type;                      /* MSG_TYPE_SENSOR_DATA */
    uint64_t timestamp_ns;              /* CLOCK_MONOTONIC timestamp */
    float tds_ppm[3];                   /* TDS readings for sensors 0, 1, 2 */
    bool raw_valid[3];                  /* Physical read success flag from ADS1115 */
} SensorDataMsg_t;

/* Pressure & Flow Packet */
typedef struct {
    uint32_t type;                      /* MSG_TYPE_PRESSURE_FLOW */
    uint64_t timestamp_ns;
    float pressure_kpa;                 /* Water pressure from HX710B */
    float flow_rate_lpm;                /* Litres per minute from YFS401 */
} PressureFlowMsg_t;

/* Valve / Pump Actuation Command (Supervisor -> Valve Controller) */
typedef struct {
    uint32_t type;                      /* MSG_TYPE_VALVE_CMD */
    ValveState_t clean_valve;           /* Clean distribution valve */
    ValveState_t filtration_valve;      /* Safe/Filtration valve */
    bool clean_pump;                    /* 5V DC pump for clean tank (TB6612FNG) */
    bool filter_pump;                   /* 5V DC pump for filtration tank (TB6612FNG) */
    BuzzerPattern_t buzzer_mode;        /* Buzzer acoustic profile */
    bool led_green;                     /* Normal status */
    bool led_orange;                    /* Degraded status */
    bool led_red;                       /* Safe state / error */
    char reason[80];                    /* Diagnostic string */
} ValveCommandMsg_t;

/* Fault Injection Command (CLI/Harness -> Supervisor / HAL) */
typedef struct {
    uint32_t type;                      /* MSG_TYPE_FAULT_CMD */
    int sensor_index;                   /* 0, 1, or 2 (-1 for global faults) */
    enum {
        FAULT_NONE = 0,
        FAULT_BIAS,                     /* Injects +150 ppm */
        FAULT_FREEZE_STUCK,             /* Freezes reading at current value */
        FAULT_DELAY_STALE,              /* Delays packets by > 500ms */
        FAULT_MANUAL_ISOLATE,           /* Manually isolate sensor at will */
        FAULT_KILL_SLAY,                /* Mark sensor dead ('k') */
        FAULT_PRESSURE_SPIKE,           /* Injects 250 kPa overpressure */
        FAULT_TOGGLE_DIRTY_MODE,        /* Switch to dirty water mode */
        FAULT_RESTORE_HEALTH,           /* Clear injected fault */
        FAULT_EMERGENCY_HALT,           /* Operator Emergency Safe-State Halt ('e') */
        FAULT_TRIGGER_FLOW              /* Trigger 3.0s Inflow Cycle ('f') */
    } fault_action;
} FaultInjectionMsg_t;

/* Comprehensive Real-Time Telemetry Snapshot */
typedef struct {
    uint32_t type;                      /* MSG_TYPE_TELEMETRY_RESP */
    uint64_t timestamp_ns;
    OperationMode_t mode;
    SystemState_t system_state;
    float tds_ppm[3];
    SensorStatus_t sensor_status[3];
    uint8_t probation_counter[3];       /* 0 to 6 cycles completed */
    float sensor_health_pct[3];         /* Sensor degradation index (0% to 100%) */
    float pressure_kpa;
    float flow_rate_lpm;
    ValveState_t clean_valve;
    ValveState_t filtration_valve;
    bool clean_pump;
    bool filter_pump;
    uint32_t vote_consensus_ppm;
    uint32_t classification_latency_us; /* Disagreement to classification time */
    uint32_t valve_response_latency_us; /* Confirmation to valve actuation time */
    uint32_t min_latency_us;            /* Minimum observed latency */
    uint32_t max_wcet_us;               /* Worst-Case Execution Time (WCET) */
    uint32_t avg_latency_us;            /* Running average latency */
    uint32_t jitter_us;                 /* Execution jitter */
    uint32_t deadline_misses;
    uint32_t fault_pump_remaining_ms;   /* Remaining fault pump run time (ms) */
    bool flow_gate_active;              /* True if flow sensor detected water inflow */
    uint8_t audit_log_count;
    AuditEvent_t audit_log[AUDIT_LOG_MAX_ENTRIES];
} TelemetrySnapshot_t;

#ifdef __cplusplus
}
#endif

#endif /* AQUAGUARD_IPC_H */
