#ifndef ORIGINREWRITE_WORLD_H
#define ORIGINREWRITE_WORLD_H

#include "or_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OR_WorldRuleStateStatus {
    OR_WORLD_RULES_VALID = 0,
    OR_WORLD_RULES_DISABLED_SAFE_MODE,
    OR_WORLD_RULES_INVALID_INPUT
} OR_WorldRuleStateStatus;

typedef struct OR_WorldRuleState {
    uint32_t save_format_version;
    uint32_t rule_pool_version;
    uint64_t rule_seed;
    uint64_t config_hash;
    uint64_t world_seed_fingerprint;
    bool initialized;
    bool disabled_safe_mode;
    OR_RuleSnapshot snapshot;
} OR_WorldRuleState;

void or_world_rule_state_init(OR_WorldRuleState *state);
OR_WorldRuleStateStatus or_world_rules_create(OR_WorldRuleState *state,
                                              const OR_Config *config,
                                              OR_ProgressStage progress,
                                              OR_TerrainSnapshot terrain,
                                              OR_Weather weather,
                                              bool is_night,
                                              uint64_t rule_seed,
                                              uint64_t config_hash,
                                              uint64_t world_seed_fingerprint);
OR_WorldRuleStateStatus or_world_rules_restore(OR_WorldRuleState *state,
                                               const OR_Config *config,
                                               uint32_t expected_pool_version,
                                               uint32_t expected_save_format_version,
                                               uint64_t expected_config_hash,
                                               uint64_t expected_world_seed_fingerprint);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_WORLD_H */
