#include "or_world.h"

#include <string.h>

#define OR_WORLD_RULE_REFRESH_DAYS 3u

static bool or_saved_snapshot_valid(const OR_RuleSnapshot *snapshot) {
    size_t i;
    uint32_t selected_mask = 0u;
    if (!snapshot || snapshot->selected_count < 2u || snapshot->selected_count > OR_MAX_WORLD_RULES ||
        snapshot->progress < OR_PROGRESS_PRE_HARDMODE || snapshot->progress >= OR_PROGRESS_COUNT ||
        snapshot->terrain.depth < OR_DEPTH_SURFACE || snapshot->terrain.depth >= OR_DEPTH_COUNT ||
        snapshot->terrain.biome < OR_BIOME_FOREST || snapshot->terrain.biome >= OR_BIOME_COUNT ||
        snapshot->terrain.special < OR_SPECIAL_NONE || snapshot->terrain.special >= OR_SPECIAL_COUNT ||
        snapshot->weather < OR_WEATHER_CLEAR || snapshot->weather >= OR_WEATHER_COUNT) {
        return false;
    }
    for (i = 0; i < snapshot->selected_count; ++i) {
        size_t j;
        if (snapshot->selected_ids[i] >= OR_RULE_COUNT) return false;
        selected_mask |= 1u << snapshot->selected_ids[i];
        for (j = i + 1u; j < snapshot->selected_count; ++j) {
            if (snapshot->selected_ids[i] == snapshot->selected_ids[j]) return false;
        }
    }
    return (snapshot->active_mask & ~selected_mask) == 0u;
}

void or_world_rule_state_init(OR_WorldRuleState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

OR_WorldRuleStateStatus or_world_rules_create(OR_WorldRuleState *state,
                                              const OR_Config *config,
                                              OR_ProgressStage progress,
                                              OR_TerrainSnapshot terrain,
                                              OR_Weather weather,
                                              bool is_night,
                                              uint64_t rule_seed,
                                              uint64_t config_hash,
                                              uint64_t world_seed_fingerprint) {
    if (!state || !config || rule_seed == 0u || world_seed_fingerprint == 0u) {
        return OR_WORLD_RULES_INVALID_INPUT;
    }
    or_world_rule_state_init(state);
    state->save_format_version = 1u;
    state->rule_pool_version = 1u;
    state->rule_seed = rule_seed;
    state->config_hash = config_hash;
    state->world_seed_fingerprint = world_seed_fingerprint;
    if (!or_rules_build_snapshot(config, progress, terrain, weather, is_night, rule_seed,
                                 &state->snapshot)) {
        state->disabled_safe_mode = true;
        return OR_WORLD_RULES_DISABLED_SAFE_MODE;
    }
    state->initialized = true;
    state->refresh_count = 0u;
    state->last_refresh_day = 0u;
    state->next_refresh_day = OR_WORLD_RULE_REFRESH_DAYS;
    state->history_count = 0u;
    return OR_WORLD_RULES_VALID;
}

OR_WorldRuleStateStatus or_world_rules_refresh(OR_WorldRuleState *state,
                                               const OR_Config *config,
                                               OR_ProgressStage progress,
                                               OR_TerrainSnapshot terrain,
                                               OR_Weather weather,
                                               bool is_night,
                                               uint64_t game_day) {
    OR_RuleSnapshot next_snapshot;
    OR_WorldRuleHistoryEntry *history;
    uint64_t next_seed;
    uint32_t history_index;
    if (!state || !config || !state->initialized || state->disabled_safe_mode ||
        game_day < state->next_refresh_day) {
        return state && state->initialized && !state->disabled_safe_mode
            ? OR_WORLD_RULES_VALID : OR_WORLD_RULES_INVALID_INPUT;
    }
    next_seed = state->rule_seed ^
        (game_day * UINT64_C(0x9e3779b97f4a7c15)) ^
        ((state->refresh_count + 1u) * UINT64_C(0xbf58476d1ce4e5b9));
    if (!or_rules_build_snapshot(config, progress, terrain, weather, is_night,
                                 next_seed, &next_snapshot)) {
        return OR_WORLD_RULES_DISABLED_SAFE_MODE;
    }
    history_index = state->history_count % OR_WORLD_RULE_HISTORY_LIMIT;
    history = &state->history[history_index];
    history->effective_day = state->last_refresh_day;
    history->revision = state->refresh_count + 1u;
    history->snapshot = state->snapshot;
    if (state->history_count < OR_WORLD_RULE_HISTORY_LIMIT) {
        state->history_count += 1u;
    }
    state->snapshot = next_snapshot;
    state->rule_seed = next_seed;
    state->refresh_count += 1u;
    state->last_refresh_day = game_day;
    state->next_refresh_day = game_day + OR_WORLD_RULE_REFRESH_DAYS;
    return OR_WORLD_RULES_VALID;
}

OR_WorldRuleStateStatus or_world_rules_restore(OR_WorldRuleState *state,
                                               const OR_Config *config,
                                               uint32_t expected_pool_version,
                                               uint32_t expected_save_format_version,
                                               uint64_t expected_config_hash,
                                               uint64_t expected_world_seed_fingerprint) {
    if (!state || !config) return OR_WORLD_RULES_INVALID_INPUT;
    if (!state->initialized || state->disabled_safe_mode || state->rule_seed == 0u ||
        state->rule_pool_version != expected_pool_version ||
        state->save_format_version != expected_save_format_version ||
        state->config_hash != expected_config_hash ||
        state->world_seed_fingerprint != expected_world_seed_fingerprint ||
        !or_saved_snapshot_valid(&state->snapshot)) {
        state->disabled_safe_mode = true;
        state->initialized = false;
        or_rules_clear(&state->snapshot);
        return OR_WORLD_RULES_DISABLED_SAFE_MODE;
    }
    return OR_WORLD_RULES_VALID;
}
