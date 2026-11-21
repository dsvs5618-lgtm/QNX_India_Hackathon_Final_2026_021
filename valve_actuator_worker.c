/**
 * @file valve_actuator_worker.c
 * @brief AquaGuard Core 0 Valve Controller & Failsafe Actuation Worker Thread
 * @standard MISRA-C:2012 / IEC 61508 SIL-3 Design
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
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
#include "hardware_pins.h"
#include "rpi_hardware.h"
#include "unified_workers.h"

static ValveState_t s_clean_valve = VALVE_CLOSED;
static ValveState_t s_filtration_valve = VALVE_CLOSED;
static bool s_clean_pump = false;
static bool s_filter_pump = false;
static BuzzerPattern_t s_buzzer = BUZZER_OFF;
static bool s_led_green = false;
static bool s_led_orange = false;
static bool s_led_red = true;

static uint64_t s_last_cmd_timestamp_ns = 0;
static uint32_t s_actuation_count = 0;
static uint32_t s_safe_state_trips = 0;
static uint32_t s_buzzer_pulse_cnt = 0;
static uint32_t s_buzzer_2sec_cnt = 0;

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static void apply_hardware_outputs(const ValveCommandMsg_t *cmd) {
    s_clean_valve = cmd->clean_valve;
    s_filtration_valve = cmd->filtration_valve;
    s_clean_pump = cmd->clean_pump;
    s_filter_pump = cmd->filter_pump;
    s_buzzer = cmd->buzzer_mode;
    s_led_green = cmd->led_green;
    s_led_orange = cmd->led_orange;
    s_led_red = cmd->led_red;

    s_last_cmd_timestamp_ns = get_time_ns();
    s_actuation_count++;

    static ValveState_t s_last_c_v = (ValveState_t)-1;
    static ValveState_t s_last_f_v = (ValveState_t)-1;
    static int s_last_c_p = -1;
    static int s_last_f_p = -1;

    bool state_changed = (cmd->clean_valve != s_last_c_v ||
                          cmd->filtration_valve != s_last_f_v ||
                          (int)cmd->clean_pump != s_last_c_p ||
                          (int)cmd->filter_pump != s_last_f_p);

    if (state_changed || (s_actuation_count % 20 == 0)) {
        printf("[VALVE ACTUATOR #%u] Clean:%s | Filter:%s | PumpClean:%d | PumpFilter:%d | LEDs(G:%d,O:%d,R:%d) | Reason: %s\n",
               s_actuation_count,
               s_clean_valve == VALVE_OPEN ? "OPEN" : "CLOSED",
               s_filtration_valve == VALVE_OPEN ? "OPEN" : "CLOSED",
               s_clean_pump, s_filter_pump,
               s_led_green, s_led_orange, s_led_red,
               cmd->reason);
        s_last_c_v = cmd->clean_valve;
        s_last_f_v = cmd->filtration_valve;
        s_last_c_p = (int)cmd->clean_pump;
        s_last_f_p = (int)cmd->filter_pump;
    }

#if USE_PHYSICAL_HARDWARE
    /* Actuate physical 5V DC Submersible Pumps via TB6612FNG Driver Inputs */
    rpi_gpio_write(PIN_PUMP_CLEAN, cmd->clean_pump ? 1 : 0);       /* Pin 11 / GPIO 17 (PWMA) */
    rpi_gpio_write(PIN_PUMP_FILTER, cmd->filter_pump ? 1 : 0);     /* Pin 13 / GPIO 27 (PWMB) */

    /* Drive physical Status LEDs */
    rpi_gpio_write(PIN_LED_GREEN, cmd->led_green ? 1 : 0);         /* Pin 40 / GPIO 21 */
    rpi_gpio_write(PIN_LED_ORANGE, cmd->led_orange ? 1 : 0);       /* Pin 38 / GPIO 20 */
    rpi_gpio_write(PIN_LED_RED, cmd->led_red ? 1 : 0);             /* Pin 36 / GPIO 16 */

    /* Deterministic Acoustic Alarm Buzzer Driver (Non-blocking pulse/continuous) */
    if (cmd->buzzer_mode == BUZZER_OFF) {
        rpi_gpio_write(PIN_BUZZER, CONFIG_BUZZER_ACTIVE_LOW ? 1 : 0);
        s_buzzer_pulse_cnt = 0;
        s_buzzer_2sec_cnt = 0;
    } else if (cmd->buzzer_mode == BUZZER_PULSE_BEEP) {
        /* Pulse Beep: 100ms chirp every 1.5 seconds (30 cycles of 50ms) */
        s_buzzer_pulse_cnt++;
        int buzz_on = ((s_buzzer_pulse_cnt % 30) < 2) ? 1 : 0;
        rpi_gpio_write(PIN_BUZZER, CONFIG_BUZZER_ACTIVE_LOW ? (!buzz_on) : buzz_on);
        s_buzzer_2sec_cnt = 0;
    } else if (cmd->buzzer_mode == BUZZER_CONTINUOUS_2SEC) {
        /* 2-Second Solid Alarm: 40 cycles at 50ms, then shut off */
        if (s_buzzer_2sec_cnt < 40) {
            s_buzzer_2sec_cnt++;
            rpi_gpio_write(PIN_BUZZER, CONFIG_BUZZER_ACTIVE_LOW ? 0 : 1);
        } else {
            rpi_gpio_write(PIN_BUZZER, CONFIG_BUZZER_ACTIVE_LOW ? 1 : 0);
        }
    }
