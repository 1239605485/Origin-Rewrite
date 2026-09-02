#include "or_config.h"

#include <math.h>
#include <string.h>

static float or_positive(float value, float fallback) {
    return isfinite(value) && value > 0.0f ? value : fallback;
}

static float or_nonnegative(float value) {
    return isfinite(value) && value > 0.0f ? value : 0.0f;
}

static float or_clamp(float value, float low, float high) {
    if (!isfinite(value)) return low;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void or_set_progress(OR_ProgressConfig *progress,
                            const float weights[OR_MODE_COUNT][OR_TIER_COUNT],
                            const float life[OR_TIER_COUNT],
                            const float damage[OR_TIER_COUNT],
                            const float defense[OR_TIER_COUNT],
                            const int32_t defense_flat[OR_TIER_COUNT],
                            float money) {
    if (!progress) return;
    progress->money_multiplier = money;
    memcpy(progress->tier_weight, weights, sizeof(progress->tier_weight));
    memcpy(progress->life_multiplier, life, sizeof(progress->life_multiplier));
    memcpy(progress->damage_multiplier, damage, sizeof(progress->damage_multiplier));
    memcpy(progress->defense_multiplier, defense, sizeof(progress->defense_multiplier));
    memcpy(progress->defense_flat, defense_flat, sizeof(progress->defense_flat));
}

void or_config_default(OR_Config *config) {
    static const float pre_weights[OR_MODE_COUNT][OR_TIER_COUNT] = {
        {0.0f, 0.75f, 0.25f, 0.00f},
        {0.0f, 0.75f, 0.25f, 0.00f},
        {0.0f, 0.75f, 0.25f, 0.00f},
        {0.0f, 0.75f, 0.25f, 0.00f},
        {0.0f, 0.75f, 0.25f, 0.00f}
    };
    static const float hard_weights[OR_MODE_COUNT][OR_TIER_COUNT] = {
        {0.0f, 0.75f, 0.20f, 0.05f},
        {0.0f, 0.65f, 0.25f, 0.10f},
        {0.0f, 0.55f, 0.30f, 0.15f},
        {0.0f, 0.45f, 0.35f, 0.20f},
        {0.0f, 0.75f, 0.20f, 0.05f}
    };
    static const float stage_life_pre[OR_TIER_COUNT] = {0.0f, 1.25f, 1.65f, 0.0f};
    static const float stage_life_hard[OR_TIER_COUNT] = {0.0f, 1.45f, 2.10f, 3.20f};
    static const float stage_life_pre_plantera[OR_TIER_COUNT] = {0.0f, 1.70f, 2.70f, 4.20f};
    static const float stage_life_post_plantera[OR_TIER_COUNT] = {0.0f, 2.00f, 3.40f, 5.50f};
    static const float stage_life_endgame[OR_TIER_COUNT] = {0.0f, 2.40f, 4.40f, 7.00f};
    static const float stage_damage_pre[OR_TIER_COUNT] = {0.0f, 1.10f, 1.30f, 0.0f};
    static const float stage_damage_hard[OR_TIER_COUNT] = {0.0f, 1.25f, 1.60f, 2.00f};
    static const float stage_damage_pre_plantera[OR_TIER_COUNT] = {0.0f, 1.45f, 2.00f, 2.50f};
    static const float stage_damage_post_plantera[OR_TIER_COUNT] = {0.0f, 1.70f, 2.50f, 3.10f};
    static const float stage_damage_endgame[OR_TIER_COUNT] = {0.0f, 2.00f, 3.10f, 3.80f};
    static const float stage_defense[OR_TIER_COUNT] = {0.0f, 1.0f, 1.0f, 1.0f};
    static const int32_t stage_defense_pre[OR_TIER_COUNT] = {0, 2, 4, 0};
    static const int32_t stage_defense_hard[OR_TIER_COUNT] = {0, 4, 7, 12};
    static const int32_t stage_defense_pre_plantera[OR_TIER_COUNT] = {0, 6, 10, 16};
    static const int32_t stage_defense_post_plantera[OR_TIER_COUNT] = {0, 8, 14, 22};
    static const int32_t stage_defense_endgame[OR_TIER_COUNT] = {0, 10, 18, 30};

    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->schema_version = 4;
    config->enable_elites = true;
    config->enable_gameplay_hooks = true;
    config->max_active_elites = 8u;
    config->same_npc_cooldown_ticks = 60u;
    config->journey_probability_multiplier = 1.0f;
    config->allow_pre_hardmode_apocalypse = false;

    /* Temporary verification setting: every eligible NPC is upgraded so the
     * mobile Hook path can be tested without waiting for a random roll. */
    config->modes[OR_MODE_CLASSIC] = (OR_ModeConfig){1.00f, 1.00f, 1.00f, 1.00f, 0.00f};
    config->modes[OR_MODE_EXPERT] = (OR_ModeConfig){1.00f, 1.15f, 1.10f, 1.05f, 0.10f};
    config->modes[OR_MODE_MASTER] = (OR_ModeConfig){1.00f, 1.35f, 1.25f, 1.10f, 0.20f};
    config->modes[OR_MODE_ZENITH] = (OR_ModeConfig){1.00f, 1.60f, 1.45f, 1.15f, 0.30f};
    config->modes[OR_MODE_JOURNEY] = (OR_ModeConfig){1.00f, 1.00f, 1.00f, 1.00f, 0.00f};

    config->tiers[OR_TIER_NONE] =
        (OR_TierConfig){false, 1.00f, 1.00f, 0.00f, 1.00f, 1.00f, 1.00f};
    config->tiers[OR_TIER_ALTERED] =
        (OR_TierConfig){true, 1.10f, 1.50f, 0.20f, 1.05f, 1.15f, 1.00f};
    config->tiers[OR_TIER_CALAMITY] =
        (OR_TierConfig){true, 1.20f, 3.00f, 0.40f, 1.10f, 1.35f, 1.15f};
    config->tiers[OR_TIER_APOCALYPSE] =
        (OR_TierConfig){true, 1.30f, 6.00f, 0.60f, 1.15f, 1.75f, 1.30f};

    or_set_progress(&config->progress[OR_PROGRESS_PRE_HARDMODE], pre_weights,
                    stage_life_pre, stage_damage_pre, stage_defense, stage_defense_pre, 1.00f);
    or_set_progress(&config->progress[OR_PROGRESS_HARDMODE_PRE_MECH], hard_weights,
                    stage_life_hard, stage_damage_hard, stage_defense, stage_defense_hard, 1.15f);
    or_set_progress(&config->progress[OR_PROGRESS_PRE_PLANTERA], hard_weights,
                    stage_life_pre_plantera, stage_damage_pre_plantera,
                    stage_defense, stage_defense_pre_plantera, 1.35f);
    or_set_progress(&config->progress[OR_PROGRESS_POST_PLANTERA], hard_weights,
                    stage_life_post_plantera, stage_damage_post_plantera,
                    stage_defense, stage_defense_post_plantera, 1.60f);
    or_set_progress(&config->progress[OR_PROGRESS_ENDGAME], hard_weights,
                    stage_life_endgame, stage_damage_endgame,
                    stage_defense, stage_defense_endgame, 2.00f);

    config->eligibility = (OR_EligibilityConfig){false, false, false, false, false};
    /* Crates stay opt-in until target-version item IDs/opening safety are verified. */
    config->loot = (OR_LootConfig){0.20f, false, 1u};
    config->caps = (OR_Caps){0.75f, 1.25f, 4, 1.25f, 15.0f, 2.0f, 1.50f, 1.20f, 0.90f};
}

bool or_config_validate(OR_Config *config) {
    size_t i;
    bool usable = true;

    if (!config) return false;
    if (config->schema_version == 0u) config->schema_version = 4u;
    if (config->max_active_elites == 0u) config->max_active_elites = 8u;
    if (config->max_active_elites > OR_MAX_TRACKED_NPCS) config->max_active_elites = OR_MAX_TRACKED_NPCS;
    if (config->same_npc_cooldown_ticks > 3600u) config->same_npc_cooldown_ticks = 3600u;
    config->journey_probability_multiplier = or_clamp(config->journey_probability_multiplier, 0.50f, 2.00f);

    for (i = 0; i < OR_MODE_COUNT; ++i) {
        config->modes[i].elite_chance = or_clamp(config->modes[i].elite_chance, 0.0f, 1.0f);
        config->modes[i].life_multiplier = or_positive(config->modes[i].life_multiplier, 1.0f);
        config->modes[i].damage_multiplier = or_positive(config->modes[i].damage_multiplier, 1.0f);
        config->modes[i].defense_multiplier = or_positive(config->modes[i].defense_multiplier, 1.0f);
        config->modes[i].knockback_reduction = or_clamp(config->modes[i].knockback_reduction, 0.0f, 0.90f);
    }

    for (i = 0; i < OR_TIER_COUNT; ++i) {
        config->tiers[i].defense_multiplier = or_positive(config->tiers[i].defense_multiplier, 1.0f);
        config->tiers[i].money_multiplier = or_positive(config->tiers[i].money_multiplier, 1.0f);
        config->tiers[i].knockback_reduction = or_clamp(config->tiers[i].knockback_reduction, 0.0f, 0.90f);
        config->tiers[i].scale_multiplier = or_positive(config->tiers[i].scale_multiplier, 1.0f);
        config->tiers[i].slot_multiplier = or_positive(config->tiers[i].slot_multiplier, 1.0f);
        config->tiers[i].ai_intensity = or_positive(config->tiers[i].ai_intensity, 1.0f);
    }
    config->tiers[OR_TIER_NONE].enabled = false;

    for (i = 0; i < OR_PROGRESS_COUNT; ++i) {
        size_t mode;
        size_t tier;
        OR_ProgressConfig *progress = &config->progress[i];
        progress->money_multiplier = or_positive(progress->money_multiplier, 1.0f);
        for (mode = 0; mode < OR_MODE_COUNT; ++mode) {
            float sum = 0.0f;
            for (tier = 0; tier < OR_TIER_COUNT; ++tier) {
                progress->tier_weight[mode][tier] = or_nonnegative(progress->tier_weight[mode][tier]);
                if (!config->tiers[tier].enabled) progress->tier_weight[mode][tier] = 0.0f;
                if (i == OR_PROGRESS_PRE_HARDMODE && tier == OR_TIER_APOCALYPSE &&
                    !config->allow_pre_hardmode_apocalypse) {
                    progress->tier_weight[mode][tier] = 0.0f;
                }
                if (tier != OR_TIER_NONE) sum += progress->tier_weight[mode][tier];
                progress->life_multiplier[tier] = or_positive(progress->life_multiplier[tier], 1.0f);
                progress->damage_multiplier[tier] = or_positive(progress->damage_multiplier[tier], 1.0f);
                progress->defense_multiplier[tier] = or_positive(progress->defense_multiplier[tier], 1.0f);
                if (progress->defense_flat[tier] < 0) progress->defense_flat[tier] = 0;
            }
            if (sum <= 0.0f) {
                if (config->tiers[OR_TIER_ALTERED].enabled) {
                    progress->tier_weight[mode][OR_TIER_ALTERED] = 1.0f;
                    sum = 1.0f;
                } else {
                    usable = false;
                }
            }
            for (tier = OR_TIER_ALTERED; tier < OR_TIER_COUNT; ++tier) {
                progress->tier_weight[mode][tier] /= sum > 0.0f ? sum : 1.0f;
            }
        }
        /* Journey cannot accidentally acquire its own second tier table. */
        memcpy(progress->tier_weight[OR_MODE_JOURNEY],
               progress->tier_weight[OR_MODE_CLASSIC], sizeof(progress->tier_weight[OR_MODE_JOURNEY]));
    }

    config->loot.altered_extra_reward_chance = or_clamp(config->loot.altered_extra_reward_chance, 0.0f, 0.60f);
    if (config->loot.max_extra_reward_slots > 1u) config->loot.max_extra_reward_slots = 1u;
    config->caps.rule_life_damage_defense_min = or_clamp(config->caps.rule_life_damage_defense_min, 0.10f, 1.0f);
    config->caps.rule_life_damage_defense_max = or_clamp(config->caps.rule_life_damage_defense_max, 1.0f, 4.0f);
    if (config->caps.rule_life_damage_defense_min > config->caps.rule_life_damage_defense_max) {
        config->caps.rule_life_damage_defense_min = 0.75f;
        config->caps.rule_life_damage_defense_max = 1.25f;
    }
    if (config->caps.rule_defense_flat_max < 0) config->caps.rule_defense_flat_max = 0;
    if (config->caps.rule_defense_flat_max > 32) config->caps.rule_defense_flat_max = 32;
    config->caps.rule_reward_multiplier_max = or_clamp(config->caps.rule_reward_multiplier_max, 1.0f, 2.0f);
    config->caps.extra_money_multiplier_max = or_clamp(config->caps.extra_money_multiplier_max, 1.0f, 100.0f);
    config->caps.rule_chance_max = or_clamp(config->caps.rule_chance_max, 1.0f, 4.0f);
    config->caps.rule_ai_intensity_max = or_clamp(config->caps.rule_ai_intensity_max, 1.0f, 4.0f);
    config->caps.max_scale_over_vanilla = or_clamp(config->caps.max_scale_over_vanilla, 1.0f, 3.0f);
    config->caps.max_total_knockback_reduction = or_clamp(config->caps.max_total_knockback_reduction, 0.0f, 0.95f);
    return usable;
}

OR_GameMode or_config_effective_stats_mode(OR_GameMode mode) {
    return mode == OR_MODE_JOURNEY ? OR_MODE_CLASSIC : mode;
}

bool or_config_tier_allowed(const OR_Config *config,
                            OR_ProgressStage progress,
                            OR_GameMode mode,
                            OR_EliteTier tier) {
    OR_GameMode effective_mode;
    if (!config || progress < OR_PROGRESS_PRE_HARDMODE || progress >= OR_PROGRESS_COUNT ||
        mode < OR_MODE_CLASSIC || mode >= OR_MODE_COUNT || tier <= OR_TIER_NONE || tier >= OR_TIER_COUNT ||
        !config->tiers[tier].enabled) return false;
    effective_mode = or_config_effective_stats_mode(mode);
    return config->progress[progress].tier_weight[effective_mode][tier] > 0.0f;
}

const char *or_progress_stage_name(OR_ProgressStage stage) {
    static const char *names[OR_PROGRESS_COUNT] = {
        "pre_hardmode", "hardmode_pre_mech", "pre_plantera", "post_plantera", "endgame"
    };
    return stage >= 0 && stage < OR_PROGRESS_COUNT ? names[stage] : "unknown";
}

const char *or_game_mode_name(OR_GameMode mode) {
    static const char *names[OR_MODE_COUNT] = {"classic", "expert", "master", "zenith", "journey"};
    return mode >= 0 && mode < OR_MODE_COUNT ? names[mode] : "unknown";
}

const char *or_elite_tier_name(OR_EliteTier tier) {
    static const char *names[OR_TIER_COUNT] = {"none", "altered", "calamity", "apocalypse"};
    return tier >= 0 && tier < OR_TIER_COUNT ? names[tier] : "unknown";
}
