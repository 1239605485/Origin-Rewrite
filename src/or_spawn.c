#include "or_spawn.h"

#include "or_ai.h"
#include "or_config.h"
#include "or_prng.h"
#include "or_rules.h"
#include "or_stats.h"

#include <string.h>

static bool or_valid_progress(OR_ProgressStage progress) {
    return progress >= OR_PROGRESS_PRE_HARDMODE && progress < OR_PROGRESS_COUNT;
}

static bool or_valid_mode(OR_GameMode mode) {
    return mode >= OR_MODE_CLASSIC && mode < OR_MODE_COUNT;
}

static bool or_source_is_excluded(const OR_Config *config, OR_SpawnSource source) {
    if (!config) return true;
    switch (source) {
        case OR_SPAWN_ELITE:
            return true; /* already upgraded; never roll a second tier */
        case OR_SPAWN_ELITE_SUMMONED:
            return !config->eligibility.allow_summoned;
        case OR_SPAWN_SPLIT_FROM_ELITE:
            return !config->eligibility.allow_splits;
        case OR_SPAWN_EVENT:
            return !config->eligibility.allow_events;
        case OR_SPAWN_SEGMENT:
            return !config->eligibility.allow_segments;
        case OR_SPAWN_NORMAL:
        default:
            return false;
    }
}

static uint64_t or_spawn_seed(const OR_SpawnContext *context, uint64_t random_seed) {
    uint64_t seed = random_seed;
    seed ^= context->world_session_id * UINT64_C(0x9e3779b97f4a7c15);
    seed ^= context->spawn_tick + UINT64_C(0x517cc1b727220a95);
    seed ^= (uint64_t)(uint32_t)context->npc_slot * UINT64_C(0xbf58476d1ce4e5b9);
    seed ^= (uint64_t)context->npc_type << 32;
    return seed;
}

