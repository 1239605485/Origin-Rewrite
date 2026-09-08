#include "or_broadcast.h"

#include "or_log.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"

#include <stdio.h>
#include <string.h>

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

typedef enum OR_BroadcastMessageId {
    OR_BROADCAST_MESSAGE_CALAMITY_SPAWN = 1u,
    OR_BROADCAST_MESSAGE_APOCALYPSE_SPAWN = 2u,
    OR_BROADCAST_MESSAGE_TERRAIN = 3u
} OR_BroadcastMessageId;

static const char *terrain_depth_name(OR_DepthTag depth) {
    static const char *const names[] = {"地表", "地下", "洞穴", "地狱"};
    return depth < OR_DEPTH_COUNT ? names[depth] : "地表";
}

static const char *terrain_biome_name(OR_BiomeTag biome) {
    static const char *const names[] = {"森林", "沙漠", "雪原", "丛林", "神圣", "腐化", "猩红"};
    return biome < OR_BIOME_COUNT ? names[biome] : "森林";
}

static const char *terrain_special_name(OR_SpecialLocationTag special) {
    static const char *const names[] = {"", "海洋", "地牢", "发光蘑菇洞", "天空"};
    return special < OR_SPECIAL_COUNT ? names[special] : "";
}

static const char *tier_prefix(OR_EliteTier tier) {
    switch (tier) {
    case OR_TIER_APOCALYPSE: return "终焉体";
    case OR_TIER_CALAMITY: return "灾变体";
    case OR_TIER_ALTERED: return "异化体";
    default: return NULL;
    }
}

static uint32_t message_id_for_tier(OR_EliteTier tier) {
    return tier == OR_TIER_APOCALYPSE
        ? OR_BROADCAST_MESSAGE_APOCALYPSE_SPAWN
        : OR_BROADCAST_MESSAGE_CALAMITY_SPAWN;
}

static void tier_rgba(OR_EliteTier tier, uint8_t rgba[4]) {
    uint32_t packed;
    switch (tier) {
    case OR_TIER_ALTERED: packed = 0xFF4CAF50u; break;
    case OR_TIER_CALAMITY: packed = 0xFF3D8DFFu; break;
    case OR_TIER_APOCALYPSE: packed = 0xFFE53935u; break;
    default: packed = 0xFFFFFFFFu; break;
    }
    rgba[0] = (uint8_t)((packed >> 16) & 0xFFu);
    rgba[1] = (uint8_t)((packed >> 8) & 0xFFu);
    rgba[2] = (uint8_t)(packed & 0xFFu);
    rgba[3] = (uint8_t)((packed >> 24) & 0xFFu);
}

void or_broadcast_init(OR_BroadcastState *state) {
    if (state) memset(state, 0, sizeof(*state));
}

