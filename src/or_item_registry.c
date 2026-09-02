#include "or_item_registry.h"

#include <string.h>

static bool or_terrain_matches(const OR_ItemCandidate *candidate,
                               OR_TerrainSnapshot terrain) {
    if (!candidate->terrain_specific) return true;
    return candidate->terrain.depth == terrain.depth &&
           candidate->terrain.biome == terrain.biome &&
           candidate->terrain.special == terrain.special;
}

bool or_item_candidate_is_legal(const OR_ItemCandidate *candidate,
                                OR_ProgressStage progress,
                                OR_TerrainSnapshot terrain) {
    if (!candidate || candidate->item_id <= 0 || !candidate->pool_id ||
        progress < OR_PROGRESS_PRE_HARDMODE || progress >= OR_PROGRESS_COUNT ||
        candidate->minimum_progress > progress || !candidate->vanilla || !candidate->enabled ||
        candidate->future_content || candidate->boss_bag || candidate->boss_summon ||
        candidate->exclusive_boss_drop || candidate->key_progression) return false;
    return or_terrain_matches(candidate, terrain);
}

const OR_ItemCandidate *or_item_registry_pick(const OR_ItemRegistry *registry,
                                             const char *pool_id,
                                             OR_ProgressStage progress,
                                             OR_TerrainSnapshot terrain,
                                             OR_Prng *rng) {
    size_t i;
    size_t legal_count = 0;
    size_t selected;
    if (!registry || !registry->item_ids_verified || !registry->entries ||
        !pool_id || !rng) return NULL;
    for (i = 0; i < registry->count; ++i) {
        if (registry->entries[i].pool_id && strcmp(registry->entries[i].pool_id, pool_id) == 0 &&
            or_item_candidate_is_legal(&registry->entries[i], progress, terrain)) {
            ++legal_count;
        }
    }
    if (legal_count == 0u) return NULL;
    selected = (size_t)(or_prng_next_u64(rng) % legal_count);
    for (i = 0; i < registry->count; ++i) {
        if (registry->entries[i].pool_id && strcmp(registry->entries[i].pool_id, pool_id) == 0 &&
            or_item_candidate_is_legal(&registry->entries[i], progress, terrain)) {
            if (selected == 0u) return &registry->entries[i];
            --selected;
        }
    }
    return NULL;
}
