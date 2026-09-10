#include "or_loot.h"

#include "or_prng.h"

#include <stddef.h>
#include <string.h>

/* Item IDs and stack ranges are copied from EliteMonsters 1.3.2. */
typedef struct OR_RewardEntry {
    int32_t item_type;
    int32_t min_stack;
    int32_t max_stack;
    const char *name;
    OR_LootKind kind;
} OR_RewardEntry;

#define OR_ITEM_GEL 23
#define OR_ITEM_WOODEN_ARROW 40
#define OR_ITEM_MUSKET_BALL 97
#define OR_ITEM_GOLDEN_CRATE 2336
#define OR_ITEM_TITANIUM_CRATE 3981
#define OR_ITEM_FALLEN_STAR 75
#define OR_ITEM_MAGIC_MIRROR 50
#define OR_ITEM_HERMES_BOOTS 54
#define OR_ITEM_CLOUD_IN_A_BOTTLE 53
#define OR_ITEM_HOOK 118
#define OR_ITEM_COBALT_BAR 381
#define OR_ITEM_MYTHRIL_BAR 382
#define OR_ITEM_ADAMANTITE_BAR 391
#define OR_ITEM_DEMON_WINGS 492
#define OR_ITEM_ANGEL_WINGS 493
#define OR_ITEM_HEALING_POTION 188
#define OR_ITEM_GREATER_HEALING_POTION 499
#define OR_ITEM_GREATER_MANA_POTION 500
#define OR_ITEM_SOUL_OF_LIGHT 520
#define OR_ITEM_SOUL_OF_NIGHT 521
#define OR_ITEM_SOUL_OF_FRIGHT 547
#define OR_ITEM_SOUL_OF_MIGHT 548
#define OR_ITEM_SOUL_OF_SIGHT 549
#define OR_ITEM_CHLOROPHYTE_ORE 947
#define OR_ITEM_CHLOROPHYTE_BAR 1006
#define OR_ITEM_HALLOWED_BAR 1225
#define OR_ITEM_ECTOPLASM 1508
#define OR_ITEM_SHROOMITE_BAR 1552
#define OR_ITEM_BEETLE_HUSK 2218
#define OR_ITEM_SPECTRE_BAR 3261
#define OR_ITEM_LUNAR_BAR 3467

#define OR_ITEM_CORRUPT_CRATE 3203
#define OR_ITEM_CRIMSON_CRATE 3204
#define OR_ITEM_DUNGEON_CRATE 3205
#define OR_ITEM_SKY_CRATE 3206
#define OR_ITEM_HALLOWED_CRATE 3207
#define OR_ITEM_JUNGLE_CRATE 3208
#define OR_ITEM_CORRUPT_CRATE_HARD 3982
#define OR_ITEM_CRIMSON_CRATE_HARD 3983
#define OR_ITEM_DUNGEON_CRATE_HARD 3984
#define OR_ITEM_SKY_CRATE_HARD 3985
#define OR_ITEM_HALLOWED_CRATE_HARD 3986
#define OR_ITEM_JUNGLE_CRATE_HARD 3987
#define OR_ITEM_FROZEN_CRATE 4405
#define OR_ITEM_FROZEN_CRATE_HARD 4406
#define OR_ITEM_OASIS_CRATE 4407
#define OR_ITEM_OASIS_CRATE_HARD 4408
#define OR_ITEM_LAVA_CRATE 4877
#define OR_ITEM_LAVA_CRATE_HARD 4878
#define OR_ITEM_OCEAN_CRATE 5002
#define OR_ITEM_OCEAN_CRATE_HARD 5003

#define OR_PROGRESS_REWARD_POOL_SIZE 8u
#define OR_MATERIAL OR_LOOT_VANILLA_MATERIAL_POOL
#define OR_CONSUMABLE OR_LOOT_VANILLA_CONSUMABLE_POOL
#define OR_EQUIPMENT OR_LOOT_VANILLA_EQUIPMENT_POOL

