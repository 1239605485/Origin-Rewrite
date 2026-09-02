#include "or_ai.h"
#include "or_config.h"
#include "or_item_registry.h"
#include "or_loot.h"
#include "or_rules.h"
#include "or_spawn.h"
#include "or_state.h"
#include "or_stats.h"
#include "or_world.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures; \
        } \
    } while (0)

static OR_VanillaStats test_vanilla(void) {
    return (OR_VanillaStats){100, 50, 10, 5, 1.0f, 1.0f, 1.0f, 100};
}

static OR_TerrainSnapshot test_terrain(void) {
    return (OR_TerrainSnapshot){OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE};
}

static void test_config_and_stats(void) {
    OR_Config config;
    OR_RuleSnapshot rules;
    OR_StatsInput input;
    OR_FinalStats altered;
    OR_FinalStats calamity;
    OR_FinalStats endgame;

    or_config_default(&config);
    CHECK(or_config_validate(&config));
    CHECK(config.modes[OR_MODE_CLASSIC].elite_chance == 1.0f);
    CHECK(config.max_active_elites == 8u);
    CHECK(config.progress[OR_PROGRESS_PRE_HARDMODE].tier_weight[OR_MODE_CLASSIC][OR_TIER_APOCALYPSE] == 0.0f);
    CHECK(config.progress[OR_PROGRESS_HARDMODE_PRE_MECH].tier_weight[OR_MODE_CLASSIC][OR_TIER_APOCALYPSE] > 0.0f);
    CHECK(config.progress[OR_PROGRESS_HARDMODE_PRE_MECH].tier_weight[OR_MODE_EXPERT][OR_TIER_APOCALYPSE] >
          config.progress[OR_PROGRESS_HARDMODE_PRE_MECH].tier_weight[OR_MODE_CLASSIC][OR_TIER_APOCALYPSE]);

    or_rules_clear(&rules);
    input = (OR_StatsInput){
        .config = &config,
        .vanilla = test_vanilla(),
        .progress = OR_PROGRESS_PRE_HARDMODE,
        .mode = OR_MODE_CLASSIC,
        .tier = OR_TIER_ALTERED,
        .rules = rules,
        .suppress_body_scale = false
    };
    CHECK(or_stats_apply(&input, &altered));
    CHECK(altered.life_max == 125);
    CHECK(altered.life_current == 63);
    CHECK(altered.damage == 11);
    CHECK(altered.defense == 8);
    CHECK(altered.money == 150);
    CHECK(altered.scale > 1.0f && altered.scale <= 1.20f);

    input.tier = OR_TIER_CALAMITY;
    CHECK(or_stats_apply(&input, &calamity));
    CHECK(calamity.life_max == 165);
    CHECK(calamity.damage == 13);
    CHECK(calamity.defense == 10);
    CHECK(calamity.money == 300);
    CHECK(calamity.life_max > altered.life_max);

    input.progress = OR_PROGRESS_ENDGAME;
    CHECK(or_stats_apply(&input, &endgame));
    CHECK(endgame.life_max > calamity.life_max);
    CHECK(endgame.damage > calamity.damage);
    CHECK(!or_stats_apply(&(OR_StatsInput){
        .config = &config,
        .vanilla = test_vanilla(),
        .progress = OR_PROGRESS_PRE_HARDMODE,
        .mode = OR_MODE_CLASSIC,
        .tier = OR_TIER_APOCALYPSE,
        .rules = rules
    }, &endgame));

    input.tier = OR_TIER_ALTERED;
    input.progress = OR_PROGRESS_PRE_HARDMODE;
    input.mode = OR_MODE_ZENITH;
    CHECK(or_stats_apply(&input, &endgame));
    CHECK(endgame.life_max == 200);

    input.suppress_body_scale = true;
    CHECK(or_stats_apply(&input, &endgame));
    CHECK(endgame.scale == 1.0f);
}

