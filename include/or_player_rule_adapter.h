#ifndef ORIGINREWRITE_PLAYER_RULE_ADAPTER_H
#define ORIGINREWRITE_PLAYER_RULE_ADAPTER_H
#include "or_types.h"
typedef struct OR_PlayerRuleAdapter {
    bool entered;
    OR_DepthTag last_depth;
    OR_DepthTag candidate_depth;
    uint8_t candidate_samples;
    OR_BiomeTag last_biome;
    OR_SpecialLocationTag last_special;
    OR_BiomeTag candidate_biome;
    OR_SpecialLocationTag candidate_special;
} OR_PlayerRuleAdapter;
void or_player_rule_adapter_init(OR_PlayerRuleAdapter *state);
bool or_player_rule_adapter_on_context(OR_PlayerRuleAdapter *state, OR_TerrainSnapshot terrain);
#endif
