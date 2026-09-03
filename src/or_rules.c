#include "or_rules.h"

#include "or_prng.h"

#include <string.h>

typedef struct OR_RuleDefinition {
    const char *name;
    uint32_t conflicts;
    float life_multiplier;
    float damage_multiplier;
    float defense_multiplier;
    int32_t defense_flat;
    float probability_multiplier;
    float reward_chance_bonus;
    float reward_quality_multiplier;
    float movement_multiplier;
    float tier_weight_multiplier[OR_TIER_COUNT];
    OR_ElementTag preferred_element;
    bool requires_night;
    bool requires_weather;
    bool requires_depth;
    bool requires_evil_biome;
} OR_RuleDefinition;

static const OR_RuleDefinition g_rule_definitions[OR_RULE_COUNT] = {
    {"elite_tide", (1u << OR_RULE_STORM_BORDER), 1.00f, 1.00f, 1.00f, 0,
     1.25f, 0.10f, 1.00f, 1.00f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     false, false, false, false},
    {"strong_world", (1u << OR_RULE_GLASS_CANNON), 1.15f, 1.15f, 1.00f, 0,
     1.00f, 0.00f, 1.00f, 1.00f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     false, false, false, false},
    {"harvest_contract", 0u, 1.00f, 1.00f, 1.00f, 0,
     1.00f, 0.15f, 1.10f, 1.00f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     false, false, false, false},
    {"storm_border", (1u << OR_RULE_ELITE_TIDE), 1.00f, 1.00f, 1.00f, 0,
     1.15f, 0.00f, 1.00f, 1.00f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     false, true, false, false},
    {"night_law", 0u, 1.00f, 1.00f, 1.00f, 0,
     1.00f, 0.00f, 1.00f, 1.10f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     true, false, false, false},
    {"abyss_echo", 0u, 1.00f, 1.00f, 1.00f, 0,
     1.00f, 0.00f, 1.00f, 1.00f, {1.00f, 1.00f, 1.15f, 1.00f}, OR_ELEMENT_NONE,
     false, false, true, false},
    {"evil_infection", 0u, 1.00f, 1.00f, 1.00f, 0,
     1.00f, 0.00f, 1.00f, 1.00f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     false, false, false, true},
    {"glass_cannon", (1u << OR_RULE_STRONG_WORLD), 0.85f, 1.15f, 1.00f, 0,
     1.00f, 0.00f, 1.00f, 1.00f, {1.00f, 1.00f, 1.00f, 1.00f}, OR_ELEMENT_NONE,
     false, false, false, false}
};

static bool or_depth_is_subterranean(OR_DepthTag depth) {
    return depth == OR_DEPTH_UNDERGROUND || depth == OR_DEPTH_CAVERN;
}

static bool or_biome_is_evil(OR_BiomeTag biome) {
    return biome == OR_BIOME_CORRUPTION || biome == OR_BIOME_CRIMSON;
}

static OR_ElementTag or_element_for_biome(OR_BiomeTag biome) {
    if (biome == OR_BIOME_SNOW) return OR_ELEMENT_FROST;
    if (biome == OR_BIOME_JUNGLE) return OR_ELEMENT_TOXIC;
    if (biome == OR_BIOME_CORRUPTION) return OR_ELEMENT_CORRUPT;
    if (biome == OR_BIOME_CRIMSON) return OR_ELEMENT_CRIMSON;
    return OR_ELEMENT_NONE;
}

static bool or_rule_condition_matches(const OR_RuleDefinition *definition,
                                      const OR_RuleSnapshot *snapshot) {
    if (definition->requires_night && !snapshot->is_night) return false;
    if (definition->requires_weather && snapshot->weather == OR_WEATHER_CLEAR) return false;
    if (definition->requires_depth && !or_depth_is_subterranean(snapshot->terrain.depth)) return false;
    if (definition->requires_evil_biome && !or_biome_is_evil(snapshot->terrain.biome)) return false;
    return true;
}

