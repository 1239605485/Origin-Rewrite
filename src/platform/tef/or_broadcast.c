#include "or_broadcast.h"
#include "or_config.h"
#include "or_rules.h"

#include "or_log.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"

#include <stdio.h>
#include <string.h>

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

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

static const char *progress_stage_name_zh(OR_ProgressStage stage) {
    static const char *const names[] = {
        "起源稳定期", "深层扰动期", "生命重构期", "起源崩解期", "终局重构期"
    };
    return stage < OR_PROGRESS_COUNT ? names[stage] : "未知阶段";
}

static const char *weather_name_zh(OR_Weather weather) {
    static const char *const names[] = {
        "晴朗", "降雨", "沙尘暴", "暴雪", "日食", "血月", "南瓜月", "霜月",
        "史莱姆雨", "大风"
    };
    return weather < OR_WEATHER_COUNT ? names[weather] : "未知天气";
}

static const char *world_rule_name_zh(uint32_t rule_id) {
    static const char *const names[] = {
        "重构潮汐", "强敌世界", "丰收契约", "暴风边境", "夜行法则",
        "深渊回响", "邪恶侵染", "玻璃炮"
    };
    return rule_id < OR_RULE_COUNT ? names[rule_id] : "未知规则";
}

static const char *tier_prefix(OR_EliteTier tier) {
    switch (tier) {
    case OR_TIER_APOCALYPSE: return "终焉体";
    case OR_TIER_CALAMITY: return "灾变体";
    case OR_TIER_ALTERED: return "异化体";
    default: return NULL;
    }
}

static void tier_rgba(OR_EliteTier tier, uint8_t rgba[4]) {
    uint32_t packed;
    switch (tier) {
    case OR_TIER_ALTERED: packed = 0xFF8FFFA8u; break; /* mint */
    case OR_TIER_CALAMITY: packed = 0xFF80C7FFu; break; /* sky */
    case OR_TIER_APOCALYPSE: packed = 0xFFF0A0FFu; break; /* lilac */
    default: packed = 0xFFFFFFFFu; break;
    }
    rgba[0] = (uint8_t)((packed >> 16) & 0xFFu);
    rgba[1] = (uint8_t)((packed >> 8) & 0xFFu);
    rgba[2] = (uint8_t)(packed & 0xFFu);
    rgba[3] = (uint8_t)((packed >> 24) & 0xFFu);
}

static const char *tier_hex(OR_EliteTier tier) {
    switch (tier) {
    case OR_TIER_ALTERED: return "8FFFA8";
    case OR_TIER_CALAMITY: return "80C7FF";
    case OR_TIER_APOCALYPSE: return "F0A0FF";
    default: return "FFFFFF";
    }
}

static const char *channel_hex(const char *channel) {
    if (!channel) return "FFFFFF";
    if (strcmp(channel, "terrain") == 0) return "8AE7FF";
    if (strcmp(channel, "weather") == 0) return "FFC477";
    if (strcmp(channel, "world_rule") == 0) return "FFE08A";
    if (strcmp(channel, "boss") == 0) return "FF9CA8";
    return "FFFFFF";
}

static bool wrap_chat_color(char *message, size_t message_size,
                            const char *hex) {
    char body[256];
    if (!message || !message_size || !hex) return false;
    (void)snprintf(body, sizeof(body), "%s", message);
    return snprintf(message, message_size, "[c/%s:%s]", hex, body) <
           (int)message_size;
}

void or_broadcast_init(OR_BroadcastState *state) {
    if (state) {
        memset(state, 0, sizeof(*state));
        state->next_message_id = 1u;
    }
}

static uint32_t next_message_id(const OR_BroadcastState *state) {
    return state && state->next_message_id != 0u ? state->next_message_id : 1u;
}

static void commit_message_id(OR_BroadcastState *state, uint32_t message_id) {
    uint32_t next;
    if (!state) return;
    state->last_message_id = message_id;
    next = message_id + 1u;
    state->next_message_id = next == 0u ? 1u : next;
}