static void test_rule_caps(void) {
    OR_Config config;
    OR_RuleSnapshot snapshot;

    or_config_default(&config);
    CHECK(or_config_validate(&config));
    or_rules_clear(&snapshot);
    snapshot.progress = OR_PROGRESS_HARDMODE_PRE_MECH;
    snapshot.selected_count = 4;
    snapshot.selected_ids[0] = OR_RULE_ELITE_TIDE;
    snapshot.selected_ids[1] = OR_RULE_STRONG_WORLD;
    snapshot.selected_ids[2] = OR_RULE_HARVEST_CONTRACT;
    snapshot.selected_ids[3] = OR_RULE_NIGHT_LAW;
    snapshot.active_mask = (1u << OR_RULE_ELITE_TIDE) |
                           (1u << OR_RULE_STRONG_WORLD) |
                           (1u << OR_RULE_HARVEST_CONTRACT) |
                           (1u << OR_RULE_NIGHT_LAW);
    snapshot.terrain = test_terrain();
    snapshot.weather = OR_WEATHER_RAIN;
    snapshot.is_night = true;
    or_rules_finalize(&config, &snapshot);
    CHECK(snapshot.life_multiplier <= config.caps.rule_life_damage_defense_max);
    CHECK(snapshot.damage_multiplier <= config.caps.rule_life_damage_defense_max);
    CHECK(snapshot.defense_flat <= config.caps.rule_defense_flat_max);
    CHECK(snapshot.reward_quality_multiplier <= config.caps.rule_reward_multiplier_max);
    CHECK(snapshot.elite_chance_multiplier <= config.caps.rule_chance_max);
    CHECK(snapshot.reward_chance_bonus <= 0.40f);
    CHECK(snapshot.selected_count == 4);
}

static void test_ai_gates(void) {
    OR_Config config;
    OR_AiPlan plan;
    OR_AiPlan rage_plan;
    OR_AiRuntimeState ai_state;

    or_config_default(&config);
    CHECK(or_config_validate(&config));
    CHECK(!or_ai_build_plan(&config, OR_PROGRESS_PRE_HARDMODE, OR_TIER_APOCALYPSE,
                            OR_AI_ARCHETYPE_MELEE, 1, &plan));
    CHECK(or_ai_build_plan(&config, OR_PROGRESS_HARDMODE_PRE_MECH, OR_TIER_APOCALYPSE,
                           OR_AI_ARCHETYPE_RANGED, 1, &plan));
    CHECK(plan.has_finisher);
    CHECK(plan.summon_count == 0u); /* summon is not legal before the first mech gate */
    CHECK(or_ai_build_plan(&config, OR_PROGRESS_PRE_HARDMODE, OR_TIER_CALAMITY,
                           OR_AI_ARCHETYPE_RANGED, 1, &plan));
    CHECK(plan.finisher == OR_AI_TEMPLATE_DASH);
    CHECK(plan.fan_shot_count == 0u); /* early C-tier cannot add extra projectiles */

    memset(&rage_plan, 0, sizeof(rage_plan));
    rage_plan.primary = OR_AI_TEMPLATE_LUNGE;
    rage_plan.rage_once = true;
    rage_plan.rage_threshold = 0.25f;
    or_ai_runtime_init(&ai_state);
    CHECK(or_ai_try_trigger_rage(&rage_plan, &ai_state, 0.30f, 0.25f));
    CHECK(!or_ai_try_trigger_rage(&rage_plan, &ai_state, 0.24f, 0.20f));
}