bool or_broadcast_emit_elite(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             OR_EliteTier tier,
                             uint32_t npc_type,
                             uint64_t generation_id,
                             uint64_t snapshot_revision,
                             uint64_t now_tick) {
    char message[192];
    patch_handle_t message_handle;
    uint32_t message_id;
    uint64_t ignored_return = 0u;
    void *args[4] = {NULL, NULL, NULL, NULL};
    bool force_display = true;
    uint8_t rgba[4];
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint64_t color_pointer_storage;
    int32_t red_i;
    int32_t green_i;
    int32_t blue_i;
    const char *prefix;

    if (!state || !runtime || tier < OR_TIER_CALAMITY) return false;
    message_id = message_id_for_tier(tier);
    prefix = tier_prefix(tier);
    tier_rgba(tier, rgba);
    red = rgba[0];
    green = rgba[1];
    blue = rgba[2];
    color_pointer_storage = 0u;
    memcpy(&color_pointer_storage, rgba, sizeof(rgba));
    red_i = red;
    green_i = green;
    blue_i = blue;
    if (!prefix || !runtime->capabilities.new_text_ready ||
        (runtime->main_new_text_arg_count != 1 &&
         runtime->main_new_text_arg_count != 3 &&
         runtime->main_new_text_arg_count != 4) ||
        !runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=api_unavailable",
               (unsigned)message_id, (unsigned)npc_type);
        return false;
    }
    if (state->last_message_id == message_id &&
        state->last_generation_id == generation_id) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BROADCAST_DEDUP] messageId=%u generation=%llu",
               (unsigned)message_id, (unsigned long long)generation_id);
        return false;
    }
    if (state->last_emit_tick != 0u && now_tick < state->last_emit_tick + 120u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BROADCAST_COOLDOWN] messageId=%u generation=%llu",
               (unsigned)message_id, (unsigned long long)generation_id);
        return false;
    }
    if (snprintf(message, sizeof(message),
                 tier == OR_TIER_CALAMITY
                     ? "【灾变体警报】世界规则发生偏移，灾变体已从裂缝中现身。"
                     : "【终焉体警报】重写波动越过边界，终焉体已降临。") >=
        (int)sizeof(message)) return false;
    message_handle = patchlib_string_create(message);
    if (!message_handle) return false;
    args[0] = &message_handle;
    if (runtime->main_new_text_arg_count == 3) {
        if (runtime->main_new_text_color_type != PATCH_POINTER) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=color_pointer_abi_unknown",
                   (unsigned)message_id, (unsigned)npc_type);
            return false;
        }
        /* PATCH_POINTER is the native pointer slot for the managed Color
         * valuetype. The invoke API expects args[index] to point at that slot. */
        args[1] = &color_pointer_storage;
        args[2] = &force_display;
    } else if (runtime->main_new_text_arg_count == 4) {
        if (runtime->main_new_text_color_type == PATCH_UINT8) {
            args[1] = &red;
            args[2] = &green;
            args[3] = &blue;
        } else if (runtime->main_new_text_color_type == PATCH_INT32) {
            args[1] = &red_i;
            args[2] = &green_i;
            args[3] = &blue_i;
        } else {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=color_abi_unknown",
                   (unsigned)message_id, (unsigned)npc_type);
            return false;
        }
    }
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COLOR] messageId=%u rgba=%u,%u,%u,%u",
           (unsigned)message_id, (unsigned)rgba[0], (unsigned)rgba[1],
           (unsigned)rgba[2], (unsigned)rgba[3]);
    if (!patchlib_method_invoke_args(runtime->method_main_new_text,
                                     PATCH_NULL, &ignored_return, args)) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=invoke_failed",
               (unsigned)message_id, (unsigned)npc_type);
        return false;
    }
    state->last_message_id = message_id;
    state->last_generation_id = generation_id;
    state->last_emit_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COMMIT] messageId=%u channel=elite_spawn scope=local_host "
           "type=%u generation=%llu snapshotRevision=%llu",
           (unsigned)message_id, (unsigned)npc_type,
           (unsigned long long)generation_id,
           (unsigned long long)snapshot_revision);
    return true;
}