static bool invoke_new_text(const OR_Runtime *runtime,
                            patch_handle_t text_handle,
                            const uint8_t rgba[4],
                            bool force_display,
    uint64_t *ignored_return) {
    void *args[4] = {NULL, NULL, NULL, NULL};
#if defined(__ANDROID__)
    static const patch_type_t color_fields[] = {
        PATCH_UINT8, PATCH_UINT8, PATCH_UINT8, PATCH_UINT8
    };
#endif
    if (!runtime || !text_handle || !rgba || !ignored_return ||
        !runtime->method_main_new_text || !patchlib_method_invoke_args) return false;
    args[0] = &text_handle;
    if (runtime->main_new_text_arg_count == 1) {
        return patchlib_method_invoke_args(runtime->method_main_new_text,
                                           PATCH_NULL, ignored_return, args);
    }
    if (runtime->main_new_text_arg_count == 3 &&
        runtime->main_new_text_color_type == PATCH_POINTER) {
#if defined(__ANDROID__)
        static bool color_abi_logged;
        patchlib_value_arg_t value_args[3] = {
            {NULL, 0u, NULL, 0u},
            {(void *)rgba, 4u, color_fields, 4u},
            {NULL, 0u, NULL, 0u}
        };
        if (!patchlib_method_invoke_value_args) return false;
        if (!color_abi_logged) {
            color_abi_logged = true;
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[NEWTEXT_COLOR_ABI] mode=value fields=uint8,uint8,uint8,uint8 "
                   "size=4 verified=yes");
        }
        /* Color is a managed value type. Passing the four component bytes as
         * a raw pointer through invoke_args loses the value-type ABI; the
         * value bridge is required for the mobile NewText overload. */
        args[1] = (void *)rgba;
        args[2] = &force_display;
        return patchlib_method_invoke_value_args(runtime->method_main_new_text,
                                                 PATCH_NULL, ignored_return,
                                                 args, value_args);
#else
        (void)force_display;
        return false;
#endif
    }
    if (runtime->main_new_text_arg_count == 4) {
        uint8_t red = rgba[0];
        uint8_t green = rgba[1];
        uint8_t blue = rgba[2];
        int32_t red_i = (int32_t)rgba[0];
        int32_t green_i = (int32_t)rgba[1];
        int32_t blue_i = (int32_t)rgba[2];
        if (runtime->main_new_text_color_type == PATCH_UINT8) {
            args[1] = &red;
            args[2] = &green;
            args[3] = &blue;
        } else if (runtime->main_new_text_color_type == PATCH_INT32) {
            args[1] = &red_i;
            args[2] = &green_i;
            args[3] = &blue_i;
        } else {
            return false;
        }
        return patchlib_method_invoke_args(runtime->method_main_new_text,
                                           PATCH_NULL, ignored_return, args);
    }
    return false;
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
    bool force_display = true;
    uint8_t rgba[4];
    const char *prefix;

    if (!state || !runtime || tier < OR_TIER_ALTERED || tier > OR_TIER_APOCALYPSE) return false;
    message_id = next_message_id(state);
    prefix = tier_prefix(tier);
    tier_rgba(tier, rgba);
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
    if (generation_id != 0u && state->last_generation_id == generation_id &&
        state->last_elite_tier == (uint32_t)tier) {
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
                 tier == OR_TIER_ALTERED
                     ? "【异化体警报】世界规则发生偏移，异化体已现身。"
                     : (tier == OR_TIER_CALAMITY
                         ? "【灾变体警报】世界规则发生偏移，灾变体已从裂缝中现身。"
                         : "【终焉体警报】重写波动越过边界，终焉体已降临。")) >=
                 (int)sizeof(message)) return false;
    if (!wrap_chat_color(message, sizeof(message), tier_hex(tier))) return false;
    message_handle = patchlib_string_create(message);
    if (!message_handle) return false;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COLOR] messageId=%u rgba=%u,%u,%u,%u",
           (unsigned)message_id, (unsigned)rgba[0], (unsigned)rgba[1],
           (unsigned)rgba[2], (unsigned)rgba[3]);
    if (!invoke_new_text(runtime, message_handle, rgba, force_display,
                         &ignored_return)) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[BROADCAST_SAFE_OFF] messageId=%u type=%u reason=invoke_failed",
               (unsigned)message_id, (unsigned)npc_type);
        return false;
    }
    commit_message_id(state, message_id);
    state->last_elite_tier = (uint32_t)tier;
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
    uint8_t rgba[4] = {138u, 231u, 255u, 255u}; /* terrain: bright cyan */
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
    if (state->last_terrain_tick != 0u && now_tick < state->last_terrain_tick + 600u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BROADCAST_SKIP] reason=cooldown key=%u", (unsigned)key);
        return false;
    }
    count = snprintf(message, sizeof(message), "地形规则：已进入%s·%s%s，当前区域规则已生效",
                     terrain_depth_name(terrain.depth), terrain_biome_name(terrain.biome),
                     special[0] != '\0' ? special : "");
    if (count < 0 || count >= (int)sizeof(message)) return false;
    if (!wrap_chat_color(message, sizeof(message), channel_hex("terrain"))) return false;
    string_handle = patchlib_string_create(message);
    if (!string_handle) return false;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COLOR] channel=terrain rgba=%u,%u,%u,%u",
           (unsigned)rgba[0], (unsigned)rgba[1], (unsigned)rgba[2],
           (unsigned)rgba[3]);
    if (!invoke_new_text(runtime, string_handle, rgba, force_display,
                         &ignored_return)) {
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
    uint8_t rgba[4] = {255u, 196u, 119u, 255u}; /* weather: soft amber */
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
    if (state->last_world_key == key && state->last_world_tick != 0u) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=duplicate key=%u", (unsigned)key);
        return false;
    }
    if (state->last_world_tick != 0u && now_tick < state->last_world_tick + 120u) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=cooldown key=%u", (unsigned)key);
        return false;
    }
    weather_name = weather_name_zh(weather);
    count = snprintf(message, sizeof(message), "起源律动：%s·%s",
                     is_night ? "夜晚" : "白昼", weather_name);
    if (count < 0 || count >= (int)sizeof(message)) return false;
    if (!wrap_chat_color(message, sizeof(message), channel_hex("weather"))) return false;
    string_handle = patchlib_string_create(message);
    if (!string_handle) return false;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COLOR] channel=weather rgba=%u,%u,%u,%u",
           (unsigned)rgba[0], (unsigned)rgba[1], (unsigned)rgba[2],
           (unsigned)rgba[3]);
    if (!invoke_new_text(runtime, string_handle, rgba, force_display,
                         &ignored_return)) {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[WORLD_BROADCAST_SKIP] reason=invoke_failed key=%u", (unsigned)key);
        return false;
    }
    state->last_world_key = key;
    state->last_world_tick = now_tick;
    state->snapshot_revision = snapshot_revision;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[WORLD_BROADCAST_COMMIT] night=%s weather=%s key=%u revision=%llu",
           is_night ? "yes" : "no", weather_name, (unsigned)key,
           (unsigned long long)snapshot_revision);
    return true;
}

