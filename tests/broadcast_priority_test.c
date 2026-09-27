#include "or_broadcast.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    OR_BroadcastState state;
    memset(&state, 0, sizeof(state));
    or_broadcast_hold_for_boss(&state, 100u);
    assert(state.boss_lock_until_tick == 220u);
    or_broadcast_hold_for_boss(&state, 110u);
    assert(state.boss_lock_until_tick == 230u);
    or_broadcast_hold_for_boss(&state, 90u);
    assert(state.boss_lock_until_tick == 230u);
    or_broadcast_hold_for_boss(&state, UINT64_MAX - 2u);
    assert(state.boss_lock_until_tick == UINT64_MAX);
    puts("broadcast_priority_test: ok");
    return 0;
}