bool or_spawn_try_commit(const OR_Config *config,
                         OR_StateStore *store,
                         const OR_SpawnContext *context,
                         uint64_t random_seed,
                         OR_SpawnResult *out_result) {
    OR_Prng rng;
    OR_RuleSnapshot rules;
    OR_StatsInput stats_input;
    OR_FinalStats final_stats;
    OR_AiPlan ai_plan;
    OR_InstanceKey key;
    OR_EliteTier tier;
    const OR_ProgressConfig *progress_config;
    const OR_ModeConfig *mode_config;
    OR_GameMode effective_mode;
    float tier_weights[OR_TIER_COUNT];
    float chance;
    size_t tier_index;
    size_t i;
    uint32_t active_limit;
    uint64_t seed;

    if (!out_result) return false;
    memset(out_result, 0, sizeof(*out_result));
    out_result->reason = OR_SPAWN_REJECT_INVALID_INPUT;
    if (!config || !store || !context || !config->enable_elites ||
        !or_valid_progress(context->progress) || !or_valid_mode(context->mode)) return false;
    if ((!context->host_authority && !context->single_player) || !context->npc_active) {
        out_result->reason = !context->npc_active
            ? OR_SPAWN_REJECT_INELIGIBLE_NPC : OR_SPAWN_REJECT_NOT_AUTHORITY;
        return false;
    }
    if (context->npc_type == 0u || context->is_town_npc || context->is_friendly || context->is_dummy ||
        (context->is_boss && !config->eligibility.allow_bosses) ||
        (context->is_segment && !config->eligibility.allow_segments) ||
        or_source_is_excluded(config, context->source)) {
        out_result->reason = or_source_is_excluded(config, context->source)
            ? OR_SPAWN_REJECT_INELIGIBLE_SOURCE : OR_SPAWN_REJECT_INELIGIBLE_NPC;
        return false;
    }

    active_limit = context->max_active_elites != 0u
        ? context->max_active_elites : config->max_active_elites;
    if (active_limit > OR_MAX_TRACKED_NPCS) active_limit = OR_MAX_TRACKED_NPCS;
    if (or_state_active_count(store) >= active_limit) {
        out_result->reason = OR_SPAWN_REJECT_LIMIT;
        return false;
    }
    if (or_state_spawn_on_cooldown(store, context->world_session_id, context->npc_slot,
                                   context->spawn_tick, config->same_npc_cooldown_ticks)) {
        out_result->reason = OR_SPAWN_REJECT_COOLDOWN;
        return false;
    }

    seed = or_spawn_seed(context, random_seed);
    or_prng_seed(&rng, seed);
    if (context->has_saved_world_rules) {
        if (!or_rules_rebind_snapshot(config, &context->saved_world_rules, context->progress,
                                      context->terrain, context->weather, context->is_night, &rules)) {
            out_result->reason = OR_SPAWN_REJECT_INVALID_INPUT;
            return false;
        }
    } else if (!or_rules_build_snapshot(config, context->progress, context->terrain, context->weather,
                                        context->is_night,
                                        context->world_rule_seed != 0u ? context->world_rule_seed : context->world_session_id,
                                        &rules)) {
        out_result->reason = OR_SPAWN_REJECT_INVALID_INPUT;
        return false;
    }
    progress_config = &config->progress[context->progress];
    mode_config = &config->modes[context->mode];
    chance = mode_config->elite_chance * rules.elite_chance_multiplier;
    if (context->mode == OR_MODE_JOURNEY) chance *= config->journey_probability_multiplier;
    if (chance > 1.0f) chance = 1.0f;
    if (!or_prng_chance(&rng, chance)) {
        out_result->reason = OR_SPAWN_REJECT_CHANCE;
        return false;
    }

    effective_mode = or_config_effective_stats_mode(context->mode);
    for (i = 0; i < OR_TIER_COUNT; ++i) {
        tier_weights[i] = progress_config->tier_weight[effective_mode][i] *
                          rules.tier_weight_multiplier[i];
    }
    if (context->progress == OR_PROGRESS_PRE_HARDMODE &&
        !config->allow_pre_hardmode_apocalypse) tier_weights[OR_TIER_APOCALYPSE] = 0.0f;
    tier_index = or_prng_weighted_index(&rng, tier_weights, OR_TIER_COUNT);
    if (tier_index == SIZE_MAX || tier_index <= OR_TIER_NONE || tier_index >= OR_TIER_COUNT ||
        !or_config_tier_allowed(config, context->progress, context->mode, (OR_EliteTier)tier_index)) {
        out_result->reason = OR_SPAWN_REJECT_TIER_DISABLED;
        return false;
    }
    tier = (OR_EliteTier)tier_index;
    if (!or_ai_build_plan(config, context->progress, tier, context->archetype, seed, &ai_plan)) {
        out_result->reason = OR_SPAWN_REJECT_TIER_DISABLED;
        return false;
    }
    ai_plan.intensity *= rules.ai_intensity;
    if (ai_plan.intensity > config->caps.rule_ai_intensity_max) {
        ai_plan.intensity = config->caps.rule_ai_intensity_max;
    }

    stats_input.config = config;
    stats_input.vanilla = context->vanilla;
    stats_input.progress = context->progress;
    stats_input.mode = context->mode;
    stats_input.tier = tier;
    stats_input.rules = rules;
    stats_input.suppress_body_scale = context->is_segment ||
                                      context->archetype == OR_AI_ARCHETYPE_WORM ||
                                      context->archetype == OR_AI_ARCHETYPE_SWARM;
    if (!or_stats_apply(&stats_input, &final_stats)) {
        out_result->reason = OR_SPAWN_REJECT_COMMIT_FAILED;
        return false;
    }

    if (!or_state_acquire_pending(store, context->world_session_id, context->npc_slot,
                                  context->npc_type, context->source, context->spawn_tick, &key)) {
        out_result->reason = OR_SPAWN_REJECT_SLOT_BUSY;
        return false;
    }
    if (!or_state_commit_spawn(store, key, context->progress, context->mode, tier,
                               &context->vanilla, &final_stats, &rules, &ai_plan) ||
        !or_state_mark_live(store, key)) {
        (void)or_state_cleanup(store, key);
        out_result->reason = OR_SPAWN_REJECT_COMMIT_FAILED;
        return false;
    }
    or_state_note_spawn(store, key);

    out_result->committed = true;
    out_result->reason = OR_SPAWN_ACCEPTED;
    out_result->key = key;
    out_result->tier = tier;
    {
        const OR_EliteRecord *record = or_state_find_const(store, key);
        if (record) out_result->record = *record;
    }
    return true;
}