static const OR_RewardEntry g_progress_reward_pools[OR_PROGRESS_COUNT][OR_PROGRESS_REWARD_POOL_SIZE] = {
    {
        {OR_ITEM_GEL, 15, 30, "Gel", OR_MATERIAL},
        {OR_ITEM_FALLEN_STAR, 3, 5, "FallenStar", OR_MATERIAL},
        {OR_ITEM_WOODEN_ARROW, 20, 50, "WoodenArrow", OR_CONSUMABLE},
        {OR_ITEM_HEALING_POTION, 2, 4, "HealingPotion", OR_CONSUMABLE},
        {OR_ITEM_MAGIC_MIRROR, 1, 1, "MagicMirror", OR_EQUIPMENT},
        {OR_ITEM_HERMES_BOOTS, 1, 1, "HermesBoots", OR_EQUIPMENT},
        {OR_ITEM_CLOUD_IN_A_BOTTLE, 1, 1, "CloudInABottle", OR_EQUIPMENT},
        {OR_ITEM_HOOK, 1, 1, "Hook", OR_EQUIPMENT}
    },
    {
        {OR_ITEM_COBALT_BAR, 2, 4, "CobaltBar", OR_MATERIAL},
        {OR_ITEM_MYTHRIL_BAR, 2, 4, "MythrilBar", OR_MATERIAL},
        {OR_ITEM_ADAMANTITE_BAR, 2, 4, "AdamantiteBar", OR_MATERIAL},
        {OR_ITEM_SOUL_OF_LIGHT, 3, 6, "SoulOfLight", OR_MATERIAL},
        {OR_ITEM_SOUL_OF_NIGHT, 3, 6, "SoulOfNight", OR_MATERIAL},
        {OR_ITEM_MUSKET_BALL, 20, 60, "MusketBall", OR_CONSUMABLE},
        {OR_ITEM_DEMON_WINGS, 1, 1, "DemonWings", OR_EQUIPMENT},
        {OR_ITEM_ANGEL_WINGS, 1, 1, "AngelWings", OR_EQUIPMENT}
    },
    {
        {OR_ITEM_HALLOWED_BAR, 2, 4, "HallowedBar", OR_MATERIAL},
        {OR_ITEM_CHLOROPHYTE_ORE, 8, 16, "ChlorophyteOre", OR_MATERIAL},
        {OR_ITEM_CHLOROPHYTE_BAR, 2, 4, "ChlorophyteBar", OR_MATERIAL},
        {OR_ITEM_SOUL_OF_FRIGHT, 3, 6, "SoulOfFright", OR_MATERIAL},
        {OR_ITEM_SOUL_OF_MIGHT, 3, 6, "SoulOfMight", OR_MATERIAL},
        {OR_ITEM_SOUL_OF_SIGHT, 3, 6, "SoulOfSight", OR_MATERIAL},
        {OR_ITEM_GREATER_MANA_POTION, 3, 6, "GreaterManaPotion", OR_CONSUMABLE},
        {OR_ITEM_HOOK, 1, 1, "Hook", OR_EQUIPMENT}
    },
    {
        {OR_ITEM_ECTOPLASM, 2, 4, "Ectoplasm", OR_MATERIAL},
        {OR_ITEM_SPECTRE_BAR, 2, 4, "SpectreBar", OR_MATERIAL},
        {OR_ITEM_SHROOMITE_BAR, 2, 4, "ShroomiteBar", OR_MATERIAL},
        {OR_ITEM_CHLOROPHYTE_BAR, 3, 6, "ChlorophyteBar", OR_MATERIAL},
        {OR_ITEM_GREATER_HEALING_POTION, 4, 8, "GreaterHealingPotion", OR_CONSUMABLE},
        {OR_ITEM_GREATER_MANA_POTION, 4, 8, "GreaterManaPotion", OR_CONSUMABLE},
        {OR_ITEM_HOOK, 1, 1, "Hook", OR_EQUIPMENT},
        {OR_ITEM_MAGIC_MIRROR, 1, 1, "MagicMirror", OR_EQUIPMENT}
    },
    {
        {OR_ITEM_LUNAR_BAR, 2, 5, "LunarBar", OR_MATERIAL},
        {OR_ITEM_ECTOPLASM, 3, 6, "Ectoplasm", OR_MATERIAL},
        {OR_ITEM_BEETLE_HUSK, 2, 4, "BeetleHusk", OR_MATERIAL},
        {OR_ITEM_SHROOMITE_BAR, 2, 5, "ShroomiteBar", OR_MATERIAL},
        {OR_ITEM_CHLOROPHYTE_BAR, 4, 8, "ChlorophyteBar", OR_MATERIAL},
        {OR_ITEM_GREATER_HEALING_POTION, 5, 10, "GreaterHealingPotion", OR_CONSUMABLE},
        {OR_ITEM_GREATER_MANA_POTION, 5, 10, "GreaterManaPotion", OR_CONSUMABLE},
        {OR_ITEM_MAGIC_MIRROR, 1, 1, "MagicMirror", OR_EQUIPMENT}
    }
};

static const int32_t g_pre_hardmode_crates[] = {
    OR_ITEM_CORRUPT_CRATE, OR_ITEM_CRIMSON_CRATE, OR_ITEM_DUNGEON_CRATE,
    OR_ITEM_SKY_CRATE, OR_ITEM_JUNGLE_CRATE, OR_ITEM_FROZEN_CRATE,
    OR_ITEM_OASIS_CRATE, OR_ITEM_LAVA_CRATE, OR_ITEM_OCEAN_CRATE
};