static void test_state_and_loot(void) {
    OR_Config config;
    OR_StateStore store;
    OR_InstanceKey key;
    OR_EliteRecord *record;
    OR_RuleSnapshot rules;
    OR_AiPlan plan;
    OR_FinalStats final_stats;
    OR_LootContext loot_context;
    OR_LootResult loot_result;
    OR_StatsInput stats_input;
    OR_InstanceKey second_key;

    or_config_default(&config);
    CHECK(or_config_validate(&config));
    or_state_store_init(&store);
    or_rules_clear(&rules);
    CHECK(or_ai_build_plan(&config, OR_PROGRESS_HARDMODE_PRE_MECH, OR_TIER_CALAMITY,
                           OR_AI_ARCHETYPE_MELEE, 2, &plan));
    stats_input = (OR_StatsInput){
        .config = &config,
        .vanilla = test_vanilla(),
        .progress = OR_PROGRESS_HARDMODE_PRE_MECH,
        .mode = OR_MODE_CLASSIC,
        .tier = OR_TIER_CALAMITY,
        .rules = rules
    };
    CHECK(or_stats_apply(&stats_input, &final_stats));
    CHECK(or_state_acquire_pending(&store, 1, 7, 100, OR_SPAWN_NORMAL, 20, &key));
    CHECK(!or_state_acquire_pending(&store, 1, 7, 100, OR_SPAWN_NORMAL, 20, &second_key));
    CHECK(or_state_commit_spawn(&store, key, OR_PROGRESS_HARDMODE_PRE_MECH,
                                OR_MODE_CLASSIC, OR_TIER_CALAMITY, &stats_input.vanilla,
                                &final_stats, &rules, &plan));
    CHECK(or_state_mark_live(&store, key));
    or_state_note_spawn(&store, key);
    CHECK(or_state_active_count(&store) == 1u);
    record = or_state_find(&store, key);
    CHECK(record && record->lifecycle == OR_LIFECYCLE_LIVE);
    CHECK(or_state_mark_death(&store, key));
    CHECK(or_state_active_count(&store) == 0u);

    loot_context = (OR_LootContext){
        .host_authority = true,
        .single_player = false,
        .original_vanilla_loot_preserved = true,
        .coin_backend_verified = false,
        .progress = OR_PROGRESS_HARDMODE_PRE_MECH,
        .tier = OR_TIER_CALAMITY,
        .terrain_snapshot = test_terrain(),
        .reward_chance_bonus = 0.0f,
        .reward_quality_multiplier = 1.0f,
        .random_seed = 11
    };
    CHECK(or_loot_commit(&store, key, &config, &loot_context, &loot_result));
    CHECK(loot_result.committed);
    CHECK(loot_result.extra_reward);
    CHECK(loot_result.extra_reward_slots == 1u);
    CHECK(loot_result.kind != OR_LOOT_NONE);
    CHECK(loot_result.money_policy == OR_MONEY_NO_EXTRA_GRANT);
    CHECK(!or_loot_commit(&store, key, &config, &loot_context, &loot_result));
    CHECK(or_state_cleanup(&store, key));
    CHECK(or_state_find(&store, key) == NULL);

    CHECK(or_state_acquire_pending(&store, 1, 7, 101, OR_SPAWN_NORMAL, 21, &second_key));
    CHECK(second_key.generation_id != key.generation_id);
    CHECK(or_state_spawn_on_cooldown(&store, 1, 7, 30, 60));
    CHECK(!or_state_spawn_on_cooldown(&store, 1, 7, 80, 60));
}

static void test_spawn_authority(void) {
    OR_Config config;
    OR_StateStore store;
    OR_SpawnContext context;
    OR_SpawnResult result;
    uint64_t seed;
    bool spawned = false;

    or_config_default(&config);
    CHECK(or_config_validate(&config));
    or_state_store_init(&store);
    memset(&context, 0, sizeof(context));
    context.world_session_id = 1;
    context.world_rule_seed = 99;
    context.spawn_tick = 10;
    context.npc_slot = 8;
    context.npc_type = 200;
    context.npc_active = true;
    context.host_authority = false;
    context.single_player = false;
    context.source = OR_SPAWN_NORMAL;
    context.progress = OR_PROGRESS_HARDMODE_PRE_MECH;
    context.mode = OR_MODE_CLASSIC;
    context.terrain = test_terrain();
    context.weather = OR_WEATHER_CLEAR;
    context.archetype = OR_AI_ARCHETYPE_MELEE;
    context.max_active_elites = 8u;
    context.vanilla = test_vanilla();
    CHECK(!or_spawn_try_commit(&config, &store, &context, 1, &result));
    CHECK(result.reason == OR_SPAWN_REJECT_NOT_AUTHORITY);

    context.host_authority = true;
    seed = 1u;
    context.spawn_tick = seed;
    spawned = or_spawn_try_commit(&config, &store, &context, seed, &result);
    CHECK(spawned);
    CHECK(result.committed && result.tier != OR_TIER_NONE);
    CHECK(or_state_active_count(&store) == 1u);
    CHECK(or_state_mark_death(&store, result.key));
    CHECK(or_state_claim_loot(&store, result.key));
    CHECK(or_state_cleanup(&store, result.key));

    /* A transient SetDefaults preparation must not create a cooldown entry;
     * the live state is attached later when AI confirms active=true. */
    or_state_store_init(&store);
    context.transient_prepare = true;
    context.spawn_tick = 100u;
    CHECK(or_spawn_try_commit(&config, &store, &context, 100u, &result));
    CHECK(result.committed && result.record.final_stats.life_max > context.vanilla.life_max);
    CHECK(or_state_cleanup(&store, result.key));
    context.transient_prepare = false;
    context.spawn_tick = 101u;
    CHECK(!or_state_spawn_on_cooldown(&store, context.world_session_id,
                                      context.npc_slot, context.spawn_tick,
                                      config.same_npc_cooldown_ticks));
}

