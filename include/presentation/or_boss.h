#ifndef OR_BOSS_H
#define OR_BOSS_H
#include "presentation/or_notice.h"
typedef struct { bool occupied,half,ended; OR_Key key; int32_t type; uint64_t seen; } OR_BossState;
typedef struct {
    OR_BossState states[OR_SLOT_COUNT];
    uint16_t arrivals[1024],kills[1024],player_losses[1024];
} OR_BossObserver;
void or_boss_reset(OR_BossObserver *s);
void or_boss_observe(OR_BossObserver *s, OR_NoticeQueue *q, OR_Key key, const OR_NpcView *v);
void or_boss_loot(OR_BossObserver *s, OR_NoticeQueue *q, OR_Key key, uint64_t tick);
void or_boss_player_death(OR_BossObserver *s, OR_NoticeQueue *q, uint64_t tick);
void or_boss_sweep(OR_BossObserver *s, uint64_t tick);
#endif