bool or_broadcast_emit_terrain(OR_BroadcastState *state,
                               const OR_Runtime *runtime,
                               OR_TerrainSnapshot terrain,
                               uint64_t snapshot_revision,
                               uint64_t now_tick) {
    char message[192];
    patch_handle_t string_handle;
    uint64_t ignored_return = 0u;
    void *args[4] = {NULL, NULL, NULL, NULL};
    uint8_t rgba[4] = {180u, 210u, 255u, 255u};
    bool force_display = true;
    uint32_t key = ((uint32_t)terrain.depth << 16) |
                   ((uint32_t)terrain.biome << 8) | (uint32_t)terrain.special;
    const char *special = terrain_special_name(terrain.special);
    int count;
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        !runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args || terrain.depth >= OR_DEPTH_COUNT ||
        terrain.biome >= OR_BIOME_COUNT || terrain.special >= OR_SPECIAL_COUNT) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=api_or_snapshot_unavailable");
        return false;
    }
    if (state->last_terrain_key == key && state->last_terrain_tick != 0u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=duplicate key=%u", (unsigned)key);
        return false;
    }
    if (state->last_terrain_tick != 0u && now_tick < state->last_terrain_tick + 120u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=cooldown key=%u", (unsigned)key);
        return false;
    }
    count = snprintf(message, sizeof(message), "起源回响：%s·%s%s",
                     terrain_depth_name(terrain.depth), terrain_biome_name(terrain.biome),
                     special[0] != '\0' ? special : "");
    if (count < 0 || count >= (int)sizeof(message)) return false;
    string_handle = patchlib_string_create(message);
    if (!string_handle) return false;
    args[0] = &string_handle;
    if (runtime->main_new_text_arg_count == 3 && runtime->main_new_text_color_type == PATCH_POINTER) {
        args[1] = rgba;
        args[2] = &force_display;
    } else if (runtime->main_new_text_arg_count != 1) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=notice_signature argCount=%d colorType=%d",
               runtime->main_new_text_arg_count, (int)runtime->main_new_text_color_type);
        return false;
    }
    if (!patchlib_method_invoke_args(runtime->method_main_new_text, PATCH_NULL,
                                     &ignored_return, args)) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=invoke_failed key=%u", (unsigned)key);
        return false;
    }
    state->last_terrain_key = key;
    state->last_terrain_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[TERRAIN_BROADCAST_COMMIT] depth=%s biome=%s special=%s revision=%llu",
           terrain_depth_name(terrain.depth), terrain_biome_name(terrain.biome),
           special[0] != '\0' ? special : "none",
           (unsigned long long)snapshot_revision);
    return true;
}

bool or_broadcast_emit_world(OR_BroadcastState *state,
                             const OR_Runtime *runtime,
                             OR_Weather weather,
                             bool is_night,
                             uint64_t snapshot_revision,
                             uint64_t now_tick) {
    char message[160];
    patch_handle_t string_handle;
    uint64_t ignored_return = 0u;
    void *args[4] = {NULL, NULL, NULL, NULL};
    uint8_t rgba[4] = {255u, 220u, 120u, 255u};
    bool force_display = true;
    uint32_t key = ((uint32_t)weather << 1) | (is_night ? 1u : 0u);
    const char *weather_name;
    int count;
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        !runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args || weather >= OR_WEATHER_COUNT) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=api_or_snapshot_unavailable");
        return false;
    }
    if (state->last_terrain_key == key && state->last_terrain_tick != 0u) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=duplicate key=%u", (unsigned)key);
        return false;
    }
    if (state->last_terrain_tick != 0u && now_tick < state->last_terrain_tick + 120u) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=cooldown key=%u", (unsigned)key);
        return false;
    }
    weather_name = or_weather_name(weather);
    count = snprintf(message, sizeof(message), "起源律动：%s·%s",
                     is_night ? "夜晚" : "白昼", weather_name);
    if (count < 0 || count >= (int)sizeof(message)) return false;
    string_handle = patchlib_string_create(message);
    if (!string_handle) return false;
    args[0] = &string_handle;
    if (runtime->main_new_text_arg_count == 3 && runtime->main_new_text_color_type == PATCH_POINTER) {
        args[1] = rgba;
        args[2] = &force_display;
    } else if (runtime->main_new_text_arg_count != 1) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=notice_signature");
        return false;
    }
    if (!patchlib_method_invoke_args(runtime->method_main_new_text, PATCH_NULL,
                                     &ignored_return, args)) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=invoke_failed key=%u", (unsigned)key);
        return false;
    }
    state->last_terrain_key = key;
    state->last_terrain_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[WORLD_BROADCAST_COMMIT] night=%s weather=%s key=%u revision=%llu",
           is_night ? "yes" : "no", weather_name, (unsigned)key,
           (unsigned long long)snapshot_revision);
    return true;
}