#endif

    if (s_buzzer == BUZZER_CONTINUOUS_2SEC && s_buzzer_2sec_cnt == 1) {
        printf(" >>> [BUZZER SOLID 2s] HIGH PRESSURE EMERGENCY ALARM TRIGGERED! <<<\n");
    } else if (s_buzzer == BUZZER_PULSE_BEEP && (s_buzzer_pulse_cnt % 30) == 1) {
        printf(" >>> [BUZZER BEEP] Sensor degraded / warning chirp <<<\n");
    }
}

static void force_failsafe_safe_state(const char *reason) {
    bool is_boot = (strcmp(reason, "Initial boot state") == 0);

    s_clean_valve = VALVE_CLOSED;
    s_filtration_valve = is_boot ? VALVE_CLOSED : VALVE_OPEN;
    s_clean_pump = false;
    s_filter_pump = is_boot ? false : true; /* Strictly OFF at boot; ON only during real emergency */
    s_led_green = false;
    s_led_orange = false;
    s_led_red = true;
    s_buzzer = is_boot ? BUZZER_OFF : BUZZER_PULSE_BEEP; /* Strictly OFF at boot */

    s_safe_state_trips++;

#if USE_PHYSICAL_HARDWARE
    /* Physical Failsafe Pin Actuation */
    rpi_gpio_write(PIN_PUMP_CLEAN, 0);        /* Clean pump clamped OFF (0.0V) */
    rpi_gpio_write(PIN_PUMP_FILTER, is_boot ? 0 : 1); /* Filter pump OFF at boot to preserve prefilled water */
    rpi_gpio_write(PIN_LED_GREEN, 0);
    rpi_gpio_write(PIN_LED_ORANGE, 0);
    rpi_gpio_write(PIN_LED_RED, 1);           /* Red Alarm LED */
    rpi_gpio_write(PIN_BUZZER, is_boot ? (CONFIG_BUZZER_ACTIVE_LOW ? 1 : 0) : (CONFIG_BUZZER_ACTIVE_LOW ? 0 : 1));
#endif

    printf("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    printf(" [FAILSAFE ACTUATOR TRIP #%u] %s\n", s_safe_state_trips, reason);
    printf(" ACTUATING HARDWARE TO SAFE STATE:\n");
    printf("   --> CLEAN VALVE:      CLOSED (Prevent contaminated distribution)\n");
    printf("   --> FILTRATION VALVE: %s\n", is_boot ? "CLOSED (Standby)" : "OPEN (Divert)");
    printf("   --> CLEAN PUMP:       OFF (0.0V)\n");
    printf("   --> FILTER PUMP:      %s (0.0V)\n", is_boot ? "OFF" : "ON");
    printf("   --> BUZZER:           %s\n", is_boot ? "OFF (Standby Silent)" : "PULSE");
    printf("   --> RED LED:          SOLID (Alarm/Standby)\n");
    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n");
}

void *valve_actuator_worker(void *arg) {
    (void)arg;

    printf("====================================================\n");
    printf(" AquaGuard Core 0 Valve Controller Thread Starting...\n");
    printf(" Priority: %d (SCHED_FIFO) | Core Affinity: 0x%02X (Core 0)\n",
           PRIORITY_VALVE_ACTUATOR, CORE_MASK_0);
    printf("====================================================\n");

    struct sched_param param;
    param.sched_priority = PRIORITY_VALVE_ACTUATOR;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if defined(__QNXNTO__)
    unsigned runmask = CORE_MASK_0;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);

    name_attach_t *attach = name_attach(NULL, CHANNEL_NAME_VALVE, 0);
    if (attach == NULL) {
        fprintf(stderr, "Failed to name_attach channel %s: %s\n",
                CHANNEL_NAME_VALVE, strerror(errno));
        return NULL;
    }

    printf("[VALVE_ACTUATOR] Channel registered at %s (chid=%d)\n",
           CHANNEL_NAME_VALVE, attach->chid);
    fflush(stdout);

    force_failsafe_safe_state("Initial boot state");

    while (1) {
        struct _msg_info info;
        ValveCommandMsg_t msg;
        int rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), &info);

        if (rcvid == -1) {
            perror("MsgReceive failed");
            continue;
        }

        if (rcvid == 0) {
            struct _pulse *pulse = (struct _pulse *)&msg;
            if (pulse->code == PULSE_CODE_EMERGENCY_SAFE_STATE) {
                force_failsafe_safe_state("Emergency Safe State Pulse Received");
            }
            continue;
        }

        if (msg.type == MSG_TYPE_VALVE_CMD) {
            if (msg.clean_valve == VALVE_OPEN && msg.filtration_valve == VALVE_OPEN) {
                force_failsafe_safe_state("Conflicting dual-open command rejected");
            } else {
                apply_hardware_outputs(&msg);
            }

            uint64_t ack_ts = get_time_ns();
            MsgReply(rcvid, EOK, &ack_ts, sizeof(ack_ts));
        } else {
            MsgError(rcvid, ENOSYS);
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
