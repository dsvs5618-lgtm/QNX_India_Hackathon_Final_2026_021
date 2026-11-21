/**
 * @file probation_mgr.c
 * @brief Self-Healing Sensor Probation & Re-Integration Engine
 * @standard MISRA-C:2012 / IEC 61508 SIL-3
 *
 * Implements strict hysteresis observation. When a sensor is in probation,
 * it must deliver concordant readings with the active consensus for
 * CONFIG_PROBATION_CYCLES (6 consecutive cycles) before trust is restored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "supervisor.h"

static float safe_fabsf(float val) {
    return (val < 0.0f) ? -val : val;
}

void probation_mgr_init(SupervisorContext_t *ctx) {
    for (int i = 0; i < 3; i++) {
        ctx->probation_counter[i] = 0;
    }
}

void probation_mgr_update(SupervisorContext_t *ctx) {
    if (!ctx->consensus_valid) {
        /* Cannot evaluate probation without a valid reference consensus */
        return;
    }

    float ref_consensus = ctx->active_consensus_ppm;

    for (int i = 0; i < 3; i++) {
        /* Only observe sensors in PROBATION */
        if (ctx->sensor_status[i] == SENSOR_STATUS_PROBATION) {
            float diff = safe_fabsf(ctx->current_tds[i] - ref_consensus);

            if (diff <= CONFIG_PURITY_TOLERANCE_PPM) {
                ctx->probation_counter[i]++;
                printf("[PROBATION] Sensor %d matches consensus (diff=%.1f ppm). Counter: [%u/%d]\n",
                       i, diff, ctx->probation_counter[i], CONFIG_PROBATION_CYCLES);

                if (ctx->probation_counter[i] >= CONFIG_PROBATION_CYCLES) {
                    /* Restored! Re-integrate sensor */
                    ctx->sensor_status[i] = SENSOR_STATUS_ACTIVE;
                    ctx->probation_counter[i] = 0;
                    ctx->sensor_health_pct[i] = 95.0f;
                    printf("[SELF-HEALING] >>> SUCCESS: Sensor %d passed probation! Re-admitted to active voting. <<<\n", i);
                    audit_log_add(ctx, "RESTORE", "Sensor %d passed 6/6 probation -> Re-admitted to Voting", i);
                }
            } else {
                /* Disagrees with consensus: reset counter to 0 */
                if (ctx->probation_counter[i] > 0) {
                    printf("[PROBATION] Sensor %d failed stability test (diff=%.1f ppm). Resetting counter.\n",
                           i, diff);
                    audit_log_add(ctx, "PROBATION", "Sensor %d jittered (diff=%.1f ppm) -> Counter reset to 0", i, diff);
                }
                ctx->probation_counter[i] = 0;
            }
        }
    }
}
