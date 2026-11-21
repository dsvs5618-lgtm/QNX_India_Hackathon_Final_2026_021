/**
 * @file sensor_adapter_worker.c
 * @brief AquaGuard Sensor Adapter Acquisition HAL Thread Worker
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

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

void *sensor_adapter_worker(void *arg) {
    (void)arg;

    printf("====================================================\n");
    printf(" AquaGuard Sensor Adapter Acquisition Thread Starting...\n");
    printf(" Priority: %d | Core Affinity: 0x%02X (Core 2)\n",
           PRIORITY_SENSOR_ACQUISITION, CORE_MASK_2);
    printf("====================================================\n");

    struct sched_param param;
    param.sched_priority = PRIORITY_SENSOR_ACQUISITION;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

#if defined(__QNXNTO__)
    unsigned runmask = CORE_MASK_2;
    ThreadCtl(_NTO_TCTL_RUNMASK, (void *)(uintptr_t)runmask);

    int sup_coid = -1;
    while (sup_coid == -1) {
        sup_coid = name_open(CHANNEL_NAME_SUPERVISOR, 0);
        if (sup_coid == -1) {
            usleep(200000);
        }
    }
    printf("[SENSOR ADAPTER] Connected to Supervisor (coid=%d)\n", sup_coid);

#if USE_PHYSICAL_HARDWARE
    rpi_hw_init();
    printf("[SENSOR ADAPTER] >>> PHYSICAL HARDWARE MODE ACTIVE <<< (ADS1115 I2C + HX710B + YFS401)\n");
#else
    printf("[SENSOR ADAPTER] >>> SIMULATION DRY-RUN MODE ACTIVE <<<\n");
#endif

#if !USE_PHYSICAL_HARDWARE
    float base_tds = 250.0f;
    float base_pressure = 50.0f;
    float base_flow = 2.4f;
#endif

    while (1) {
        SensorDataMsg_t smsg;
        memset(&smsg, 0, sizeof(smsg));
        smsg.type = MSG_TYPE_SENSOR_DATA;
        smsg.timestamp_ns = get_time_ns();

        PressureFlowMsg_t pfmsg;
        memset(&pfmsg, 0, sizeof(pfmsg));
        pfmsg.type = MSG_TYPE_PRESSURE_FLOW;
        pfmsg.timestamp_ns = get_time_ns();

#if USE_PHYSICAL_HARDWARE
        /* Read physical ADS1115 I2C Channels for TDS Probes */
        smsg.tds_ppm[0] = rpi_ads1115_read_tds_ppm(ADS1115_CH_TDS0);
        smsg.tds_ppm[1] = rpi_ads1115_read_tds_ppm(ADS1115_CH_TDS1);
        smsg.tds_ppm[2] = rpi_ads1115_read_tds_ppm(ADS1115_CH_TDS2);
        smsg.raw_valid[0] = true;
        smsg.raw_valid[1] = true;
        smsg.raw_valid[2] = true;

        /* Read physical HX710B Pressure & YFS401 Flow */
        pfmsg.pressure_kpa = rpi_hx710b_read_pressure_kpa();
        pfmsg.flow_rate_lpm = rpi_yfs401_read_flow_lpm();
#else
        smsg.tds_ppm[0] = base_tds + ((float)(rand() % 10) - 5.0f);
        smsg.tds_ppm[1] = base_tds + ((float)(rand() % 10) - 5.0f);
        smsg.tds_ppm[2] = base_tds + ((float)(rand() % 10) - 5.0f);
        smsg.raw_valid[0] = true;
        smsg.raw_valid[1] = true;
        smsg.raw_valid[2] = true;

        pfmsg.pressure_kpa = base_pressure + ((float)(rand() % 6) - 3.0f);
        pfmsg.flow_rate_lpm = base_flow + ((float)(rand() % 4) - 2.0f) * 0.1f;
#endif

        MsgSend(sup_coid, &smsg, sizeof(smsg), NULL, 0);
        MsgSend(sup_coid, &pfmsg, sizeof(pfmsg), NULL, 0);

        usleep(50000); /* 50ms sampling loop (20 Hz) */
    }

    name_close(sup_coid);
#else
    while (1) {
        sleep(1);
    }
#endif
    return NULL;
}
