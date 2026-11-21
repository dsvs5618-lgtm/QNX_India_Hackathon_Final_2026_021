/**
 * @file watchdog_worker.c
 * @brief AquaGuard Core 2 Safety Watchdog Thread Worker
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
#include "unified_workers.h"

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

void *watchdog_worker(void *arg) {
    (void)arg;

    printf("====================================================\n");
    printf(" AquaGuard Core 2 Safety Watchdog Thread Starting...\n");
    printf(" Priority: %d (SCHED_FIFO) | Core Affinity: 0x%02X (Core 2)\n",
           PRIORITY_WATCHDOG, CORE_MASK_2);
    printf(" Watchdog Timeout: %d ms\n", WATCHDOG_TIMEOUT_MS);
    printf("====================================================\n");

    struct sched_param param;
    param.sched_priority = PRIORITY_WATCHDOG;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if defined(__QNXNTO__)
    unsigned runmask = CORE_MASK_2;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);

    name_attach_t *attach = name_attach(NULL, CHANNEL_NAME_WATCHDOG, 0);
    if (attach == NULL) {
        fprintf(stderr, "Failed to name_attach channel %s: %s\n",
                CHANNEL_NAME_WATCHDOG, strerror(errno));
        return NULL;
    }

    printf("[WATCHDOG] Channel registered at %s (chid=%d)\n",
           CHANNEL_NAME_WATCHDOG, attach->chid);
    fflush(stdout);

    int valve_coid = -1;
    while (valve_coid == -1) {
        valve_coid = name_open(CHANNEL_NAME_VALVE, 0);
        if (valve_coid == -1) {
            usleep(100000);
        }
    }
    printf("[WATCHDOG] Connected to Valve Channel (coid=%d)\n", valve_coid);

    uint64_t last_heartbeat_ns = get_time_ns();
    const uint64_t timeout_ns = (uint64_t)WATCHDOG_TIMEOUT_MS * 1000000ULL;

    while (1) {
        struct _msg_info info;
        uint8_t buffer[256];

        uint64_t timeout_spec = 100000000ULL; /* 100ms tick */
        TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL, &timeout_spec, NULL);

        int rcvid = MsgReceive(attach->chid, buffer, sizeof(buffer), &info);
        uint64_t now_ns = get_time_ns();

        if (rcvid == 0) {
            struct _pulse *pulse = (struct _pulse *)buffer;
            if (pulse->code == PULSE_CODE_WATCHDOG_HEARTBEAT) {
                last_heartbeat_ns = now_ns;
            }
        } else if (rcvid == -1) {
            if (errno == ETIMEDOUT) {
                if ((now_ns - last_heartbeat_ns) > timeout_ns) {
                    fprintf(stderr, "\n[WATCHDOG ALARM] DEADMAN TIMEOUT EXCEEDED! (> %d ms without pulse)\n",
                            WATCHDOG_TIMEOUT_MS);
                    fprintf(stderr, "[WATCHDOG ALARM] Directly commanding Emergency Safe State to Valve Actuator!\n");
                    
                    MsgSendPulse(valve_coid, PRIORITY_WATCHDOG,
                                 PULSE_CODE_EMERGENCY_SAFE_STATE, 0);

                    last_heartbeat_ns = now_ns;
                }
            }
        } else {
            MsgReply(rcvid, EOK, NULL, 0);
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
