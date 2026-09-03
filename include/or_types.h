#ifndef ORIGINREWRITE_TYPES_H
#define ORIGINREWRITE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OR_MAX_WORLD_RULES 4u
#define OR_MAX_TRACKED_NPCS 1024u
#define OR_MAX_AI_SUMMONS 2u
#define OR_MAX_AI_FAN_SHOTS 5u

typedef enum OR_ProgressStage {
    OR_PROGRESS_PRE_HARDMODE = 0,
    OR_PROGRESS_HARDMODE_PRE_MECH,
    OR_PROGRESS_PRE_PLANTERA,
    OR_PROGRESS_POST_PLANTERA,
    OR_PROGRESS_ENDGAME,
    OR_PROGRESS_COUNT
} OR_ProgressStage;

typedef enum OR_GameMode {
    OR_MODE_CLASSIC = 0,
    OR_MODE_EXPERT,
    OR_MODE_MASTER,
    OR_MODE_ZENITH,
    OR_MODE_JOURNEY,
    OR_MODE_COUNT
} OR_GameMode;

typedef enum OR_EliteTier {
    OR_TIER_NONE = 0,
    OR_TIER_ALTERED,
    OR_TIER_CALAMITY,
    OR_TIER_APOCALYPSE,
    OR_TIER_COUNT
} OR_EliteTier;

typedef enum OR_DepthTag {
    OR_DEPTH_SURFACE = 0,
    OR_DEPTH_UNDERGROUND,
    OR_DEPTH_CAVERN,
    OR_DEPTH_UNDERWORLD,
    OR_DEPTH_COUNT
} OR_DepthTag;

typedef enum OR_BiomeTag {
    OR_BIOME_FOREST = 0,
    OR_BIOME_DESERT,
    OR_BIOME_SNOW,
    OR_BIOME_JUNGLE,
    OR_BIOME_HALLOW,
    OR_BIOME_CORRUPTION,
    OR_BIOME_CRIMSON,
    OR_BIOME_COUNT
} OR_BiomeTag;

typedef enum OR_SpecialLocationTag {
    OR_SPECIAL_NONE = 0,
    OR_SPECIAL_OCEAN,
    OR_SPECIAL_DUNGEON,
    OR_SPECIAL_MUSHROOM,
    OR_SPECIAL_SKY,
    OR_SPECIAL_COUNT
} OR_SpecialLocationTag;

typedef struct OR_TerrainSnapshot {
    OR_DepthTag depth;
    OR_BiomeTag biome;
    OR_SpecialLocationTag special;
} OR_TerrainSnapshot;

typedef enum OR_Weather {
    OR_WEATHER_CLEAR = 0,
    OR_WEATHER_RAIN,
    OR_WEATHER_SANDSTORM,
    OR_WEATHER_BLIZZARD,
    OR_WEATHER_ECLIPSE,
    OR_WEATHER_BLOOD_MOON,
    OR_WEATHER_COUNT
} OR_Weather;

const char *or_weather_name(OR_Weather weather);

typedef enum OR_SpawnSource {
    OR_SPAWN_NORMAL = 0,
    OR_SPAWN_ELITE,
    OR_SPAWN_ELITE_SUMMONED,
    OR_SPAWN_SPLIT_FROM_ELITE,
    OR_SPAWN_EVENT,
    OR_SPAWN_SEGMENT
} OR_SpawnSource;

typedef enum OR_EliteLifecycle {
    OR_LIFECYCLE_EMPTY = 0,
    OR_LIFECYCLE_PENDING_INIT,
    OR_LIFECYCLE_SPAWN_COMMITTED,
    OR_LIFECYCLE_LIVE,
    OR_LIFECYCLE_DEATH_STARTED,
    OR_LIFECYCLE_LOOT_COMMITTED,
    OR_LIFECYCLE_CLEANUP
} OR_EliteLifecycle;

typedef enum OR_AiArchetype {
    OR_AI_ARCHETYPE_MELEE = 0,
    OR_AI_ARCHETYPE_RANGED,
    OR_AI_ARCHETYPE_FLYING,
    OR_AI_ARCHETYPE_WORM,
    OR_AI_ARCHETYPE_SWARM,
    OR_AI_ARCHETYPE_SPECIAL
} OR_AiArchetype;

typedef enum OR_AiTemplate {
    OR_AI_TEMPLATE_NONE = 0,
    OR_AI_TEMPLATE_LUNGE,
    OR_AI_TEMPLATE_PROJECTILE_BURST,
    OR_AI_TEMPLATE_BURROW,
    OR_AI_TEMPLATE_DASH,
    OR_AI_TEMPLATE_FAN_SHOT,
    OR_AI_TEMPLATE_PHASE,
    OR_AI_TEMPLATE_SUMMON,
    OR_AI_TEMPLATE_RAGE
} OR_AiTemplate;

