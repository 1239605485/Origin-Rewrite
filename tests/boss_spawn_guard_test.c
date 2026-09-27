#include "or_config.h"
#include "or_spawn.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    OR_Config config;
    OR_StateStore store;
    OR_SpawnContext context;
    OR_SpawnResult result;

    or_config_default(&config);
    or_state_store_init(&store);
    memset(&context, 0, sizeof(context));
    context.world_session_id = 7u;
    context.world_rule_seed = 7u;
    context.spawn_tick = 20u;
    context.npc_slot = 3;
    context.npc_type = 5u;
    context.npc_active = true;
    context.host_authority = true;
    context.single_player = true;
    context.boss_encounter_active = true;
    context.progress = OR_PROGRESS_PRE_HARDMODE;
    context.mode = OR_MODE_CLASSIC;

    assert(!or_spawn_try_commit(&config, &store, &context, 11u, &result));
    assert(result.reason == OR_SPAWN_REJECT_INELIGIBLE_SOURCE);
    assert(or_state_active_count(&store) == 0u);

    puts("boss_spawn_guard_test: ok");
    return 0;
}