static const int32_t g_hardmode_crates[] = {
    OR_ITEM_CORRUPT_CRATE_HARD, OR_ITEM_CRIMSON_CRATE_HARD,
    OR_ITEM_DUNGEON_CRATE_HARD, OR_ITEM_SKY_CRATE_HARD,
    OR_ITEM_HALLOWED_CRATE_HARD, OR_ITEM_JUNGLE_CRATE_HARD,
    OR_ITEM_FROZEN_CRATE_HARD, OR_ITEM_OASIS_CRATE_HARD,
    OR_ITEM_LAVA_CRATE_HARD, OR_ITEM_OCEAN_CRATE_HARD
};

static int32_t or_random_range(OR_Prng *rng, int32_t min_value, int32_t max_value) {
    uint64_t span;
    if (min_value < 1) min_value = 1;
    if (max_value < min_value) max_value = min_value;
    span = (uint64_t)(max_value - min_value + 1);
    return min_value + (int32_t)(or_prng_next_u64(rng) % span);
}

static OR_ProgressStage or_valid_progress(OR_ProgressStage progress) {
    return progress >= OR_PROGRESS_PRE_HARDMODE && progress < OR_PROGRESS_COUNT
        ? progress : OR_PROGRESS_PRE_HARDMODE;
}

static bool or_select_progress_item(OR_Prng *rng, OR_ProgressStage progress,
                                    OR_LootKind kind, bool large_material,
                                    int32_t *item_type, int32_t *item_stack,
                                    const char **item_name) {
    const OR_RewardEntry *pool;
    size_t candidates[OR_PROGRESS_REWARD_POOL_SIZE];
    size_t candidate_count = 0u;
    size_t i;
    const OR_RewardEntry *entry;
    int32_t stack;
    if (!rng || !item_type || !item_stack || !item_name) return false;
    progress = or_valid_progress(progress);
    pool = g_progress_reward_pools[progress];
    for (i = 0u; i < OR_PROGRESS_REWARD_POOL_SIZE; ++i) {
        if (kind == OR_LOOT_NONE || pool[i].kind == kind) candidates[candidate_count++] = i;
    }
    if (candidate_count == 0u) return false;
    entry = &pool[candidates[or_prng_next_u64(rng) % candidate_count]];
    stack = or_random_range(rng, entry->min_stack, entry->max_stack);
    if (large_material && entry->kind == OR_MATERIAL && stack <= 16) stack *= 2;
    *item_type = entry->item_type;
    *item_stack = stack;
    *item_name = entry->name;
    return true;
}

static int32_t or_environment_crate(OR_ProgressStage progress,
                                    OR_TerrainSnapshot terrain,
                                    OR_Prng *rng) {
    bool hard = progress != OR_PROGRESS_PRE_HARDMODE;
    size_t count = hard ? sizeof(g_hardmode_crates) / sizeof(g_hardmode_crates[0])
                        : sizeof(g_pre_hardmode_crates) / sizeof(g_pre_hardmode_crates[0]);
    const int32_t *fallback = hard ? g_hardmode_crates : g_pre_hardmode_crates;
    int32_t normal = 0;
    int32_t hard_id = 0;
    switch (terrain.special) {
        case OR_SPECIAL_DUNGEON: normal = OR_ITEM_DUNGEON_CRATE; hard_id = OR_ITEM_DUNGEON_CRATE_HARD; break;
        case OR_SPECIAL_OCEAN: normal = OR_ITEM_OCEAN_CRATE; hard_id = OR_ITEM_OCEAN_CRATE_HARD; break;
        case OR_SPECIAL_SKY: normal = OR_ITEM_SKY_CRATE; hard_id = OR_ITEM_SKY_CRATE_HARD; break;
        default: break;
    }
    if (normal == 0) {
        switch (terrain.biome) {
            case OR_BIOME_CORRUPTION: normal = OR_ITEM_CORRUPT_CRATE; hard_id = OR_ITEM_CORRUPT_CRATE_HARD; break;
            case OR_BIOME_CRIMSON: normal = OR_ITEM_CRIMSON_CRATE; hard_id = OR_ITEM_CRIMSON_CRATE_HARD; break;
            case OR_BIOME_HALLOW: normal = OR_ITEM_HALLOWED_CRATE; hard_id = OR_ITEM_HALLOWED_CRATE_HARD; break;
            case OR_BIOME_JUNGLE: normal = OR_ITEM_JUNGLE_CRATE; hard_id = OR_ITEM_JUNGLE_CRATE_HARD; break;
            case OR_BIOME_SNOW: normal = OR_ITEM_FROZEN_CRATE; hard_id = OR_ITEM_FROZEN_CRATE_HARD; break;
            case OR_BIOME_DESERT: normal = OR_ITEM_OASIS_CRATE; hard_id = OR_ITEM_OASIS_CRATE_HARD; break;
            default: break;
        }
    }
    if (normal == 0 && terrain.depth == OR_DEPTH_UNDERWORLD) {
        normal = OR_ITEM_LAVA_CRATE;
        hard_id = OR_ITEM_LAVA_CRATE_HARD;
    }
    if (normal != 0) return hard ? hard_id : normal;
    return fallback[or_prng_next_u64(rng) % count];
}

