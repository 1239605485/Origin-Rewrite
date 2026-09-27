#include "or_config.h"
#include "or_rules.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

int main(void) {
    OR_Config config;
    OR_TerrainSnapshot terrain = {
        OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE
    };
    OR_RuleSnapshot snapshot;
    OR_RuleSnapshot repeat;
    bool seen[OR_MAX_WORLD_RULES + 1u] = {false};
    uint64_t seed;

    or_config_default(&config);
    for (seed = 1u; seed <= 256u; ++seed) {
        assert(or_rules_build_snapshot(&config, OR_PROGRESS_PRE_HARDMODE,
                                       terrain, OR_WEATHER_CLEAR, true,
                                       seed, &snapshot));
        assert(snapshot.selected_count >= 2u);
        assert(snapshot.selected_count <= 4u);
        seen[snapshot.selected_count] = true;
    }
    assert(seen[2u] && seen[3u] && seen[4u]);

    assert(or_rules_build_snapshot(&config, OR_PROGRESS_PRE_HARDMODE,
                                   terrain, OR_WEATHER_CLEAR, true,
                                   UINT64_C(0x1020304050607080), &snapshot));
    assert(or_rules_build_snapshot(&config, OR_PROGRESS_PRE_HARDMODE,
                                   terrain, OR_WEATHER_CLEAR, true,
                                   UINT64_C(0x1020304050607080), &repeat));
    assert(snapshot.selected_count == repeat.selected_count);
    for (seed = 0u; seed < snapshot.selected_count; ++seed) {
        assert(snapshot.selected_ids[seed] == repeat.selected_ids[seed]);
    }

    puts("world_rules_test: ok");
    return 0;
}