void or_rules_clear(OR_RuleSnapshot *snapshot) {
    size_t i;
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->life_multiplier = 1.0f;
    snapshot->damage_multiplier = 1.0f;
    snapshot->defense_multiplier = 1.0f;
    snapshot->money_multiplier = 1.0f;
    snapshot->elite_chance_multiplier = 1.0f;
    snapshot->reward_quality_multiplier = 1.0f;
    snapshot->movement_multiplier = 1.0f;
    snapshot->preferred_element = OR_ELEMENT_NONE;
    for (i = 0; i < OR_TIER_COUNT; ++i) snapshot->tier_weight_multiplier[i] = 1.0f;
}

static bool or_rule_already_selected(const OR_RuleSnapshot *snapshot, uint32_t id) {
    size_t i;
    for (i = 0; i < snapshot->selected_count; ++i) {
        if (snapshot->selected_ids[i] == id) return true;
    }
    return false;
}

static void or_apply_context_modifiers(OR_RuleSnapshot *snapshot) {
    if (or_depth_is_subterranean(snapshot->terrain.depth)) {
        snapshot->life_multiplier *= 1.05f;
        snapshot->defense_multiplier *= 1.05f;
        snapshot->tier_weight_multiplier[OR_TIER_CALAMITY] *= 1.15f;
    }
    if (snapshot->terrain.depth == OR_DEPTH_UNDERWORLD &&
        snapshot->progress != OR_PROGRESS_PRE_HARDMODE) {
        snapshot->tier_weight_multiplier[OR_TIER_APOCALYPSE] *= 1.15f;
    }
    if (snapshot->terrain.biome == OR_BIOME_FOREST) {
        snapshot->tier_weight_multiplier[OR_TIER_ALTERED] *= 1.10f;
    }
    if (snapshot->weather == OR_WEATHER_BLOOD_MOON) {
        snapshot->elite_chance_multiplier *= 1.15f;
        snapshot->reward_quality_multiplier *= 1.10f;
    }
    if (snapshot->weather == OR_WEATHER_ECLIPSE) {
        snapshot->reward_quality_multiplier *= 1.10f;
    }
    if ((snapshot->active_mask & (1u << OR_RULE_EVIL_INFECTION)) != 0u &&
        snapshot->preferred_element == OR_ELEMENT_NONE) {
        snapshot->preferred_element = or_element_for_biome(snapshot->terrain.biome);
    }
}

bool or_rules_build_snapshot(const OR_Config *config,
                             OR_ProgressStage stage,
                             OR_TerrainSnapshot terrain,
                             OR_Weather weather,
                             bool is_night,
                             uint64_t world_rule_seed,
                             OR_RuleSnapshot *out_snapshot) {
    OR_Prng rng;
    size_t attempts = 0;
    size_t target;

    if (!out_snapshot || stage < OR_PROGRESS_PRE_HARDMODE || stage >= OR_PROGRESS_COUNT ||
        weather < OR_WEATHER_CLEAR || weather >= OR_WEATHER_COUNT) return false;
    or_rules_clear(out_snapshot);
    out_snapshot->progress = stage;
    out_snapshot->terrain = terrain;
    out_snapshot->weather = weather;
    out_snapshot->is_night = is_night;

    or_prng_seed(&rng, world_rule_seed ^ UINT64_C(0x4f524947494e5255));
    target = 2u + (size_t)(or_prng_next_u64(&rng) % 3u);
    while (out_snapshot->selected_count < target && attempts++ < 128u) {
        uint32_t candidate = (uint32_t)(or_prng_next_u64(&rng) % OR_RULE_COUNT);
        const OR_RuleDefinition *definition = &g_rule_definitions[candidate];
        size_t i;
        bool conflicts = false;

        if (or_rule_already_selected(out_snapshot, candidate)) continue;
        for (i = 0; i < out_snapshot->selected_count; ++i) {
            uint32_t selected = out_snapshot->selected_ids[i];
            if ((definition->conflicts & (1u << selected)) != 0u ||
                (g_rule_definitions[selected].conflicts & (1u << candidate)) != 0u) {
                conflicts = true;
                break;
            }
        }
        if (conflicts) continue;
        out_snapshot->selected_ids[out_snapshot->selected_count++] = candidate;
        if (or_rule_condition_matches(definition, out_snapshot)) {
            out_snapshot->active_mask |= (1u << candidate);
        }
    }

    if (out_snapshot->selected_count < 2u) {
        uint32_t candidate;
        for (candidate = 0; candidate < OR_RULE_COUNT && out_snapshot->selected_count < 2u; ++candidate) {
            if (!or_rule_already_selected(out_snapshot, candidate)) {
                out_snapshot->selected_ids[out_snapshot->selected_count++] = candidate;
                if (or_rule_condition_matches(&g_rule_definitions[candidate], out_snapshot)) {
                    out_snapshot->active_mask |= (1u << candidate);
                }
            }
        }
    }
    or_rules_finalize(config, out_snapshot);
    return out_snapshot->selected_count >= 2u && out_snapshot->selected_count <= OR_MAX_WORLD_RULES;
}