static float or_safe_quality(float quality) {
    return quality > 0.0f ? quality : 1.0f;
}

static void or_set_extra(OR_LootResult *result, OR_LootKind kind, const char *pool_id,
                         int32_t item_type, int32_t item_stack, const char *item_name) {
    if (!result || !pool_id || item_type <= 0 || item_stack <= 0) return;
    result->extra_reward = true;
    result->extra_reward_slots = 1u;
    result->item_count = 1u;
    result->item_registry_required = false;
    result->kind = kind;
    result->pool_id = pool_id;
    result->item_type = item_type;
    result->item_stack = item_stack;
    result->item_name = item_name;
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
            int32_t item_type = 0;
            int32_t item_stack = 0;
            const char *item_name = NULL;
            OR_LootKind kind = or_prng_chance(&rng, 0.35f)
                ? OR_LOOT_VANILLA_CONSUMABLE_POOL : OR_LOOT_VANILLA_MATERIAL_POOL;
            if (or_select_progress_item(&rng, context->progress, kind,
                                         false, &item_type, &item_stack, &item_name)) {
                or_set_extra(out_result, kind,
                             kind == OR_LOOT_VANILLA_CONSUMABLE_POOL
                                 ? "current_stage_vanilla_potion_or_ammo_pool"
                                 : "current_stage_vanilla_material_pool",
                             item_type, item_stack, item_name);
            }
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
        {
            OR_LootKind kind = branch == 0u ? OR_LOOT_VANILLA_MATERIAL_POOL
                             : branch == 1u ? OR_LOOT_VANILLA_CONSUMABLE_POOL
                                            : OR_LOOT_VANILLA_EQUIPMENT_POOL;
            int32_t item_type = 0;
            int32_t item_stack = 0;
            const char *item_name = NULL;
            if (or_select_progress_item(&rng, context->progress, kind, false,
                                        &item_type, &item_stack, &item_name)) {
                or_set_extra(out_result, kind,
                             kind == OR_LOOT_VANILLA_MATERIAL_POOL
                                 ? "current_stage_vanilla_material_or_ore_pool"
                                 : kind == OR_LOOT_VANILLA_CONSUMABLE_POOL
                                     ? "current_stage_vanilla_potion_or_ammo_pool"
                                     : "current_stage_vanilla_legal_equipment_or_accessory_pool",
                             item_type, item_stack, item_name);
            }
        }
    } else {
        float crate_chance = context->progress == OR_PROGRESS_PRE_HARDMODE ? 0.20f
            : context->progress <= OR_PROGRESS_PRE_PLANTERA ? 0.40f
            : context->progress == OR_PROGRESS_POST_PLANTERA ? 0.25f : 0.15f;
        bool choose_crate = config->loot.allow_vanilla_crates &&
                            or_prng_chance(&rng, crate_chance);
        if (choose_crate) {
            int32_t crate = or_environment_crate(context->progress,
                                                  context->terrain_snapshot, &rng);
            or_set_extra(out_result, OR_LOOT_VANILLA_CRATE_POOL,
                         "terrain_crate_by_spawn_snapshot", crate, 1,
                         context->progress == OR_PROGRESS_PRE_HARDMODE
                             ? "GoldenOrEnvironmentCrate" : "TitaniumOrEnvironmentCrate");
        } else {
            OR_LootKind kind = or_prng_chance(&rng, 0.50f)
                ? OR_LOOT_VANILLA_EQUIPMENT_POOL : OR_LOOT_VANILLA_MATERIAL_POOL;
            int32_t item_type = 0;
            int32_t item_stack = 0;
            const char *item_name = NULL;
            if (or_select_progress_item(&rng, context->progress, kind,
                                         kind == OR_LOOT_VANILLA_MATERIAL_POOL,
                                         &item_type, &item_stack, &item_name)) {
                or_set_extra(out_result, kind,
                             kind == OR_LOOT_VANILLA_EQUIPMENT_POOL
                                 ? "legal_vanilla_equipment_or_accessory_pool"
                                 : "large_current_stage_vanilla_material_pool",
                             item_type, item_stack, item_name);
            }
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