typedef enum OR_ElementTag {
    OR_ELEMENT_NONE = 0,
    OR_ELEMENT_FROST,
    OR_ELEMENT_HEAT,
    OR_ELEMENT_TOXIC,
    OR_ELEMENT_CORRUPT,
    OR_ELEMENT_CRIMSON
} OR_ElementTag;

typedef struct OR_VanillaStats {
    int64_t life_max;
    int64_t life_current;
    int32_t damage;
    int32_t defense;
    float knockback_resist;
    float scale;
    float npc_slots;
    int64_t money;
    /* Exact native NPC.aiStyle observed at activation. Negative means the
     * optional field was unavailable; it is not interpreted yet. */
    int32_t ai_style;
} OR_VanillaStats;

typedef struct OR_FinalStats {
    int64_t life_max;
    int64_t life_current;
    int32_t damage;
    int32_t defense;
    float knockback_resist;
    float scale;
    float npc_slots;
    int64_t money;
} OR_FinalStats;

typedef struct OR_TierConfig {
    bool enabled;
    float defense_multiplier;
    float money_multiplier;
    float knockback_reduction;
    float scale_multiplier;
    float slot_multiplier;
    float ai_intensity;
} OR_TierConfig;

typedef struct OR_ProgressConfig {
    float tier_weight[OR_MODE_COUNT][OR_TIER_COUNT];
    /* Stage × tier values are kept together to prevent double scaling. */
    float life_multiplier[OR_TIER_COUNT];
    float damage_multiplier[OR_TIER_COUNT];
    float defense_multiplier[OR_TIER_COUNT];
    int32_t defense_flat[OR_TIER_COUNT];
    float money_multiplier;
} OR_ProgressConfig;

typedef struct OR_ModeConfig {
    float elite_chance;
    float life_multiplier;
    float damage_multiplier;
    float defense_multiplier;
    float knockback_reduction;
} OR_ModeConfig;

typedef struct OR_LootConfig {
    float altered_extra_reward_chance;
    bool allow_vanilla_crates;
    uint8_t max_extra_reward_slots;
} OR_LootConfig;

typedef struct OR_EligibilityConfig {
    bool allow_bosses;
    bool allow_events;
    bool allow_summoned;
    bool allow_splits;
    bool allow_segments;
} OR_EligibilityConfig;

typedef struct OR_Caps {
    float rule_life_damage_defense_min;
    float rule_life_damage_defense_max;
    int32_t rule_defense_flat_max;
    float rule_reward_multiplier_max;
    float extra_money_multiplier_max;
    float rule_chance_max;
    float rule_ai_intensity_max;
    float max_scale_over_vanilla;
    float max_total_knockback_reduction;
} OR_Caps;

typedef struct OR_Config {
    uint32_t schema_version;
    bool enable_elites;
    bool enable_gameplay_hooks;
    uint32_t max_active_elites;
    uint64_t same_npc_cooldown_ticks;
    float journey_probability_multiplier;
    bool allow_pre_hardmode_apocalypse;
    OR_ProgressConfig progress[OR_PROGRESS_COUNT];
    OR_ModeConfig modes[OR_MODE_COUNT];
    OR_TierConfig tiers[OR_TIER_COUNT];
    OR_EligibilityConfig eligibility;
    OR_LootConfig loot;
    OR_Caps caps;
} OR_Config;

typedef struct OR_RuleSnapshot {
    uint32_t selected_ids[OR_MAX_WORLD_RULES];
    size_t selected_count;
    uint32_t active_mask;
    float life_multiplier;
    float damage_multiplier;
    float defense_multiplier;
    int32_t defense_flat;
    float money_multiplier;
    float elite_chance_multiplier;
    float ai_intensity;
    OR_ProgressStage progress;
    OR_TerrainSnapshot terrain;
    OR_Weather weather;
    bool is_night;
    float reward_chance_bonus;
    float reward_quality_multiplier;
    float movement_multiplier;
    float tier_weight_multiplier[OR_TIER_COUNT];
    OR_ElementTag preferred_element;
} OR_RuleSnapshot;

typedef struct OR_AiPlan {
    OR_AiTemplate primary;
    OR_AiTemplate finisher;
    bool has_light_layer;
    bool has_finisher;
    bool rage_once;
    uint8_t summon_count;
    uint8_t fan_shot_count;
    float intensity;
    float rage_threshold;
    uint32_t telegraph_ticks;
    uint32_t active_ticks;
    uint32_t recovery_ticks;
    uint32_t cooldown_ticks;
} OR_AiPlan;

typedef struct OR_AiRuntimeState {
    uint32_t action_started_tick;
    uint32_t cooldown_until_tick;
    uint8_t phase;
    bool rage_triggered;
} OR_AiRuntimeState;

typedef struct OR_InstanceKey {
    uint64_t world_session_id;
    int32_t npc_slot;
    uint64_t generation_id;
} OR_InstanceKey;

#endif /* ORIGINREWRITE_TYPES_H */
