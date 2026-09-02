#include "or_state.h"

#include <string.h>

static bool or_key_valid(OR_InstanceKey key) {
    return key.world_session_id != 0 &&
           key.npc_slot >= 0 &&
           (uint32_t)key.npc_slot < OR_MAX_TRACKED_NPCS &&
           key.generation_id != 0;
}

static bool or_key_matches(const OR_EliteRecord *record, OR_InstanceKey key) {
    return record && record->occupied &&
           record->key.world_session_id == key.world_session_id &&
           record->key.npc_slot == key.npc_slot &&
           record->key.generation_id == key.generation_id;
}

void or_state_store_init(OR_StateStore *store) {
    if (!store) return;
    memset(store, 0, sizeof(*store));
}

bool or_state_spawn_on_cooldown(const OR_StateStore *store,
                                uint64_t world_session_id,
                                int32_t npc_slot,
                                uint64_t spawn_tick,
                                uint64_t cooldown_ticks) {
    uint64_t last_tick;
    if (!store || world_session_id == 0u || npc_slot < 0 ||
        (uint32_t)npc_slot >= OR_MAX_TRACKED_NPCS || cooldown_ticks == 0u) return false;
    if (store->last_spawn_session[npc_slot] != world_session_id) return false;
    last_tick = store->last_spawn_tick[npc_slot];
    if (spawn_tick < last_tick) return true; /* conservative across a counter wrap/reset */
    return spawn_tick - last_tick < cooldown_ticks;
}

void or_state_note_spawn(OR_StateStore *store, OR_InstanceKey key) {
    const OR_EliteRecord *record = or_state_find_const(store, key);
    if (!store || !record) return;
    store->last_spawn_session[key.npc_slot] = key.world_session_id;
    store->last_spawn_tick[key.npc_slot] = record->spawn_tick;
}

bool or_state_acquire_pending(OR_StateStore *store,
                               uint64_t world_session_id,
                               int32_t npc_slot,
                               uint32_t npc_type,
                               OR_SpawnSource source,
                               uint64_t spawn_tick,
                               OR_InstanceKey *out_key) {
    OR_EliteRecord *record;
    uint64_t generation;

    if (!store || !out_key || world_session_id == 0 || npc_slot < 0 ||
        (uint32_t)npc_slot >= OR_MAX_TRACKED_NPCS) return false;
    record = &store->records[npc_slot];
    if (record->occupied) return false;

    generation = store->next_generation[npc_slot] + 1u;
    if (generation == 0) generation = 1u;
    store->next_generation[npc_slot] = generation;
    memset(record, 0, sizeof(*record));
    record->occupied = true;
    record->lifecycle = OR_LIFECYCLE_PENDING_INIT;
    record->key = (OR_InstanceKey){world_session_id, npc_slot, generation};
    record->npc_type = npc_type;
    record->source = source;
    record->spawn_tick = spawn_tick;
    *out_key = record->key;
    return true;
}

OR_EliteRecord *or_state_find(OR_StateStore *store, OR_InstanceKey key) {
    if (!store || !or_key_valid(key)) return NULL;
    if (!or_key_matches(&store->records[key.npc_slot], key)) return NULL;
    return &store->records[key.npc_slot];
}

const OR_EliteRecord *or_state_find_const(const OR_StateStore *store,
                                          OR_InstanceKey key) {
    if (!store || !or_key_valid(key)) return NULL;
    if (!or_key_matches(&store->records[key.npc_slot], key)) return NULL;
    return &store->records[key.npc_slot];
}

bool or_state_commit_spawn(OR_StateStore *store,
                           OR_InstanceKey key,
                           OR_ProgressStage progress,
                           OR_GameMode mode,
                           OR_EliteTier tier,
                           const OR_VanillaStats *vanilla,
                           const OR_FinalStats *final_stats,
                           const OR_RuleSnapshot *rules,
                           const OR_AiPlan *ai_plan) {
    OR_EliteRecord *record = or_state_find(store, key);
    if (!record || record->lifecycle != OR_LIFECYCLE_PENDING_INIT ||
        !vanilla || !final_stats || !rules || !ai_plan) return false;
    record->progress = progress;
    record->mode = mode;
    record->tier = tier;
    record->vanilla = *vanilla;
    record->final_stats = *final_stats;
    record->rules = *rules;
    record->ai_plan = *ai_plan;
    record->loot_committed = false;
    record->lifecycle = OR_LIFECYCLE_SPAWN_COMMITTED;
    store->active_elites += 1u;
    return true;
}

bool or_state_mark_live(OR_StateStore *store, OR_InstanceKey key) {
    OR_EliteRecord *record = or_state_find(store, key);
    if (!record || record->lifecycle != OR_LIFECYCLE_SPAWN_COMMITTED) return false;
    record->lifecycle = OR_LIFECYCLE_LIVE;
    return true;
}

bool or_state_mark_death(OR_StateStore *store, OR_InstanceKey key) {
    OR_EliteRecord *record = or_state_find(store, key);
    if (!record || (record->lifecycle != OR_LIFECYCLE_LIVE &&
                    record->lifecycle != OR_LIFECYCLE_SPAWN_COMMITTED)) return false;
    record->lifecycle = OR_LIFECYCLE_DEATH_STARTED;
    if (store->active_elites > 0u) store->active_elites -= 1u;
    return true;
}

bool or_state_claim_loot(OR_StateStore *store, OR_InstanceKey key) {
    OR_EliteRecord *record = or_state_find(store, key);
    if (!record || record->lifecycle != OR_LIFECYCLE_DEATH_STARTED || record->loot_committed) {
        return false;
    }
    record->loot_committed = true;
    record->lifecycle = OR_LIFECYCLE_LOOT_COMMITTED;
    return true;
}

bool or_state_cleanup(OR_StateStore *store, OR_InstanceKey key) {
    OR_EliteRecord *record = or_state_find(store, key);
    if (!record) return false;
    if (record->lifecycle == OR_LIFECYCLE_SPAWN_COMMITTED ||
        record->lifecycle == OR_LIFECYCLE_LIVE) {
        if (store->active_elites > 0u) store->active_elites -= 1u;
    }
    record->lifecycle = OR_LIFECYCLE_CLEANUP;
    record->occupied = false;
    memset(record, 0, sizeof(*record));
    return true;
}

uint32_t or_state_active_count(const OR_StateStore *store) {
    return store ? store->active_elites : 0u;
}
