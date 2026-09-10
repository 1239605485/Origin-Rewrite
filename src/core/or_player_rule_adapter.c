#include "or_player_rule_adapter.h"
#include <string.h>
void or_player_rule_adapter_init(OR_PlayerRuleAdapter *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->last_depth = OR_DEPTH_COUNT;
    state->candidate_depth = OR_DEPTH_COUNT;
}

bool or_player_rule_adapter_on_context(OR_PlayerRuleAdapter *state,
                                       OR_TerrainSnapshot terrain) {
    if (!state || terrain.depth >= OR_DEPTH_COUNT) return false;
    if (state->candidate_depth != terrain.depth) {
        state->candidate_depth = terrain.depth;
        state->candidate_samples = 1u;
        return false;
    }
    if (state->candidate_samples < 3u) state->candidate_samples += 1u;
    if (state->candidate_samples < 2u) return false;
    if (!state->entered || state->last_depth != terrain.depth) {
        state->entered = true;
        state->last_depth = terrain.depth;
        return true;
    }
    return false;
}
