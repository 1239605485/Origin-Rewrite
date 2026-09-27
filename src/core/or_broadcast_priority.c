#include "or_broadcast.h"

#include <stdint.h>

void or_broadcast_hold_for_boss(OR_BroadcastState *state, uint64_t now_tick) {
    uint64_t hold_until;
    if (!state) return;
    hold_until = now_tick + 120u;
    if (hold_until < now_tick) hold_until = UINT64_MAX;
    if (hold_until > state->boss_lock_until_tick)
        state->boss_lock_until_tick = hold_until;
}
