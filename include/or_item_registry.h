#ifndef ORIGINREWRITE_ITEM_REGISTRY_H
#define ORIGINREWRITE_ITEM_REGISTRY_H

#include "or_prng.h"
#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OR_ItemKind {
    OR_ITEM_MATERIAL = 0,
    OR_ITEM_CONSUMABLE,
    OR_ITEM_EQUIPMENT,
    OR_ITEM_ACCESSORY,
    OR_ITEM_CRATE
} OR_ItemKind;

typedef struct OR_ItemCandidate {
    int32_t item_id;
    const char *pool_id;
    OR_ItemKind kind;
    OR_ProgressStage minimum_progress;
    OR_TerrainSnapshot terrain;
    bool terrain_specific;
    bool vanilla;
    bool enabled;
    bool future_content;
    bool boss_bag;
    bool boss_summon;
    bool exclusive_boss_drop;
    bool key_progression;
} OR_ItemCandidate;

typedef struct OR_ItemRegistry {
    const OR_ItemCandidate *entries;
    size_t count;
    bool item_ids_verified;
} OR_ItemRegistry;

bool or_item_candidate_is_legal(const OR_ItemCandidate *candidate,
                                OR_ProgressStage progress,
                                OR_TerrainSnapshot terrain);
const OR_ItemCandidate *or_item_registry_pick(const OR_ItemRegistry *registry,
                                             const char *pool_id,
                                             OR_ProgressStage progress,
                                             OR_TerrainSnapshot terrain,
                                             OR_Prng *rng);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_ITEM_REGISTRY_H */