bool or_broadcast_emit_rule_summary(OR_BroadcastState *state,
                                    const OR_Runtime *runtime,
                                    const OR_RuleSnapshot *snapshot,
                                    uint64_t rule_revision,
                                    uint64_t now_tick) {
    (void)now_tick;
    char message[256];
    patch_handle_t text;
    uint64_t ignored = 0u;
    bool force = true;
    uint8_t rgba[4] = {255u, 224u, 138u, 255u}; /* world rules: pale gold */
    size_t i;
    int used;
    if (!state || !runtime || !snapshot || !snapshot->selected_count ||
        !runtime->capabilities.new_text_ready || !runtime->method_main_new_text ||
        !patchlib_string_create || !patchlib_method_invoke_args) return false;
    {
        uint64_t key = ((uint64_t)snapshot->active_mask << 32) |
                       ((uint64_t)snapshot->progress << 24) |
                       (rule_revision & UINT64_C(0xFFFFFF));
        if (state->last_rule_summary_key == key) return false;
    }
    used = snprintf(message, sizeof(message), "起源规则：%s；本轮规则：",
                    progress_stage_name_zh(snapshot->progress));
    if (used < 0 || used >= (int)sizeof(message)) return false;
    for (i = 0u; i < snapshot->selected_count && i < OR_MAX_WORLD_RULES; ++i) {
        size_t len = strlen(message);
        int n = snprintf(message + len, sizeof(message) - len, "%s%s",
                         i == 0u ? "" : "、", world_rule_name_zh(snapshot->selected_ids[i]));
        if (n < 0 || (size_t)n >= sizeof(message) - len) return false;
    }
    if (!wrap_chat_color(message, sizeof(message), channel_hex("world_rule"))) return false;
    text = patchlib_string_create(message); if (!text) return false;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BROADCAST_COLOR] channel=world_rule rgba=%u,%u,%u,%u",
           (unsigned)rgba[0], (unsigned)rgba[1], (unsigned)rgba[2],
           (unsigned)rgba[3]);
    if (!invoke_new_text(runtime, text, rgba, force, &ignored)) return false;
    state->last_rule_summary_key = ((uint64_t)snapshot->active_mask << 32) |
                                   ((uint64_t)snapshot->progress << 24) |
                                   (rule_revision & UINT64_C(0xFFFFFF));
    OR_LOG(MOD_LOG_LEVEL_INFO, "[RULE_SUMMARY_BROADCAST] stage=%s revision=%llu rules=%u",
           progress_stage_name_zh(snapshot->progress), (unsigned long long)rule_revision,
           (unsigned)snapshot->selected_count);
    return true;
}