bool or_rules_rebind_snapshot(const OR_Config *config,
                              const OR_RuleSnapshot *saved_world_snapshot,
                              OR_ProgressStage progress,
                              OR_TerrainSnapshot terrain,
                              OR_Weather weather,
                              bool is_night,
                              OR_RuleSnapshot *out_snapshot) {
    size_t i;
    uint32_t selected_mask = 0u;
    if (!saved_world_snapshot || !out_snapshot ||
        saved_world_snapshot->selected_count < 2u ||
        saved_world_snapshot->selected_count > OR_MAX_WORLD_RULES ||
        progress < OR_PROGRESS_PRE_HARDMODE || progress >= OR_PROGRESS_COUNT ||
        weather < OR_WEATHER_CLEAR || weather >= OR_WEATHER_COUNT) return false;

    or_rules_clear(out_snapshot);
    out_snapshot->selected_count = saved_world_snapshot->selected_count;
    memcpy(out_snapshot->selected_ids, saved_world_snapshot->selected_ids,
           sizeof(out_snapshot->selected_ids));
    out_snapshot->progress = progress;
    out_snapshot->terrain = terrain;
    out_snapshot->weather = weather;
    out_snapshot->is_night = is_night;
    for (i = 0; i < out_snapshot->selected_count; ++i) {
        size_t j;
        uint32_t id = out_snapshot->selected_ids[i];
        if (id >= OR_RULE_COUNT) return false;
        selected_mask |= 1u << id;
        for (j = i + 1u; j < out_snapshot->selected_count; ++j) {
            if (id == out_snapshot->selected_ids[j]) return false;
        }
        if (or_rule_condition_matches(&g_rule_definitions[id], out_snapshot)) {
            out_snapshot->active_mask |= 1u << id;
        }
    }
    if ((saved_world_snapshot->active_mask & ~selected_mask) != 0u) return false;
    or_rules_finalize(config, out_snapshot);
    return true;
}

