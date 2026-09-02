#ifndef ORIGINREWRITE_STATE_H
#define ORIGINREWRITE_STATE_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OR_EliteRecord {
    bool occupied;
    OR_EliteLifecycle lifecycle;
    OR_InstanceKey key;
    uint32_t npc_type;
    OR_SpawnSource source;
    uint64_t spawn_tick;
    OR_ProgressStage progress;
    OR_GameMode mode;
    OR_EliteTier tier;
    OR_VanillaStats vanilla;
    OR_FinalStats final_stats;
    OR_RuleSnapshot rules;
    OR_AiPlan ai_plan;
    bool loot_committed;
} OR_EliteRecord;

typedef struct OR_StateStore {
    OR_EliteRecord records[OR_MAX_TRACKED_NPCS];
    uint64_t next_generation[OR_MAX_TRACKED_NPCS];
    uint64_t last_spawn_session[OR_MAX_TRACKED_NPCS];
    uint64_t last_spawn_tick[OR_MAX_TRACKED_NPCS];
    uint32_t active_elites;
} OR_StateStore;

void or_state_store_init(OR_StateStore *store);
bool or_state_spawn_on_cooldown(const OR_StateStore *store,
                                uint64_t world_session_id,
                                int32_t npc_slot,
                                uint64_t spawn_tick,
                                uint64_t cooldown_ticks);
void or_state_note_spawn(OR_StateStore *store, OR_InstanceKey key);
bool or_state_acquire_pending(OR_StateStore *store,
                               uint64_t world_session_id,
                               int32_t npc_slot,
                               uint32_t npc_type,
                               OR_SpawnSource source,
                               uint64_t spawn_tick,
                               OR_InstanceKey *out_key);
OR_EliteRecord *or_state_find(OR_StateStore *store, OR_InstanceKey key);
const OR_EliteRecord *or_state_find_const(const OR_StateStore *store,
                                          OR_InstanceKey key);
bool or_state_commit_spawn(OR_StateStore *store,
                           OR_InstanceKey key,
                           OR_ProgressStage progress,
                           OR_GameMode mode,
                           OR_EliteTier tier,
                           const OR_VanillaStats *vanilla,
                           const OR_FinalStats *final_stats,
                           const OR_RuleSnapshot *rules,
                           const OR_AiPlan *ai_plan);
bool or_state_mark_live(OR_StateStore *store, OR_InstanceKey key);
bool or_state_mark_death(OR_StateStore *store, OR_InstanceKey key);
bool or_state_claim_loot(OR_StateStore *store, OR_InstanceKey key);
bool or_state_cleanup(OR_StateStore *store, OR_InstanceKey key);
uint32_t or_state_active_count(const OR_StateStore *store);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_STATE_H */
