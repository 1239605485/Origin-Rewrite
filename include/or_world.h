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

#define OR_WORLD_RULE_HISTORY_LIMIT 8u

typedef struct OR_WorldRuleHistoryEntry {
    uint64_t effective_day;
    uint64_t revision;
    OR_RuleSnapshot snapshot;
} OR_WorldRuleHistoryEntry;

typedef struct OR_WorldRuleState {
    uint32_t save_format_version;
    uint32_t rule_pool_version;
    uint64_t rule_seed;
    uint64_t config_hash;
    uint64_t world_seed_fingerprint;
    bool initialized;
    bool disabled_safe_mode;
    uint64_t refresh_count;
    uint64_t last_refresh_day;
    uint64_t next_refresh_day;
    uint32_t history_count;
    OR_WorldRuleHistoryEntry history[OR_WORLD_RULE_HISTORY_LIMIT];
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
OR_WorldRuleStateStatus or_world_rules_refresh(OR_WorldRuleState *state,
                                               const OR_Config *config,
                                               OR_ProgressStage progress,
                                               OR_TerrainSnapshot terrain,
                                               OR_Weather weather,
                                               bool is_night,
                                               uint64_t game_day);
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
