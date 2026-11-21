/**
 * @file voting_engine.c
 * @brief Deterministic 3-Sensor Voting and Cross-Consistency Engine
 * @standard MISRA-C:2012 / IEC 61508 SIL-3
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include "supervisor.h"

static float safe_fabsf(float val) {
    return (val < 0.0f) ? -val : val;
}

void voting_engine_init(SupervisorContext_t *ctx) {
    ctx->active_consensus_ppm = 0.0f;
    ctx->last_trusted_consensus_ppm = 250.0f; /* Nominal safe baseline */
    ctx->consensus_valid = false;
    ctx->active_sensor_count = 3;
    ctx->sensor_health_pct[0] = 100.0f;
    ctx->sensor_health_pct[1] = 100.0f;
    ctx->sensor_health_pct[2] = 100.0f;
}

/**
 * @brief Evaluates current sensor data against cross-consistency rules
 * @return true if valid consensus achieved, false if consensus failed (safe-state required)
 */
bool voting_engine_evaluate(SupervisorContext_t *ctx) {
    int active_indices[3];
    int active_cnt = 0;

    for (int i = 0; i < 3; i++) {
        if (ctx->sensor_status[i] == SENSOR_STATUS_ACTIVE) {
            active_indices[active_cnt++] = i;
        }
    }
    ctx->active_sensor_count = active_cnt;

    /* =====================================================================
     * CASE 1: All 3 Sensors Active (Triple Modular Redundancy)
     * ===================================================================== */
    if (active_cnt == 3) {
        float a = ctx->current_tds[0];
        float b = ctx->current_tds[1];
        float c = ctx->current_tds[2];

        bool ab_match = (safe_fabsf(a - b) <= CONFIG_PURITY_TOLERANCE_PPM);
        bool bc_match = (safe_fabsf(b - c) <= CONFIG_PURITY_TOLERANCE_PPM);
        bool ca_match = (safe_fabsf(c - a) <= CONFIG_PURITY_TOLERANCE_PPM);

        if (ab_match && bc_match && ca_match) {
            /* Full consensus */
            ctx->active_consensus_ppm = (a + b + c) / 3.0f;
            ctx->last_trusted_consensus_ppm = ctx->active_consensus_ppm;
            ctx->consensus_valid = true;
            goto calc_health;
        }

        /* 1 sensor deviating */
        if (ab_match && !bc_match && !ca_match) {
            /* Sensor 2 (c) is outlier */
            printf("[VOTING] Sensor 2 deviated (%.1f ppm vs A:%.1f, B:%.1f). Isolating to probation.\n", c, a, b);
            audit_log_add(ctx, "ISOLATE", "Sensor 2 deviated (%.1f ppm) -> Moved to Probation", c);
            ctx->sensor_status[2] = SENSOR_STATUS_PROBATION;
            ctx->probation_counter[2] = 0;
            ctx->active_sensor_count = 2;
            ctx->active_consensus_ppm = (a + b) / 2.0f;
            ctx->last_trusted_consensus_ppm = ctx->active_consensus_ppm;
            ctx->consensus_valid = true;
            goto calc_health;
        } else if (bc_match && !ab_match && !ca_match) {
            /* Sensor 0 (a) is outlier */
            printf("[VOTING] Sensor 0 deviated (%.1f ppm vs B:%.1f, C:%.1f). Isolating to probation.\n", a, b, c);
            audit_log_add(ctx, "ISOLATE", "Sensor 0 deviated (%.1f ppm) -> Moved to Probation", a);
            ctx->sensor_status[0] = SENSOR_STATUS_PROBATION;
            ctx->probation_counter[0] = 0;
            ctx->active_sensor_count = 2;
            ctx->active_consensus_ppm = (b + c) / 2.0f;
            ctx->last_trusted_consensus_ppm = ctx->active_consensus_ppm;
            ctx->consensus_valid = true;
            goto calc_health;
        } else if (ca_match && !ab_match && !bc_match) {
            /* Sensor 1 (b) is outlier */
            printf("[VOTING] Sensor 1 deviated (%.1f ppm vs A:%.1f, C:%.1f). Isolating to probation.\n", b, a, c);
            audit_log_add(ctx, "ISOLATE", "Sensor 1 deviated (%.1f ppm) -> Moved to Probation", b);
            ctx->sensor_status[1] = SENSOR_STATUS_PROBATION;
            ctx->probation_counter[1] = 0;
            ctx->active_sensor_count = 2;
            ctx->active_consensus_ppm = (a + c) / 2.0f;
            ctx->last_trusted_consensus_ppm = ctx->active_consensus_ppm;
            ctx->consensus_valid = true;
            goto calc_health;
        } else {
            /* Multiple sensors disagree completely with each other */
            printf("[VOTING ALERT] Unresolvable 3-way sensor disagreement! Tripping safe state.\n");
            audit_log_add(ctx, "EMERGENCY", "Unresolvable 3-way disagreement -> Safe State");
            ctx->consensus_valid = false;
            goto calc_health;
        }
    }

    /* =====================================================================
     * CASE 2: 2 Sensors Active (Dual Modular Comparison)
     * ===================================================================== */
    if (active_cnt == 2) {
        int idx1 = active_indices[0];
        int idx2 = active_indices[1];
        float val1 = ctx->current_tds[idx1];
        float val2 = ctx->current_tds[idx2];

        if (safe_fabsf(val1 - val2) <= CONFIG_PURITY_TOLERANCE_PPM) {
            ctx->active_consensus_ppm = (val1 + val2) / 2.0f;
            ctx->last_trusted_consensus_ppm = ctx->active_consensus_ppm;
            ctx->consensus_valid = true;
            goto calc_health;
        } else {
            printf("[VOTING ALERT] Dual sensors (%d: %.1f, %d: %.1f) disagree! Unsafe to arbitrate.\n",
                   idx1, val1, idx2, val2);
            audit_log_add(ctx, "EMERGENCY", "Dual sensors %d and %d disagree -> Safe State", idx1, idx2);
            ctx->consensus_valid = false;
            goto calc_health;
        }
    }

    /* =====================================================================
     * CASE 3: Only 1 Sensor Active (Historical Baseline Verification)
     * ===================================================================== */
    if (active_cnt == 1) {
        int sole_idx = active_indices[0];
        float val = ctx->current_tds[sole_idx];
        float delta = safe_fabsf(val - ctx->last_trusted_consensus_ppm);

        if (delta <= CONFIG_MAX_SINGLE_SENSOR_DRIFT_PPM) {
            /* Reading is stable and consistent with past trusted history */
            ctx->active_consensus_ppm = val;
            ctx->consensus_valid = true;
            goto calc_health;
        } else {
            printf("[VOTING ALERT] Single sensor %d drifted drastically! (%.1f ppm vs trusted %.1f, delta=%.1f). Safe state tripped.\n",
                   sole_idx, val, ctx->last_trusted_consensus_ppm, delta);
            audit_log_add(ctx, "EMERGENCY", "Single sensor %d drift (delta=%.1f) -> Safe State", sole_idx, delta);
            ctx->consensus_valid = false;
            goto calc_health;
        }
    }

    /* =====================================================================
     * CASE 4: 0 Active Sensors
     * ===================================================================== */
    printf("[VOTING ALERT] ZERO active sensors available! Total failsafe trip.\n");
    audit_log_add(ctx, "EMERGENCY", "ZERO active sensors -> Total Safe-State Trip");
    ctx->consensus_valid = false;

calc_health:
    /* Compute rolling health degradation index (0% to 100%) for each probe */
    for (int i = 0; i < 3; i++) {
        if (ctx->sensor_status[i] == SENSOR_STATUS_ACTIVE) {
            float diff = safe_fabsf(ctx->current_tds[i] - ctx->active_consensus_ppm);
            float h = 100.0f - (diff / CONFIG_PURITY_TOLERANCE_PPM) * 15.0f;
            if (h > 100.0f) h = 100.0f;
            if (h < 75.0f) h = 75.0f;
            ctx->sensor_health_pct[i] = h;
        } else if (ctx->sensor_status[i] == SENSOR_STATUS_PROBATION) {
            ctx->sensor_health_pct[i] = 30.0f + ((float)ctx->probation_counter[i] * 6.5f);
        } else if (ctx->sensor_status[i] == SENSOR_STATUS_ISOLATED) {
            ctx->sensor_health_pct[i] = 20.0f;
        } else {
            ctx->sensor_health_pct[i] = 0.0f; /* DEAD */
        }
    }

    return ctx->consensus_valid;
}