static void test_world_persistence_and_item_gate(void) {
    OR_Config config;
    OR_WorldRuleState world;
    OR_RuleSnapshot rebound;
    OR_TerrainSnapshot terrain = {OR_DEPTH_UNDERGROUND, OR_BIOME_JUNGLE, OR_SPECIAL_NONE};
    static const OR_ItemCandidate candidates[] = {
        {1001, "test_pool", OR_ITEM_MATERIAL, OR_PROGRESS_PRE_HARDMODE,
         {OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE}, false,
         true, true, false, false, false, false, false},
        {1002, "test_pool", OR_ITEM_EQUIPMENT, OR_PROGRESS_ENDGAME,
         {OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE}, false,
         true, true, false, false, false, false, true}
    };
    OR_ItemRegistry registry = {candidates, 2u, false};
    OR_Prng rng;

    or_config_default(&config);
    CHECK(or_config_validate(&config));
    CHECK(or_world_rules_create(&world, &config, OR_PROGRESS_HARDMODE_PRE_MECH,
                                terrain, OR_WEATHER_RAIN, true, 123u, 9u, 55u) == OR_WORLD_RULES_VALID);
    CHECK(world.initialized && !world.disabled_safe_mode);
    CHECK(world.snapshot.selected_count >= 2u && world.snapshot.selected_count <= 4u);
    CHECK(or_rules_rebind_snapshot(&config, &world.snapshot, OR_PROGRESS_ENDGAME,
                                   test_terrain(), OR_WEATHER_CLEAR, false, &rebound));
    CHECK(rebound.selected_count == world.snapshot.selected_count);
    CHECK(rebound.progress == OR_PROGRESS_ENDGAME);
    CHECK(rebound.weather == OR_WEATHER_CLEAR && !rebound.is_night);
    CHECK(or_world_rules_restore(&world, &config, 1u, 1u, 9u, 55u) == OR_WORLD_RULES_VALID);
    CHECK(or_world_rules_restore(&world, &config, 1u, 1u, 10u, 55u) == OR_WORLD_RULES_DISABLED_SAFE_MODE);
    CHECK(world.disabled_safe_mode && !world.initialized);

    CHECK(!or_item_candidate_is_legal(&candidates[1], OR_PROGRESS_PRE_HARDMODE, terrain));
    CHECK(or_item_candidate_is_legal(&candidates[0], OR_PROGRESS_PRE_HARDMODE, terrain));
    or_prng_seed(&rng, 7u);
    CHECK(or_item_registry_pick(&registry, "test_pool", OR_PROGRESS_PRE_HARDMODE, terrain, &rng) == NULL);
    registry.item_ids_verified = true;
    CHECK(or_item_registry_pick(&registry, "test_pool", OR_PROGRESS_PRE_HARDMODE, terrain, &rng) == &candidates[0]);
}

int main(void) {
    test_config_and_stats();
    test_rule_caps();
    test_ai_gates();
    test_state_and_loot();
    test_spawn_authority();
    test_world_persistence_and_item_gate();
    if (g_failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", g_failures);
        return 1;
    }
    puts("OriginRewrite core tests passed");
    return 0;
}
