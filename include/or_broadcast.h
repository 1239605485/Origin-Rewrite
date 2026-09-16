#ifndef ORIGINREWRITE_BROADCAST_H
#define ORIGINREWRITE_BROADCAST_H

#include "or_runtime.h"
#include "or_types.h"
#include "or_boss_dialog_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Terraria advances 60 ticks per second.  A Boss card is three lines and is
 * kept exclusive for two seconds so ordinary world/elite notices cannot
 * cover it while it is being read. */
#define OR_BOSS_DIALOG_LOCK_TICKS 120u
/* Temporary test cadence for the daily world-status card: 60 ticks = 1 s. */
#define OR_DAILY_BROADCAST_INTERVAL_TICKS 1800u

typedef struct OR_BroadcastState {
    uint32_t next_message_id;
    uint32_t last_message_id;
    uint32_t last_elite_tier;
    uint64_t last_generation_id;
    uint64_t last_emit_tick;
    uint64_t snapshot_revision;
    uint32_t last_terrain_key;
    uint64_t last_terrain_tick;
    uint32_t last_world_key;
    uint64_t last_world_tick;
    uint64_t last_rule_summary_key;
    uint64_t last_daily_broadcast_wall_second;
    /* Narrative counters are intentionally independent from loot and from
     * transient chat de-duplication.  They survive a local-player respawn. */
    uint32_t boss_summon_count[1024];
    uint32_t boss_kill_count[1024];
    uint32_t boss_player_death_count[1024];
    uint64_t boss_lock_until_tick;
} OR_BroadcastState;

void or_broadcast_init(OR_BroadcastState *state);
void or_broadcast_on_player_respawn(OR_BroadcastState *state);
bool or_broadcast_emit_elite(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             OR_EliteTier tier,
                             uint32_t npc_type,
                             uint64_t generation_id,
                             uint64_t snapshot_revision,
                             uint64_t now_tick);
bool or_broadcast_emit_terrain(OR_BroadcastState *state,
                               const OR_Runtime *runtime,
                               OR_TerrainSnapshot terrain,
                               uint64_t snapshot_revision,
                               uint64_t now_tick);
bool or_broadcast_emit_world(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             OR_Weather weather,
                             bool is_night,
                             uint64_t snapshot_revision,
                             uint64_t now_tick);
bool or_broadcast_emit_rule_summary(OR_BroadcastState *state,
                                    const OR_Runtime *runtime,
                                    const OR_RuleSnapshot *snapshot,
                                    uint64_t rule_revision,
                                    uint64_t now_tick);
bool or_broadcast_emit_daily(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             const OR_RuleSnapshot *snapshot,
                             uint64_t rule_revision,
                             uint64_t now_tick);
bool or_broadcast_emit_boss_dialog(OR_BroadcastState *state, const OR_Runtime *runtime,
                                   uint32_t npc_type, OR_BossDialogEvent event,
                                   uint64_t now_tick);
bool or_broadcast_emit_boss_player_death(OR_BroadcastState *state,
                                         const OR_Runtime *runtime,
                                         uint32_t npc_type, uint32_t death_count,
                                         uint64_t now_tick);

#ifdef __cplusplus
}
#endif

#endif
