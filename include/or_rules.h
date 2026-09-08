#ifndef ORIGINREWRITE_RULES_H
#define ORIGINREWRITE_RULES_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OR_WorldRuleId {
    OR_RULE_ELITE_TIDE = 0,
    OR_RULE_STRONG_WORLD,
    OR_RULE_HARVEST_CONTRACT,
    OR_RULE_STORM_BORDER,
    OR_RULE_NIGHT_LAW,
    OR_RULE_ABYSS_ECHO,
    OR_RULE_EVIL_INFECTION,
    OR_RULE_GLASS_CANNON,
    OR_RULE_COUNT
} OR_WorldRuleId;

void or_rules_clear(OR_RuleSnapshot *snapshot);
bool or_rules_build_snapshot(const OR_Config *config,
                             OR_ProgressStage stage,
                             OR_TerrainSnapshot terrain,
                             OR_Weather weather,
                             bool is_night,
                             uint64_t world_rule_seed,
                             OR_RuleSnapshot *out_snapshot);
bool or_rules_rebind_snapshot(const OR_Config *config,
                              const OR_RuleSnapshot *saved_world_snapshot,
                              OR_ProgressStage progress,
                              OR_TerrainSnapshot terrain,
                              OR_Weather weather,
                              bool is_night,
                              OR_RuleSnapshot *out_snapshot);
void or_rules_finalize(const OR_Config *config, OR_RuleSnapshot *snapshot);
const char *or_world_rule_name(uint32_t rule_id);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_RULES_H */
