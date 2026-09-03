#include "or_loot.h"

#include "or_prng.h"

#include <string.h>

static const char *or_environment_crate_pool(OR_TerrainSnapshot terrain) {
    /* Names are resolver keys; item_registry will map them to target-version IDs. */
    switch (terrain.special) {
        case OR_SPECIAL_DUNGEON: return "dungeon_crate_or_stockade_crate";
        case OR_SPECIAL_OCEAN: return "ocean_crate_or_seaside_crate";
        case OR_SPECIAL_SKY: return "sky_crate_or_azure_crate";
        case OR_SPECIAL_MUSHROOM: return NULL; /* no universal vanilla mapping */
        case OR_SPECIAL_NONE:
        default:
            break;
    }
    switch (terrain.biome) {
        case OR_BIOME_JUNGLE: return "jungle_crate_or_bramble_crate";
        case OR_BIOME_SNOW: return "frozen_crate_or_boreal_crate";
        case OR_BIOME_DESERT: return "oasis_crate_or_mirage_crate";
        case OR_BIOME_CORRUPTION: return "corrupt_crate_or_defiled_crate";
        case OR_BIOME_CRIMSON: return "crimson_crate_or_hematic_crate";
        case OR_BIOME_HALLOW: return "divine_crate_or_hallowed_crate";
        case OR_BIOME_FOREST:
        default:
            break;
    }
    if (terrain.depth == OR_DEPTH_UNDERWORLD) return "obsidian_crate_or_hellstone_crate";
    return NULL;
}

static float or_safe_quality(float quality) {
    return quality > 0.0f ? quality : 1.0f;
}

static void or_set_extra(OR_LootResult *result, OR_LootKind kind, const char *pool_id) {
    if (!result || !pool_id) return;
    result->extra_reward = true;
    result->extra_reward_slots = 1u;
    result->item_count = 1u;
    result->item_registry_required = true;
    result->kind = kind;
    result->pool_id = pool_id;
}

bool or_loot_build_policy(const OR_Config *config,
                          const OR_LootContext *context,
                          OR_LootResult *out_result) {
    OR_Prng rng;
    float reward_chance;
    float quality;

    if (!out_result) return false;
    memset(out_result, 0, sizeof(*out_result));
    out_result->money_policy = OR_MONEY_NO_EXTRA_GRANT;
    out_result->reward_quality_multiplier = 1.0f;
    if (!config || !context || !context->original_vanilla_loot_preserved ||
        context->progress < OR_PROGRESS_PRE_HARDMODE || context->progress >= OR_PROGRESS_COUNT ||
        context->tier <= OR_TIER_NONE || context->tier >= OR_TIER_COUNT) return false;
    if (!context->host_authority && !context->single_player) return false;

    quality = or_safe_quality(context->reward_quality_multiplier);
    if (quality > config->caps.rule_reward_multiplier_max) quality = config->caps.rule_reward_multiplier_max;
    out_result->reward_quality_multiplier = quality;
    or_prng_seed(&rng, context->random_seed ^ UINT64_C(0x4c4f4f545f763031));

    if (context->tier == OR_TIER_ALTERED) {
        reward_chance = config->loot.altered_extra_reward_chance + context->reward_chance_bonus;
        if (reward_chance > 0.60f) reward_chance = 0.60f;
        if (or_prng_chance(&rng, reward_chance)) {
            or_set_extra(out_result, OR_LOOT_VANILLA_MATERIAL_POOL,
                         "current_stage_vanilla_material_potion_or_ammo_pool");
        }
    } else if (context->tier == OR_TIER_CALAMITY) {
        float weights[3] = {0.70f, 0.20f, 0.10f};
        size_t branch;
        /* Quality changes branch weights inside the one slot, never slot count. */
        if (quality > 1.0f) {
            weights[2] += (quality - 1.0f) * 0.10f;
            weights[0] -= (quality - 1.0f) * 0.10f;
        }
        branch = or_prng_weighted_index(&rng, weights, 3u);
        if (branch == 0u) {
            or_set_extra(out_result, OR_LOOT_VANILLA_MATERIAL_POOL,
                         "current_stage_vanilla_material_or_ore_pool");
        } else if (branch == 1u) {
            or_set_extra(out_result, OR_LOOT_VANILLA_CONSUMABLE_POOL,
                         "current_stage_vanilla_potion_or_ammo_pool");
        } else {
            or_set_extra(out_result, OR_LOOT_VANILLA_EQUIPMENT_POOL,
                         "current_stage_vanilla_legal_equipment_or_accessory_pool");
        }
    } else {
        const char *crate_pool = or_environment_crate_pool(context->terrain_snapshot);
        bool choose_crate = false;
        if (config->loot.allow_vanilla_crates) {
            float crate_chance = context->progress == OR_PROGRESS_PRE_HARDMODE ? 0.20f
                : context->progress <= OR_PROGRESS_PRE_PLANTERA ? 0.40f
                : context->progress == OR_PROGRESS_POST_PLANTERA ? 0.25f : 0.15f;
            choose_crate = crate_pool && or_prng_chance(&rng, crate_chance);
        }
        if (choose_crate) {
            or_set_extra(out_result, OR_LOOT_VANILLA_CRATE_POOL, crate_pool);
        } else if (context->progress == OR_PROGRESS_ENDGAME) {
            or_set_extra(out_result, OR_LOOT_VANILLA_MATERIAL_POOL,
                         "endgame_vanilla_fragment_material_or_accessory_pool");
        } else if (context->progress == OR_PROGRESS_POST_PLANTERA) {
            or_set_extra(out_result, OR_LOOT_VANILLA_EQUIPMENT_POOL,
                         "post_plantera_vanilla_legal_equipment_or_accessory_pool");
        } else {
            or_set_extra(out_result, OR_LOOT_VANILLA_MATERIAL_POOL,
                         "current_stage_vanilla_material_or_equipment_pool");
        }
    }

    /* The single money backend is the NPC value field, when the native hook is verified. */
    out_result->money_policy = context->coin_backend_verified
        ? OR_MONEY_USE_FINAL_NPC_VALUE : OR_MONEY_NO_EXTRA_GRANT;
    return true;
}

bool or_loot_commit(OR_StateStore *store,
                    OR_InstanceKey key,
                    const OR_Config *config,
                    const OR_LootContext *context,
                    OR_LootResult *out_result) {
    OR_LootContext effective_context;
    OR_LootResult policy;
    const OR_EliteRecord *record;

    if (!store || !out_result || !config || !context) return false;
    record = or_state_find_const(store, key);
    if (!record) return false;
    effective_context = *context;
    /* The state snapshot is authoritative; callers cannot redirect a reward at death time. */
    effective_context.progress = record->progress;
    effective_context.tier = record->tier;
    effective_context.terrain_snapshot = record->rules.terrain;
    effective_context.reward_chance_bonus = record->rules.reward_chance_bonus;
    effective_context.reward_quality_multiplier = record->rules.reward_quality_multiplier;
    if (!or_loot_build_policy(config, &effective_context, &policy)) return false;
    if (!or_state_claim_loot(store, key)) return false;
    policy.committed = true;
    *out_result = policy;
    return true;
}
