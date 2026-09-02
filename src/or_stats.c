#include "or_stats.h"

#include "or_config.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static float or_safe_multiplier(float value) {
    return isfinite(value) && value > 0.0f ? value : 1.0f;
}

static double or_nonnegative_double(double value) {
    return value > 0.0 ? value : 0.0;
}

static int64_t or_round_i64(double value) {
    if (!isfinite(value) || value <= 0.0) return 0;
    if (value >= (double)INT64_MAX) return INT64_MAX;
    return (int64_t)llround(value);
}

static int32_t or_round_i32(double value) {
    if (!isfinite(value) || value <= 0.0) return 0;
    if (value >= (double)INT32_MAX) return INT32_MAX;
    return (int32_t)lround(value);
}

static OR_ProgressStage or_valid_progress(OR_ProgressStage progress) {
    if (progress < OR_PROGRESS_PRE_HARDMODE || progress >= OR_PROGRESS_COUNT) {
        return OR_PROGRESS_PRE_HARDMODE;
    }
    return progress;
}

static OR_GameMode or_valid_mode(OR_GameMode mode) {
    if (mode < OR_MODE_CLASSIC || mode >= OR_MODE_COUNT) return OR_MODE_CLASSIC;
    return mode;
}

bool or_stats_apply(const OR_StatsInput *input, OR_FinalStats *out_stats) {
    OR_ProgressStage progress;
    OR_GameMode mode_id;
    OR_GameMode stats_mode_id;
    const OR_ProgressConfig *progress_config;
    const OR_ModeConfig *mode_config;
    const OR_TierConfig *tier_config;
    double life_multiplier;
    double damage_multiplier;
    double defense_multiplier;
    double money_multiplier;
    double money_multiplier_cap;
    double current_ratio = 1.0;
    float base_scale;
    float requested_scale;
    float max_scale;
    float knockback_reduction;

    if (!input || !input->config || !out_stats) return false;
    if (!or_config_tier_allowed(input->config, input->progress, input->mode, input->tier)) return false;

    memset(out_stats, 0, sizeof(*out_stats));
    progress = or_valid_progress(input->progress);
    mode_id = or_valid_mode(input->mode);
    stats_mode_id = or_config_effective_stats_mode(mode_id);
    progress_config = &input->config->progress[progress];
    mode_config = &input->config->modes[stats_mode_id];
    tier_config = &input->config->tiers[input->tier];

    life_multiplier = (double)or_safe_multiplier(progress_config->life_multiplier[input->tier]) *
                      (double)or_safe_multiplier(mode_config->life_multiplier) *
                      (double)or_safe_multiplier(input->rules.life_multiplier);
    damage_multiplier = (double)or_safe_multiplier(progress_config->damage_multiplier[input->tier]) *
                        (double)or_safe_multiplier(mode_config->damage_multiplier) *
                        (double)or_safe_multiplier(input->rules.damage_multiplier);
    defense_multiplier = (double)or_safe_multiplier(progress_config->defense_multiplier[input->tier]) *
                         (double)or_safe_multiplier(tier_config->defense_multiplier) *
                         (double)or_safe_multiplier(mode_config->defense_multiplier) *
                         (double)or_safe_multiplier(input->rules.defense_multiplier);
    money_multiplier = (double)or_safe_multiplier(progress_config->money_multiplier) *
                       (double)or_safe_multiplier(tier_config->money_multiplier) *
                       (double)or_safe_multiplier(input->rules.money_multiplier);
    money_multiplier_cap = input->config->caps.extra_money_multiplier_max > 0.0f
        ? input->config->caps.extra_money_multiplier_max : 15.0;
    if (money_multiplier > money_multiplier_cap) money_multiplier = money_multiplier_cap;

    out_stats->life_max = or_round_i64(or_nonnegative_double((double)input->vanilla.life_max) * life_multiplier);
    if (input->vanilla.life_max > 0 && input->vanilla.life_current > 0) {
        current_ratio = (double)input->vanilla.life_current / (double)input->vanilla.life_max;
        if (current_ratio < 0.0) current_ratio = 0.0;
        if (current_ratio > 1.0) current_ratio = 1.0;
    }
    out_stats->life_current = input->vanilla.life_max > 0 && input->vanilla.life_current > 0
        ? or_round_i64((double)out_stats->life_max * current_ratio)
        : out_stats->life_max;
    out_stats->damage = or_round_i32((double)(input->vanilla.damage > 0 ? input->vanilla.damage : 0) * damage_multiplier);
    out_stats->defense = or_round_i32((double)(input->vanilla.defense > 0 ? input->vanilla.defense : 0) * defense_multiplier +
                                      (double)progress_config->defense_flat[input->tier] +
                                      (double)input->rules.defense_flat);

    base_scale = input->vanilla.scale > 0.0f && isfinite(input->vanilla.scale) ? input->vanilla.scale : 1.0f;
    requested_scale = base_scale * (input->suppress_body_scale
                                    ? 1.0f
                                    : or_safe_multiplier(tier_config->scale_multiplier));
    max_scale = base_scale * (input->config->caps.max_scale_over_vanilla > 0.0f
                              ? input->config->caps.max_scale_over_vanilla : 1.20f);
    out_stats->scale = requested_scale > max_scale ? max_scale : requested_scale;

    knockback_reduction = tier_config->knockback_reduction + mode_config->knockback_reduction;
    if (knockback_reduction > input->config->caps.max_total_knockback_reduction) {
        knockback_reduction = input->config->caps.max_total_knockback_reduction;
    }
    if (knockback_reduction < 0.0f) knockback_reduction = 0.0f;
    out_stats->knockback_resist = input->vanilla.knockback_resist * (1.0f - knockback_reduction);
    if (!isfinite(out_stats->knockback_resist) || out_stats->knockback_resist < 0.0f) {
        out_stats->knockback_resist = 0.0f;
    }
    if (out_stats->knockback_resist > 1.0f) out_stats->knockback_resist = 1.0f;

    out_stats->npc_slots = input->vanilla.npc_slots > 0.0f && isfinite(input->vanilla.npc_slots)
        ? input->vanilla.npc_slots * or_safe_multiplier(tier_config->slot_multiplier)
        : or_safe_multiplier(tier_config->slot_multiplier);
    out_stats->money = or_round_i64(or_nonnegative_double((double)input->vanilla.money) * money_multiplier);
    return true;
}