void or_rules_finalize(const OR_Config *config, OR_RuleSnapshot *snapshot) {
    float min_multiplier = 0.75f;
    float max_multiplier = 1.25f;
    float max_reward_multiplier = 1.25f;
    int32_t max_defense_flat = 4;
    float max_chance = 2.0f;
    float max_movement = 1.50f;
    uint32_t selected_ids[OR_MAX_WORLD_RULES];
    uint32_t active_mask;
    size_t selected_count;
    OR_ProgressStage progress;
    OR_TerrainSnapshot terrain;
    OR_Weather weather;
    bool is_night;
    size_t i;

    if (!snapshot) return;
    selected_count = snapshot->selected_count > OR_MAX_WORLD_RULES
        ? OR_MAX_WORLD_RULES : snapshot->selected_count;
    memcpy(selected_ids, snapshot->selected_ids, sizeof(selected_ids));
    active_mask = snapshot->active_mask;
    progress = snapshot->progress;
    terrain = snapshot->terrain;
    weather = snapshot->weather;
    is_night = snapshot->is_night;
    if (config) {
        min_multiplier = config->caps.rule_life_damage_defense_min;
        max_multiplier = config->caps.rule_life_damage_defense_max;
        max_reward_multiplier = config->caps.rule_reward_multiplier_max;
        max_defense_flat = config->caps.rule_defense_flat_max;
        max_chance = config->caps.rule_chance_max;
    }
    if (min_multiplier > max_multiplier) {
        min_multiplier = 0.75f;
        max_multiplier = 1.25f;
    }

    or_rules_clear(snapshot);
    snapshot->selected_count = selected_count;
    memcpy(snapshot->selected_ids, selected_ids, sizeof(selected_ids));
    snapshot->active_mask = active_mask;
    snapshot->progress = progress;
    snapshot->terrain = terrain;
    snapshot->weather = weather;
    snapshot->is_night = is_night;

    for (i = 0; i < selected_count; ++i) {
        uint32_t id = snapshot->selected_ids[i];
        const OR_RuleDefinition *definition;
        size_t tier;
        if (id >= OR_RULE_COUNT || (active_mask & (1u << id)) == 0u) continue;
        definition = &g_rule_definitions[id];
        snapshot->life_multiplier *= definition->life_multiplier;
        snapshot->damage_multiplier *= definition->damage_multiplier;
        snapshot->defense_multiplier *= definition->defense_multiplier;
        snapshot->defense_flat += definition->defense_flat;
        snapshot->elite_chance_multiplier *= definition->probability_multiplier;
        snapshot->reward_chance_bonus += definition->reward_chance_bonus;
        snapshot->reward_quality_multiplier *= definition->reward_quality_multiplier;
        snapshot->movement_multiplier *= definition->movement_multiplier;
        if (definition->preferred_element != OR_ELEMENT_NONE) {
            snapshot->preferred_element = definition->preferred_element;
        }
        for (tier = 0; tier < OR_TIER_COUNT; ++tier) {
            snapshot->tier_weight_multiplier[tier] *= definition->tier_weight_multiplier[tier];
        }
    }
    or_apply_context_modifiers(snapshot);

    if (snapshot->life_multiplier < min_multiplier) snapshot->life_multiplier = min_multiplier;
    if (snapshot->life_multiplier > max_multiplier) snapshot->life_multiplier = max_multiplier;
    if (snapshot->damage_multiplier < min_multiplier) snapshot->damage_multiplier = min_multiplier;
    if (snapshot->damage_multiplier > max_multiplier) snapshot->damage_multiplier = max_multiplier;
    if (snapshot->defense_multiplier < min_multiplier) snapshot->defense_multiplier = min_multiplier;
    if (snapshot->defense_multiplier > max_multiplier) snapshot->defense_multiplier = max_multiplier;
    if (snapshot->defense_flat > max_defense_flat) snapshot->defense_flat = max_defense_flat;
    if (snapshot->defense_flat < -max_defense_flat) snapshot->defense_flat = -max_defense_flat;
    if (snapshot->money_multiplier < 0.0f) snapshot->money_multiplier = 0.0f;
    if (snapshot->money_multiplier > max_reward_multiplier) snapshot->money_multiplier = max_reward_multiplier;
    if (snapshot->reward_chance_bonus < 0.0f) snapshot->reward_chance_bonus = 0.0f;
    if (snapshot->reward_chance_bonus > 0.40f) snapshot->reward_chance_bonus = 0.40f;
    if (snapshot->reward_quality_multiplier < 0.0f) snapshot->reward_quality_multiplier = 0.0f;
    if (snapshot->reward_quality_multiplier > max_reward_multiplier) {
        snapshot->reward_quality_multiplier = max_reward_multiplier;
    }
    if (snapshot->elite_chance_multiplier < 0.0f) snapshot->elite_chance_multiplier = 0.0f;
    if (snapshot->elite_chance_multiplier > max_chance) snapshot->elite_chance_multiplier = max_chance;
    if (snapshot->movement_multiplier < 1.0f) snapshot->movement_multiplier = 1.0f;
    if (snapshot->movement_multiplier > max_movement) snapshot->movement_multiplier = max_movement;
    for (i = 0; i < OR_TIER_COUNT; ++i) {
        if (snapshot->tier_weight_multiplier[i] < 0.50f) snapshot->tier_weight_multiplier[i] = 0.50f;
        if (snapshot->tier_weight_multiplier[i] > 2.00f) snapshot->tier_weight_multiplier[i] = 2.00f;
    }
}

const char *or_world_rule_name(uint32_t rule_id) {
    return rule_id < OR_RULE_COUNT ? g_rule_definitions[rule_id].name : "unknown";
}
