#ifndef ORIGINREWRITE_SPAWN_H
#define ORIGINREWRITE_SPAWN_H

#include "or_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum OR_SpawnRejectReason {
    OR_SPAWN_ACCEPTED = 0,
    OR_SPAWN_REJECT_INVALID_INPUT,
    OR_SPAWN_REJECT_NOT_AUTHORITY,
    OR_SPAWN_REJECT_INELIGIBLE_SOURCE,
    OR_SPAWN_REJECT_INELIGIBLE_NPC,
    OR_SPAWN_REJECT_LIMIT,
    OR_SPAWN_REJECT_CHANCE,
    OR_SPAWN_REJECT_TIER_DISABLED,
    OR_SPAWN_REJECT_COOLDOWN,
    OR_SPAWN_REJECT_SLOT_BUSY,
    OR_SPAWN_REJECT_COMMIT_FAILED
} OR_SpawnRejectReason;

typedef struct OR_SpawnContext {
    uint64_t world_session_id;
    uint64_t world_rule_seed;
    bool has_saved_world_rules;
    OR_RuleSnapshot saved_world_rules;
    uint64_t spawn_tick;
    int32_t npc_slot;
    uint32_t npc_type;
    bool npc_active;
    bool host_authority;
    bool single_player;
    bool is_boss;
    bool is_town_npc;
    bool is_friendly;
    bool is_dummy;
    bool is_segment;
    OR_SpawnSource source;
    OR_ProgressStage progress;
    OR_GameMode mode;
    OR_TerrainSnapshot terrain;
    OR_Weather weather;
    bool is_night;
    OR_AiArchetype archetype;
    uint32_t max_active_elites;
    bool transient_prepare;
    OR_VanillaStats vanilla;
} OR_SpawnContext;

typedef struct OR_SpawnResult {
    bool committed;
    OR_SpawnRejectReason reason;
    float base_chance;
    float effective_chance;
    bool chance_passed;
    OR_InstanceKey key;
    OR_EliteTier tier;
    OR_EliteRecord record;
} OR_SpawnResult;

const char *or_spawn_reject_reason_name(OR_SpawnRejectReason reason);

bool or_spawn_try_commit(const OR_Config *config,
                         OR_StateStore *store,
                         const OR_SpawnContext *context,
                         uint64_t random_seed,
                         OR_SpawnResult *out_result);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_SPAWN_H */
