#include "or_player_rule_adapter.h"
#include <string.h>
void or_player_rule_adapter_init(OR_PlayerRuleAdapter *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->last_depth = OR_DEPTH_COUNT;
    state->candidate_depth = OR_DEPTH_COUNT;
    state->last_biome = OR_BIOME_COUNT;
    state->last_special = OR_SPECIAL_COUNT;
    state->candidate_biome = OR_BIOME_COUNT;
    state->candidate_special = OR_SPECIAL_COUNT;
}

bool or_player_rule_adapter_on_context(OR_PlayerRuleAdapter *state,
                                       OR_TerrainSnapshot terrain) {
    if (!state || terrain.depth >= OR_DEPTH_COUNT ||
        terrain.biome >= OR_BIOME_COUNT || terrain.special >= OR_SPECIAL_COUNT) return false;
    if (state->candidate_depth != terrain.depth ||
        state->candidate_biome != terrain.biome ||
        state->candidate_special != terrain.special) {
        state->candidate_depth = terrain.depth;
        state->candidate_biome = terrain.biome;
        state->candidate_special = terrain.special;
        state->candidate_samples = 1u;
        return false;
    }
    if (state->candidate_samples < 3u) state->candidate_samples += 1u;
    if (state->candidate_samples < 2u) return false;
    if (!state->entered || state->last_depth != terrain.depth ||
        state->last_biome != terrain.biome ||
        state->last_special != terrain.special) {
        state->entered = true;
        state->last_depth = terrain.depth;
        state->last_biome = terrain.biome;
        state->last_special = terrain.special;
        return true;
    }
    return false;
}