bool or_broadcast_emit_boss_dialog(OR_BroadcastState *state, const OR_Runtime *runtime,
                                   uint32_t npc_type, OR_BossDialogEvent event, uint64_t now_tick) {
    char message[160]; patch_handle_t text; uint64_t ignored=0; bool force=true;
    uint8_t rgba[4]={255u,156u,168u,255u};
    if (!state || !runtime || !runtime->capabilities.new_text_ready ||
        (runtime->main_new_text_arg_count != 1 && runtime->main_new_text_arg_count != 3) || !patchlib_string_create || !patchlib_method_invoke_args) return false;
    if (state->last_emit_tick && now_tick < state->last_emit_tick + 120u) return false;
    if (event != OR_BOSS_DIALOG_SPAWN && event != OR_BOSS_DIALOG_HALF && event != OR_BOSS_DIALOG_DEATH) return false;
    snprintf(message,sizeof(message), event == OR_BOSS_DIALOG_HALF ? "【首领回响】目标 #%u 的防线正在瓦解。" : (event == OR_BOSS_DIALOG_DEATH ? "【首领回响】目标 #%u 的回响已归于寂静。" : "【首领回响】目标 #%u 已被起源律动锁定。"), (unsigned)npc_type);
    if (!wrap_chat_color(message, sizeof(message), channel_hex("boss"))) return false;
    text=patchlib_string_create(message); if (!text) return false;
    OR_LOG(MOD_LOG_LEVEL_INFO,"[BROADCAST_COLOR] channel=boss rgba=%u,%u,%u,%u",
           (unsigned)rgba[0],(unsigned)rgba[1],(unsigned)rgba[2],(unsigned)rgba[3]);
    if (!invoke_new_text(runtime, text, rgba, force, &ignored)) return false;
    state->last_emit_tick=now_tick; OR_LOG(MOD_LOG_LEVEL_INFO,"[BOSS_DIALOG] type=%u event=%s",(unsigned)npc_type,event == OR_BOSS_DIALOG_HALF ? "half" : (event == OR_BOSS_DIALOG_DEATH ? "death" : "spawn")); return true;
}
