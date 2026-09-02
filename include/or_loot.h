#ifndef ORIGINREWRITE_LOOT_H
#define ORIGINREWRITE_LOOT_H

#include "or_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OR_LootKind {
    OR_LOOT_NONE = 0,
    OR_LOOT_VANILLA_MATERIAL_POOL,
    OR_LOOT_VANILLA_CONSUMABLE_POOL,
    OR_LOOT_VANILLA_EQUIPMENT_POOL,
    OR_LOOT_VANILLA_ACCESSORY_POOL,
    OR_LOOT_VANILLA_CRATE_POOL
} OR_LootKind;

typedef enum OR_MoneyPolicy {
    OR_MONEY_USE_FINAL_NPC_VALUE = 0,
    OR_MONEY_NO_EXTRA_GRANT
} OR_MoneyPolicy;

typedef struct OR_LootContext {
    bool host_authority;
    bool single_player;
    bool original_vanilla_loot_preserved;
    bool coin_backend_verified;
    OR_ProgressStage progress;
    OR_EliteTier tier;
    OR_TerrainSnapshot terrain_snapshot;
    float reward_chance_bonus;
    float reward_quality_multiplier;
    uint64_t random_seed;
} OR_LootContext;

typedef struct OR_LootResult {
    bool committed;
    bool extra_reward;
    uint8_t extra_reward_slots;
    OR_LootKind kind;
    const char *pool_id;
    uint8_t item_count;
    float reward_quality_multiplier;
    bool item_registry_required;
    OR_MoneyPolicy money_policy;
    bool coin_backend_required;
} OR_LootResult;

bool or_loot_build_policy(const OR_Config *config,
                          const OR_LootContext *context,
                          OR_LootResult *out_result);
bool or_loot_commit(OR_StateStore *store,
                    OR_InstanceKey key,
                    const OR_Config *config,
                    const OR_LootContext *context,
                    OR_LootResult *out_result);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_LOOT_H */
