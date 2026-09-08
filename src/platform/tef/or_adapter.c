#include "or_adapter.h"

#include "or_log.h"
#include "or_ai.h"
#include "or_bnm_bridge.h"
#include "or_loot.h"
#include "or_spawn.h"
#include "or_broadcast.h"
#include "or_death_probe.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"
#include "tefkernel/patchlib/struct/array.h"

#include <limits.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
#include <android/log.h>
#endif

#if !defined(__ANDROID__)
extern void *(*patchlib_field_get_pointer)(patch_handle_t field,
                                           void *instance);
#endif

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

#define OR_DIAGNOSTIC_LOG_LIMIT 256u
#define OR_STAT_DIAGNOSTIC_LOG_LIMIT 256u
#define OR_AI_DIAGNOSTIC_SAMPLE_LIMIT 8u
#define OR_LIFECYCLE_HEALTH_INTERVAL 1800u
#define OR_PENDING_STALE_TICKS 600u
#define OR_COLOR_PROBE_LIMIT 16u
#define OR_VISUAL_MEMBER_LIMIT 64u
#define OR_VISUAL_METHOD_ARG_LIMIT 8u
#define ORIGINREWRITE_ENABLE_LIVE_REWARD 0

#define OR_DIAG_LOG(...) \
    do { \
        if (g_adapter.diagnostic_log_count < OR_DIAGNOSTIC_LOG_LIMIT) { \
            ++g_adapter.diagnostic_log_count; \
            or_log_write(MOD_LOG_LEVEL_WARNING, "[OR_DIAG] " __VA_ARGS__); \
        } \
    } while (0)

#define OR_STAT_DIAG_LOG(...) \
    do { \
        if (g_adapter.stat_diagnostic_log_count < OR_STAT_DIAGNOSTIC_LOG_LIMIT) { \
            ++g_adapter.stat_diagnostic_log_count; \
            or_log_write(MOD_LOG_LEVEL_WARNING, "[OR_DIAG] " __VA_ARGS__); \
        } \
    } while (0)

typedef struct OR_NativeBinding {
    bool occupied;
    bool roll_resolved;
    bool elite;
    bool pending;
    bool loot_seen;
    bool death_started;
    patch_handle_t instance;
    OR_InstanceKey key;
    OR_VanillaStats pending_vanilla;
    uint32_t pending_npc_type;
    bool pending_is_boss;
    bool pending_is_town;
    bool pending_is_friendly;
    OR_AiRuntimeState ai_runtime;
    uint64_t ai_ticks;
    uint64_t last_seen_tick;
    float previous_life_ratio;
} OR_NativeBinding;

typedef struct OR_Adapter {
    OR_Runtime *runtime;
    OR_Config *config;
    OR_StateStore *state;
    OR_NativeBinding bindings[OR_MAX_TRACKED_NPCS];
    uint64_t fallback_tick;
    uint32_t diagnostic_log_count;
    uint32_t stat_diagnostic_log_count;
    uint32_t diagnostic_callback_count;
    uint32_t diagnostic_ai_callback_count;
    uint64_t setdefaults_total;
    uint64_t ai_callback_total;
    uint64_t commit_total;
    uint64_t binding_reclaim_total;
    uint64_t binding_full_total;
    uint64_t pending_clear_total;
    uint64_t elite_clear_total;
    uint64_t lifecycle_reuse_total;
    uint64_t stale_pending_clear_total;
    uint64_t batch_depth_counts[OR_DEPTH_COUNT];
    uint64_t batch_archetype_counts[6];
    uint64_t last_health_tick;
    uint32_t color_probe_count;
    bool installed;
    OR_BroadcastState broadcast;
} OR_Adapter;

static OR_Adapter g_adapter;
static uint32_t g_batch_drop_index;

static bool host_authority(bool *single_player, bool *known);
static uint64_t update_tick(void);
static const char *biome_name(OR_BiomeTag biome);
static const char *special_name(OR_SpecialLocationTag special);

static void release_adapter_handle(patch_handle_t handle) {
    if (!handle) return;
#if defined(__ANDROID__)
    (void)handle;
#else
    if (patchlib_free) patchlib_free(handle);
#endif
}

static bool visual_member_name(const char *name) {
    static const char *const tokens[] = {
        "color", "draw", "render", "alpha", "glow", "effect", "trail"
    };
    size_t i;
    size_t j;
    char lowered[128];
    if (!name) return false;
    for (i = 0u; i + 1u < sizeof(lowered) && name[i] != '\0'; ++i) {
        lowered[i] = (char)tolower((unsigned char)name[i]);
    }
    lowered[i] = '\0';
    for (j = 0u; j < sizeof(tokens) / sizeof(tokens[0]); ++j) {
        if (strstr(lowered, tokens[j]) != NULL) return true;
    }
    return false;
}

static void log_visual_method_signature(patch_handle_t method, const char *name) {
    patch_method_signature_t signature;
    size_t arg_count;
    size_t i;
    if (!method || !name || !patchlib_method_get_signature ||
        !tefstd_vector_size || !tefstd_vector_at) return;
    memset(&signature, 0, sizeof(signature));
    if (!patchlib_method_get_signature(method, &signature)) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[VISUAL_METHOD_SIG] name=%s signature=unavailable", name);
        return;
    }
    arg_count = tefstd_vector_size(&signature.arg_types);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[VISUAL_METHOD_SIG] name=%s instance=%s returnType=%d argCount=%zu",
           name, signature.is_instance ? "yes" : "no",
           (int)signature.return_type, arg_count);
    for (i = 0u; i < arg_count && i < OR_VISUAL_METHOD_ARG_LIMIT; ++i) {
        patch_type_t *arg_type = (patch_type_t *)tefstd_vector_at(
            &signature.arg_types, i);
        if (arg_type) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[VISUAL_METHOD_ARG] name=%s index=%zu type=%d",
                   name, i, (int)*arg_type);
        }
    }
    if (arg_count > OR_VISUAL_METHOD_ARG_LIMIT) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[VISUAL_METHOD_ARG] name=%s truncated=yes limit=%u",
               name, (unsigned)OR_VISUAL_METHOD_ARG_LIMIT);
    }
    if (patchlib_method_signature_free) {
        (void)patchlib_method_signature_free(&signature);
    }
}

static void scan_visual_members(OR_Runtime *runtime) {
    tefstd_vector_t entries = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!runtime || !runtime->npc_type || !tefstd_vector_init ||
        !tefstd_vector_size || !tefstd_vector_at || !tefstd_vector_destroy) return;

    if (patchlib_type_get_fields && patchlib_field_get_name &&
        patchlib_field_get_size && patchlib_field_get_type &&
        tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_fields(runtime->npc_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) &&
             logged < OR_VISUAL_MEMBER_LIMIT; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t field = entry ? *entry : PATCH_NULL;
            const char *name = field ? patchlib_field_get_name(field) : NULL;
            if (visual_member_name(name)) {
                OR_LOG(MOD_LOG_LEVEL_INFO,
                       "[VISUAL_MEMBER] kind=field name=%s size=%zu type=%d",
                       name, patchlib_field_get_size(field),
                       (int)patchlib_field_get_type(field));
                logged += 1u;
            }
        }
        tefstd_vector_destroy(&entries);
    } else {
        tefstd_vector_destroy(&entries);
    }

    if (patchlib_type_get_properties && patchlib_property_get_name &&
        patchlib_property_get_get_method && patchlib_property_get_set_method &&
        tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_properties(runtime->npc_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) &&
             logged < OR_VISUAL_MEMBER_LIMIT; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t property = entry ? *entry : PATCH_NULL;
            const char *name = property ? patchlib_property_get_name(property) : NULL;
            patch_handle_t getter = property ? patchlib_property_get_get_method(property) : PATCH_NULL;
            patch_handle_t setter = property ? patchlib_property_get_set_method(property) : PATCH_NULL;
            if (visual_member_name(name)) {
                OR_LOG(MOD_LOG_LEVEL_INFO,
                       "[VISUAL_MEMBER] kind=property name=%s getter=%s setter=%s",
                       name, getter ? "available" : "unavailable",
                       setter ? "available" : "unavailable");
                logged += 1u;
            }
        }
        tefstd_vector_destroy(&entries);
    } else {
        tefstd_vector_destroy(&entries);
    }

    if (patchlib_type_get_methods && patchlib_method_get_name &&
        tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_methods(runtime->npc_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) &&
             logged < OR_VISUAL_MEMBER_LIMIT; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t method = entry ? *entry : PATCH_NULL;
            const char *name = method ? patchlib_method_get_name(method) : NULL;
            if (visual_member_name(name)) {
                OR_LOG(MOD_LOG_LEVEL_INFO,
                       "[VISUAL_MEMBER] kind=method name=%s handle=%p",
                       name, (void *)method);
                log_visual_method_signature(method, name);
                logged += 1u;
            }
        }
        tefstd_vector_destroy(&entries);
    } else {
        tefstd_vector_destroy(&entries);
    }
    OR_LOG(MOD_LOG_LEVEL_INFO, "[VISUAL_SCAN] logged=%u limit=%u",
           (unsigned)logged, (unsigned)OR_VISUAL_MEMBER_LIMIT);
}

static bool handle_valid(patch_handle_t handle) {
    if (!handle) return false;
    return !patchlib_is_valid || patchlib_is_valid(handle);
}

static bool field_read(patch_handle_t field, patch_handle_t instance, void *out) {
    if (!handle_valid(field) || !out || !patchlib_field_get_value) return false;
#if defined(__ANDROID__)
    if (!instance && patchlib_field_is_static && patchlib_field_is_static(field) &&
        patchlib_field_is_const && patchlib_field_is_const(field)) {
        patchlib_field_get_value(field, NULL, out);
        return true;
    }
    if (!instance && patchlib_field_is_static && patchlib_field_is_static(field) &&
        patchlib_field_is_thread_static && patchlib_field_is_thread_static(field)) {
        patchlib_field_get_value(field, NULL, out);
        return true;
    }
    if (!instance && patchlib_field_is_static && patchlib_field_is_static(field) &&
        patchlib_field_get_pointer && patchlib_field_get_size) {
        void *raw = patchlib_field_get_pointer(field, NULL);
        size_t size = patchlib_field_get_size(field);
        if (raw && size != 0u) {
            memcpy(out, raw, size);
            return true;
        }
    }
#endif
    patchlib_field_get_value(field, instance, out);
    return true;
}

static bool field_write(patch_handle_t field, patch_handle_t instance, void *value) {
    if (!handle_valid(field) || !value || !patchlib_field_set_value) return false;
#if defined(__ANDROID__)
    if (!instance && patchlib_field_is_static && patchlib_field_is_static(field) &&
        patchlib_field_is_const && patchlib_field_is_const(field)) {
        return false;
    }
    if (!instance && patchlib_field_is_static && patchlib_field_is_static(field) &&
        patchlib_field_is_thread_static && patchlib_field_is_thread_static(field)) {
        return false;
    }
    if (!instance && patchlib_field_is_static && patchlib_field_is_static(field) &&
        patchlib_field_get_pointer && patchlib_field_get_size) {
        void *raw = patchlib_field_get_pointer(field, NULL);
        size_t size = patchlib_field_get_size(field);
        if (raw && size != 0u) {
            memcpy(raw, value, size);
            return true;
        }
    }
#endif
    patchlib_field_set_value(field, instance, value);
    return true;
}

static bool read_i32(patch_handle_t field, patch_handle_t instance, int32_t *out) {
    if (!out || !handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_INT32) return false;
    return field_read(field, instance, out);
}

static bool read_bool(patch_handle_t field, patch_handle_t instance, bool *out) {
    if (!out || !handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_BOOL) return false;
    return field_read(field, instance, out);
}

static bool read_float(patch_handle_t field, patch_handle_t instance, float *out) {
    if (!out || !handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_FLOAT) return false;
    return field_read(field, instance, out);
}

static bool read_double(patch_handle_t field, patch_handle_t instance, double *out) {
    if (!out || !handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_DOUBLE) return false;
    return field_read(field, instance, out);
}

static bool read_position_raw(patch_handle_t field,
                              patch_handle_t instance,
                              unsigned char raw[8]) {
    if (!field || !instance || !raw || !patchlib_field_get_value) return false;
    memset(raw, 0, 8u);
    patchlib_field_get_value(field, instance, raw);
    return true;
}

static bool read_static_i32_method(patch_handle_t method, int32_t *out) {
    if (!method || !out || !patchlib_method_invoke_args) return false;
    *out = 0;
    return patchlib_method_invoke_args(method, PATCH_NULL, out, NULL);
}

static bool read_player_bool_property(patch_handle_t getter,
                                      patch_handle_t player,
                                      bool *out) {
    if (!getter || !player || !out || !patchlib_method_invoke_args) return false;
    *out = false;
    return patchlib_method_invoke_args(getter, player, out, NULL);
}

static bool read_player_biome_probe(patch_handle_t getter,
                                    patch_handle_t player,
                                    bool *out) {
    return read_player_bool_property(getter, player, out);
}

static bool capture_player_biome(OR_TerrainSnapshot *terrain,
                                 uint64_t tick,
                                 bool log_context) {
    static OR_TerrainSnapshot cached = {
        OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE
    };
    static uint64_t cached_tick;
    static bool cache_valid;
    static patch_handle_t last_player;
    static patch_handle_t last_local_player;
    static patch_handle_t last_player_source;
    static bool last_local_player_ok;
    static uint64_t last_player_context_tick;
    static OR_TerrainSnapshot last_logged = {
        OR_DEPTH_COUNT, OR_BIOME_COUNT, OR_SPECIAL_COUNT
    };
    patch_handle_t player = PATCH_NULL;
    bool local_player_ok;
    bool field_player_ok = false;
    patch_handle_t player_array = PATCH_NULL;
    patch_handle_t array_player = PATCH_NULL;
    int32_t my_player = -1;
    bool my_player_ok = false;
    size_t player_array_length = 0u;
    unsigned char player_position_raw[8] = {0};
    bool player_position_ok = false;
    unsigned char player_array_raw[8] = {0};
    bool corrupt = false;
    bool crimson = false;
    bool hallow = false;
    bool jungle = false;
    bool snow = false;
    bool desert = false;
    bool beach = false;
    bool glowshroom = false;
    bool shopping_forest = false;
    bool shopping_any_biome = false;
    bool corrupt_ok;
    bool crimson_ok;
    bool hallow_ok;
    bool jungle_ok;
    bool snow_ok;
    bool desert_ok;
    bool beach_ok;
    OR_TerrainSnapshot next = {
        OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE
    };
    if (!terrain || !g_adapter.runtime || !g_adapter.runtime->main_local_player_get ||
        !patchlib_method_invoke_args || !handle_valid(g_adapter.runtime->main_local_player_get)) {
        return false;
    }
    if (cache_valid && tick >= cached_tick && tick - cached_tick < 45u) {
        terrain->biome = cached.biome;
        terrain->special = cached.special;
        return true;
    }
    local_player_ok = patchlib_method_invoke_args(
        g_adapter.runtime->main_local_player_get, PATCH_NULL, &player, NULL);
    if (g_adapter.runtime->main_player_field_probe && patchlib_array_at &&
        patchlib_array_length && g_adapter.runtime->main_my_player_field_probe) {
        (void)field_read(g_adapter.runtime->main_player_field_probe, NULL,
                         player_array_raw);
        memcpy(&player_array, player_array_raw, sizeof(player_array));
        my_player_ok = field_read(g_adapter.runtime->main_my_player_field_probe,
                                  NULL, &my_player);
        if (handle_valid(player_array)) {
            player_array_length = patchlib_array_length(player_array);
        }
        if (handle_valid(player_array) && my_player_ok && my_player >= 0 &&
            (size_t)my_player < player_array_length) {
            field_player_ok = patchlib_array_at(player_array, (size_t)my_player,
                                                &array_player) && handle_valid(array_player);
        }
        if (field_player_ok) player_array = array_player;
    }
    if (!local_player_ok || !handle_valid(player)) {
        player = field_player_ok ? player_array : PATCH_NULL;
        if (player && (last_player_source != player || last_local_player != PATCH_NULL ||
                       last_local_player_ok != local_player_ok)) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[PLAYER_SOURCE_VALUE] source=Main.player[%d] arrayRead=ok objectHandle=%p use=enabled",
                   (int)my_player, (void *)player);
        }
    }
    if (!player) {
        if (local_player_ok != last_local_player_ok ||
            last_local_player != player || last_player_source != PATCH_NULL) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[PLAYER_SOURCE_VALUE] source=none localPlayerInvoke=%s arrayRead=%s myPlayer=%d use=disabled",
                   local_player_ok ? "ok" : "fail",
                   field_player_ok ? "ok" : "fail", (int)my_player);
        }
        last_local_player = player;
        last_local_player_ok = local_player_ok;
        last_player_source = PATCH_NULL;
        return false;
    }
    if (g_adapter.runtime->player_position_field_probe) {
        player_position_ok = read_position_raw(
            g_adapter.runtime->player_position_field_probe, player,
            player_position_raw);
    }
    if (log_context || last_player_context_tick == 0u ||
        tick >= last_player_context_tick + 1800u) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[PLAYER_CONTEXT] arrayLength=%zu myPlayer=%d player=%p positionRead=%s "
               "positionRaw=%02x%02x%02x%02x%02x%02x%02x%02x samplePeriod=1800",
               player_array_length, (int)my_player, (void *)player,
               player_position_ok ? "ok" : "unavailable",
               player_position_raw[0], player_position_raw[1], player_position_raw[2],
               player_position_raw[3], player_position_raw[4], player_position_raw[5],
               player_position_raw[6], player_position_raw[7]);
        last_player_context_tick = tick;
    }
    corrupt_ok = read_player_biome_probe(g_adapter.runtime->player_zone_corrupt_get,
                                         player, &corrupt);
    crimson_ok = read_player_biome_probe(g_adapter.runtime->player_zone_crimson_get,
                                         player, &crimson);
    hallow_ok = read_player_biome_probe(g_adapter.runtime->player_zone_hallow_get,
                                        player, &hallow);
    jungle_ok = read_player_biome_probe(g_adapter.runtime->player_zone_jungle_get,
                                        player, &jungle);
    snow_ok = read_player_biome_probe(g_adapter.runtime->player_zone_snow_get,
                                      player, &snow);
    desert_ok = read_player_biome_probe(g_adapter.runtime->player_zone_desert_get,
                                        player, &desert);
    beach_ok = read_player_biome_probe(g_adapter.runtime->player_zone_beach_get,
                                       player, &beach);
    (void)read_player_biome_probe(g_adapter.runtime->player_zone_glowshroom_get,
                                  player, &glowshroom);
    (void)read_player_biome_probe(g_adapter.runtime->player_shopping_forest_get,
                                  player, &shopping_forest);
    (void)read_player_biome_probe(g_adapter.runtime->player_shopping_any_biome_get,
                                  player, &shopping_any_biome);
    if (crimson) next.biome = OR_BIOME_CRIMSON;
    else if (corrupt) next.biome = OR_BIOME_CORRUPTION;
    else if (hallow) next.biome = OR_BIOME_HALLOW;
    else if (jungle) next.biome = OR_BIOME_JUNGLE;
    else if (snow) next.biome = OR_BIOME_SNOW;
    else if (desert) next.biome = OR_BIOME_DESERT;
    if (beach) next.special = OR_SPECIAL_OCEAN;
    cached.biome = next.biome;
    cached.special = next.special;
    cached_tick = tick;
    cache_valid = true;
    terrain->biome = next.biome;
    terrain->special = next.special;
    if (log_context || last_player != player ||
        last_logged.biome != next.biome || last_logged.special != next.special) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BIOME_PROBE_RESULT] player=%p changed=%s "
               "corrupt=%s:%s crimson=%s:%s hallow=%s:%s jungle=%s:%s "
               "snow=%s:%s desert=%s:%s beach=%s:%s",
               (void *)player, last_player != player ? "yes" : "no",
               corrupt_ok ? "ok" : "fail", corrupt ? "yes" : "no",
               crimson_ok ? "ok" : "fail", crimson ? "yes" : "no",
               hallow_ok ? "ok" : "fail", hallow ? "yes" : "no",
               jungle_ok ? "ok" : "fail", jungle ? "yes" : "no",
               snow_ok ? "ok" : "fail", snow ? "yes" : "no",
               desert_ok ? "ok" : "fail", desert ? "yes" : "no",
               beach_ok ? "ok" : "fail", beach ? "yes" : "no");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BIOME_CONTEXT] read=ok cacheTicks=45 corrupt=%s crimson=%s "
               "hallow=%s jungle=%s snow=%s desert=%s beach=%s biome=%s special=%s",
               corrupt ? "yes" : "no", crimson ? "yes" : "no",
               hallow ? "yes" : "no", jungle ? "yes" : "no",
               snow ? "yes" : "no", desert ? "yes" : "no",
               beach ? "yes" : "no", biome_name(next.biome),
               special_name(next.special));
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[BIOME_CROSSCHECK] zoneJungle=%s zoneGlowshroom=%s "
               "shoppingForest=%s shoppingAnyBiome=%s",
               jungle ? "yes" : "no", glowshroom ? "yes" : "no",
               shopping_forest ? "yes" : "no", shopping_any_biome ? "yes" : "no");
    }
    last_player = player;
    last_player_source = player;
    last_local_player = local_player_ok ? player : PATCH_NULL;
    last_local_player_ok = local_player_ok;
    last_logged = next;
    return true;
}

static const char *terrain_depth_name(OR_DepthTag depth) {
    switch (depth) {
        case OR_DEPTH_SURFACE: return "surface";
        case OR_DEPTH_UNDERGROUND: return "underground";
        case OR_DEPTH_CAVERN: return "cavern";
        case OR_DEPTH_UNDERWORLD: return "underworld";
        default: return "unknown";
    }
}

static const char *biome_name(OR_BiomeTag biome) {
    static const char *const names[] = {
        "forest", "desert", "snow", "jungle", "hallow", "corruption", "crimson"
    };
    return biome < OR_BIOME_COUNT ? names[biome] : "forest";
}

static const char *special_name(OR_SpecialLocationTag special) {
    static const char *const names[] = {
        "none", "ocean", "dungeon", "mushroom", "sky"
    };
    return special < OR_SPECIAL_COUNT ? names[special] : "none";
}

static const char *ai_template_name(OR_AiTemplate template_id) {
    switch (template_id) {
        case OR_AI_TEMPLATE_LUNGE: return "lunge";
        case OR_AI_TEMPLATE_PROJECTILE_BURST: return "projectile_burst";
        case OR_AI_TEMPLATE_BURROW: return "burrow";
        case OR_AI_TEMPLATE_DASH: return "dash";
        case OR_AI_TEMPLATE_FAN_SHOT: return "fan_shot";
        case OR_AI_TEMPLATE_PHASE: return "phase";
        case OR_AI_TEMPLATE_SUMMON: return "summon";
        case OR_AI_TEMPLATE_RAGE: return "rage";
        case OR_AI_TEMPLATE_NONE:
        default: return "none";
    }
}

static bool decode_position_depth(const unsigned char raw[8],
                                  double world_surface,
                                  double rock_layer,
                                  int32_t underworld_layer,
                                  float top_world,
                                  float bottom_world,
                                  OR_DepthTag *out_depth,
                                  float *out_x,
                                  float *out_y) {
    float x;
    float y;
    double surface_px;
    double rock_px;
    double underworld_px;
    if (!raw || !out_depth || !out_x || !out_y || !isfinite(world_surface) ||
        !isfinite(rock_layer) || !isfinite(top_world) || !isfinite(bottom_world) ||
        world_surface <= 0.0 || rock_layer <= world_surface ||
        underworld_layer <= 0 || bottom_world <= top_world) return false;
    memcpy(&x, raw, sizeof(x));
    memcpy(&y, raw + sizeof(x), sizeof(y));
    surface_px = world_surface * 16.0;
    rock_px = rock_layer * 16.0;
    underworld_px = (double)underworld_layer * 16.0;
    if (!isfinite(x) || !isfinite(y) || y < (float)top_world ||
        y > (float)bottom_world || underworld_px <= rock_px ||
        underworld_px > (double)bottom_world) return false;
    if ((double)y < surface_px) {
        *out_depth = OR_DEPTH_SURFACE;
    } else if ((double)y < rock_px) {
        *out_depth = OR_DEPTH_UNDERGROUND;
    } else if ((double)y < underworld_px) {
        *out_depth = OR_DEPTH_CAVERN;
    } else {
        *out_depth = OR_DEPTH_UNDERWORLD;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

static bool read_u64(patch_handle_t field, patch_handle_t instance, uint64_t *out) {
    patch_type_t type;
    int64_t signed_value;
    int32_t i32;
    if (!out || !handle_valid(field) || !patchlib_field_get_type) return false;
    type = patchlib_field_get_type(field);
    if (type == PATCH_UINT64) return field_read(field, instance, out);
    if (type == PATCH_INT64) {
        signed_value = 0;
        if (!field_read(field, instance, &signed_value)) return false;
        *out = signed_value > 0 ? (uint64_t)signed_value : 0u;
        return true;
    }
    if (type == PATCH_INT32) {
        i32 = 0;
        if (!field_read(field, instance, &i32)) return false;
        *out = i32 > 0 ? (uint64_t)i32 : 0u;
        return true;
    }
    return false;
}

static OR_NativeBinding *find_binding(patch_handle_t instance) {
    size_t i;
    if (!instance) return NULL;
    for (i = 0; i < OR_MAX_TRACKED_NPCS; ++i) {
        if (g_adapter.bindings[i].occupied && g_adapter.bindings[i].instance == instance) {
            return &g_adapter.bindings[i];
        }
    }
    return NULL;
}

static void clear_binding(OR_NativeBinding *binding);

static OR_NativeBinding *get_or_create_binding(patch_handle_t instance) {
    OR_NativeBinding *binding = find_binding(instance);
    size_t i;
    if (binding) return binding;
    for (i = 0; i < OR_MAX_TRACKED_NPCS; ++i) {
        if (!g_adapter.bindings[i].occupied) {
            memset(&g_adapter.bindings[i], 0, sizeof(g_adapter.bindings[i]));
            g_adapter.bindings[i].occupied = true;
            g_adapter.bindings[i].instance = instance;
            or_ai_runtime_init(&g_adapter.bindings[i].ai_runtime);
            return &g_adapter.bindings[i];
        }
    }
    /* Pending baselines are disposable observations. Reclaim them when the
     * table is full; only a committed live elite is protected. */
    for (i = 0; i < OR_MAX_TRACKED_NPCS; ++i) {
        if (g_adapter.bindings[i].occupied &&
            !g_adapter.bindings[i].elite) {
            clear_binding(&g_adapter.bindings[i]);
            g_adapter.binding_reclaim_total += 1u;
            memset(&g_adapter.bindings[i], 0, sizeof(g_adapter.bindings[i]));
            g_adapter.bindings[i].occupied = true;
            g_adapter.bindings[i].instance = instance;
            or_ai_runtime_init(&g_adapter.bindings[i].ai_runtime);
            OR_DIAG_LOG("binding_reclaimed slot=%u", (unsigned)i);
            return &g_adapter.bindings[i];
        }
    }
    g_adapter.binding_full_total += 1u;
    OR_DIAG_LOG("binding_table_full instance=%p", (void *)instance);
    return NULL;
}

static void clear_binding(OR_NativeBinding *binding) {
    if (!binding) return;
    if (binding->pending) g_adapter.pending_clear_total += 1u;
    if (binding->elite) g_adapter.elite_clear_total += 1u;
    if (binding->elite && g_adapter.state) {
        (void)or_state_cleanup(g_adapter.state, binding->key);
    }
    memset(binding, 0, sizeof(*binding));
}

void or_adapter_observe_death_state(patch_handle_t instance,
                                    int32_t npc_type,
                                    int32_t life,
                                    bool active,
                                    int32_t strike_result) {
    OR_NativeBinding *binding;
    if (!instance || !g_adapter.installed || !g_adapter.state ||
        (life > 0 && active)) return;
    binding = find_binding(instance);
    if (!binding || !binding->elite || binding->death_started) return;
    binding->death_started = true;
    if (or_state_mark_death(g_adapter.state, binding->key)) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[DEATH_STARTED_OBSERVE] type=%d life=%d active=%s result=%d "
               "idempotent=claimed=yes rewards=off lootHook=off",
               (int)npc_type, (int)life, active ? "true" : "false",
               (int)strike_result);
    } else {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[DEATH_STARTED_OBSERVE] type=%d rejected=state_not_live "
               "rewards=off lootHook=off",
               (int)npc_type);
    }
}

void or_adapter_observe_loot_boundary(patch_handle_t instance) {
    OR_NativeBinding *binding;
    const OR_EliteRecord *record = NULL;
    OR_LootContext context;
    OR_LootResult policy;
    bool single_player = false;
    bool authority;
    bool prior_death_started;
    bool claimed = false;
    static const struct { const char *name; int16_t net_id; } drop_ids[] = {
        {"Gel", 23}, {"Torch", 8}, {"Wood", 9},
        {"HealingPotion", 28}, {"CopperCoin", 71}
    };
    if (!instance || !g_adapter.installed || !g_adapter.state) return;
    binding = find_binding(instance);
    if (!binding || !binding->elite || binding->loot_seen) return;
    binding->loot_seen = true;
    or_runtime_probe_main_item_array(g_adapter.runtime);
    {
        unsigned char position_raw[8] = {0};
        int32_t width = 0;
        int32_t height = 0;
        bool position_ok = read_position_raw(
            g_adapter.runtime ? g_adapter.runtime->field_position_probe : PATCH_NULL,
            instance, position_raw);
        bool width_ok = read_i32(
            g_adapter.runtime ? g_adapter.runtime->field_width : PATCH_NULL,
            instance, &width);
        bool height_ok = read_i32(
            g_adapter.runtime ? g_adapter.runtime->field_height : PATCH_NULL,
            instance, &height);
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[LOOT_POSITION_DIAGNOSTIC] position=%s raw=%02x%02x%02x%02x%02x%02x%02x%02x "
               "width=%s:%d height=%s:%d interpretation=deferred",
               position_ok ? "ok" : "unavailable",
               position_raw[0], position_raw[1], position_raw[2], position_raw[3],
               position_raw[4], position_raw[5], position_raw[6], position_raw[7],
               width_ok ? "ok" : "unavailable", width,
               height_ok ? "ok" : "unavailable", height);
    }
    if (g_adapter.runtime && g_adapter.runtime->method_item_id_from_net_id &&
        patchlib_method_invoke_args) {
        const size_t drop_index = (size_t)(g_batch_drop_index %
                                            (sizeof(drop_ids) / sizeof(drop_ids[0])));
        int16_t net_id = drop_ids[drop_index].net_id;
        int16_t resolved_id = -1;
        void *args[1] = {&net_id};
        bool invoke_ok = patchlib_method_invoke_args(
            g_adapter.runtime->method_item_id_from_net_id, PATCH_NULL,
            &resolved_id, args);
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_ID_RUNTIME_DIAGNOSTIC] method=FromNetId input=%d invoke=%s return=%d "
               "use=diagnostic_only newItem=disabled",
               (int)net_id, invoke_ok ? "ok" : "failed", (int)resolved_id);
    }
    record = or_state_find_const(g_adapter.state, binding->key);
    prior_death_started = record && record->lifecycle == OR_LIFECYCLE_DEATH_STARTED;
    if (record && !prior_death_started) {
        claimed = or_state_mark_death(g_adapter.state, binding->key);
        record = or_state_find_const(g_adapter.state, binding->key);
    }
    if (prior_death_started || claimed) binding->death_started = true;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[LOOT_BOUNDARY_OBSERVE] entered=yes priorDeathStarted=%s "
           "deathStartedNow=%s source=NPCLootPostfix state=%s "
           "originalLootPreserved=yes rewards=off extraLoot=off tier=%s "
           "itemType=%s newItemSig=%s itemIdType=%s itemIdStaticInts=%u "
           "itemIdLookupSig=%s",
           prior_death_started ? "yes" : "no",
           (prior_death_started || claimed) ? "yes" : "no",
           record ? "tracked" : "missing",
           record ? or_elite_tier_name(record->tier) : "none",
           g_adapter.runtime && g_adapter.runtime->item_type ? "resolved" : "unavailable",
           g_adapter.runtime && g_adapter.runtime->item_new_item_signature_ready ? "ready" : "unavailable",
           g_adapter.runtime && g_adapter.runtime->item_id_type_resolved ? "resolved" : "unavailable",
           g_adapter.runtime ? (unsigned)g_adapter.runtime->item_id_static_int_count : 0u,
           g_adapter.runtime && g_adapter.runtime->item_id_lookup_signature_ready ? "ready" : "unavailable");
    if (!record || !g_adapter.config) return;
    authority = host_authority(&single_player, NULL);
    if (!authority) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[REWARD_POLICY_OBSERVE] eligible=no reason=non_authority "
               "committed=no rewards=off");
        return;
    }
    memset(&context, 0, sizeof(context));
    context.host_authority = true;
    context.single_player = single_player;
    context.original_vanilla_loot_preserved = true;
    context.coin_backend_verified = false;
    context.progress = record->progress;
    context.tier = record->tier;
    context.terrain_snapshot = record->rules.terrain;
    context.reward_chance_bonus = record->rules.reward_chance_bonus;
    context.reward_quality_multiplier = record->rules.reward_quality_multiplier;
    context.random_seed = record->key.world_session_id ^ record->key.generation_id;
    memset(&policy, 0, sizeof(policy));
    if (!or_loot_build_policy(g_adapter.config, &context, &policy)) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[REWARD_POLICY_OBSERVE] eligible=no reason=policy_rejected "
               "committed=no rewards=off");
        return;
    }
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[REWARD_POLICY] eligible=yes tier=%s extraReward=%s "
           "pool=%s slots=%u moneyPolicy=no_extra_grant committed=no rewards=test-gel "
           "quality=%.3f chanceBonus=%.3f",
           or_elite_tier_name(record->tier), policy.extra_reward ? "yes" : "no",
           policy.pool_id ? policy.pool_id : "none",
           (unsigned)policy.extra_reward_slots,
           (double)policy.reward_quality_multiplier,
           (double)record->rules.reward_chance_bonus);
    if (!policy.extra_reward) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_PLAN_OBSERVE] ready=no reason=no_extra_reward "
               "invoke=disabled");
    } else if (!g_adapter.runtime->item_new_item_signature_ready) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_PLAN_OBSERVE] ready=no reason=NewItem_signature_unavailable "
               "invoke=disabled");
    } else {
#if !ORIGINREWRITE_ENABLE_LIVE_REWARD
        unsigned char sandbox_position_raw[8] = {0};
        int32_t sandbox_x = 0;
        int32_t sandbox_y = 0;
        int32_t sandbox_width = 0;
        int32_t sandbox_height = 0;
        int32_t sandbox_type = 23;
        int32_t sandbox_stack = 1;
        uint32_t guard_before = 0x4f525742u;
        uint32_t guard_after = 0xa17e5c39u;
        bool sandbox_position_ok = read_position_raw(
            g_adapter.runtime->field_position_probe, instance, sandbox_position_raw);
        bool sandbox_width_ok = read_i32(g_adapter.runtime->field_width, instance,
                                         &sandbox_width);
        bool sandbox_height_ok = read_i32(g_adapter.runtime->field_height, instance,
                                          &sandbox_height);
        if (sandbox_position_ok) {
            memcpy(&sandbox_x, sandbox_position_raw, sizeof(sandbox_x));
            memcpy(&sandbox_y, sandbox_position_raw + sizeof(sandbox_x),
                   sizeof(sandbox_y));
        }
        if (!sandbox_width_ok || sandbox_width <= 0) sandbox_width = 16;
        if (!sandbox_height_ok || sandbox_height <= 0) sandbox_height = 16;
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_SANDBOX_ARGS] phase=before independentCopies=ready "
               "x=%d y=%d width=%d height=%d type=%d stack=%d position=%s "
               "guardBefore=%08x guardAfter=%08x",
               (int)sandbox_x, (int)sandbox_y, (int)sandbox_width,
               (int)sandbox_height, (int)sandbox_type, (int)sandbox_stack,
               sandbox_position_ok ? "ok" : "unavailable",
               (unsigned)guard_before, (unsigned)guard_after);
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_SANDBOX_VERIFY] invoke=disabled mutation=blocked "
               "copiesIntact=yes guardIntact=%s postCallCheck=not_applicable",
               guard_before == 0x4f525742u && guard_after == 0xa17e5c39u
                   ? "yes" : "no");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_SANDBOX] eligible=yes item=Gel expectedId=23 "
               "invoke=disabled mutation=blocked alternate=NewItem12 "
               "reason=abi_parameter_diagnostic");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_NEWITEM_ROUTE] primary=NewItem9 disabled "
               "alternate=NewItem12 metadata-only pointerArgs=unverified");
#else
        static bool live_reward_attempted;
        if (live_reward_attempted) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_CALL_SINGLE_GUARD] eligible=yes invoke=disabled "
                   "mutation=blocked reason=already_attempted");
            return;
        }
        live_reward_attempted = true;
        /* Controlled first live reward test: one fixed vanilla Gel only. */
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_PLAN] ready=yes reason=controlled_gel_test "
               "invoke=enabled mutation=allowed");
        const char *drop_name = "Gel";
        int16_t net_id = 23;
        unsigned char position_raw[8] = {0};
        float position_x = 0.0f;
        float position_y = 0.0f;
        int32_t x = 0;
        int32_t y = 0;
        int32_t width = 0;
        int32_t height = 0;
        int16_t resolved_item_id = -1;
        int32_t item_type_arg = -1;
        int32_t spawn_width = 0;
        int32_t spawn_height = 0;
        int32_t result_slot = -1;
        bool no_broadcast = true;
        int32_t prefix = 0;
        int32_t ownership = 0;
        void *args[9];
        bool position_ok = read_position_raw(g_adapter.runtime->field_position_probe,
                                              instance, position_raw);
        bool width_ok = read_i32(g_adapter.runtime->field_width, instance, &width);
        bool height_ok = read_i32(g_adapter.runtime->field_height, instance, &height);
        bool id_ok = false;
        bool call_ok = false;
        bool size_fallback = false;
        int32_t slot_item_type = -1;
        int32_t slot_item_stack = -1;
        bool slot_read_ok = false;
        bool stack_read_ok = false;
        if (position_ok) {
            memcpy(&position_x, position_raw, sizeof(position_x));
            memcpy(&position_y, position_raw + sizeof(position_x), sizeof(position_y));
            if (isfinite(position_x) && isfinite(position_y) &&
                position_x >= (float)INT32_MIN && position_x <= (float)INT32_MAX &&
                position_y >= (float)INT32_MIN && position_y <= (float)INT32_MAX) {
                x = (int32_t)position_x;
                y = (int32_t)position_y;
            } else {
                position_ok = false;
            }
        }
        spawn_width = width;
        spawn_height = height;
        if (!width_ok || !height_ok || width <= 0 || height <= 0 ||
            spawn_width <= 0 || spawn_height <= 0) {
            spawn_width = 16;
            spawn_height = 16;
            size_fallback = true;
        }
        if (g_adapter.runtime->method_item_id_from_net_id && patchlib_method_invoke_args) {
            void *id_args[1] = {&net_id};
            id_ok = patchlib_method_invoke_args(
                g_adapter.runtime->method_item_id_from_net_id, PATCH_NULL,
                &resolved_item_id, id_args) && resolved_item_id == net_id;
            item_type_arg = (int32_t)resolved_item_id;
            if (id_ok && resolved_item_id != net_id) id_ok = false;
        }
        if (position_ok && width_ok && height_ok && id_ok &&
            g_adapter.runtime->method_item_new_item && patchlib_method_invoke_args &&
            spawn_width > 0 && spawn_height > 0 && isfinite(position_x) && isfinite(position_y)) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_CALL_ARGS] phase=before x=%d y=%d width=%d height=%d type=%d "
                   "stack=1 noBroadcast=1 prefix=0 ownership=0 fallback=%s",
                   (int)x, (int)y, (int)spawn_width, (int)spawn_height,
                   (int)item_type_arg, size_fallback ? "yes" : "no");
            args[0] = &x;
            args[1] = &y;
            args[2] = &spawn_width;
            args[3] = &spawn_height;
            args[4] = &item_type_arg;
            {
                int32_t stack = 1;
                args[5] = &stack;
                args[6] = &no_broadcast;
                args[7] = &prefix;
                args[8] = &ownership;
            }
            call_ok = patchlib_method_invoke_args(
                g_adapter.runtime->method_item_new_item, PATCH_NULL,
                &result_slot, args);
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_CALL_ARGS] phase=after invoke=%s x=%d y=%d width=%d height=%d "
                   "type=%d resultSlot=%d",
                   call_ok ? "ok" : "failed", (int)x, (int)y,
                   (int)spawn_width, (int)spawn_height, (int)item_type_arg,
                   (int)result_slot);
            if (call_ok && result_slot >= 0 && g_adapter.runtime->main_item_field &&
                g_adapter.runtime->item_field_type && patchlib_array_at &&
                patchlib_field_get_value) {
                patch_handle_t item_array = PATCH_NULL;
                patch_handle_t item_object = PATCH_NULL;
                patchlib_field_get_value(g_adapter.runtime->main_item_field, PATCH_NULL,
                                         &item_array);
                if (item_array && patchlib_array_at(item_array, (size_t)result_slot,
                                                    &item_object) && item_object) {
                    void *type_ptr = NULL;
                    void *stack_ptr = NULL;
                    patchlib_field_get_value(g_adapter.runtime->item_field_type,
                                             item_object, &slot_item_type);
                    slot_read_ok = slot_item_type >= 0;
                    if (g_adapter.runtime->item_field_stack) {
                        patchlib_field_get_value(g_adapter.runtime->item_field_stack,
                                                 item_object, &slot_item_stack);
                        stack_read_ok = slot_item_stack >= 0;
                    }
#if defined(__ANDROID__)
                    if (patchlib_field_get_pointer) {
                        type_ptr = patchlib_field_get_pointer(
                            g_adapter.runtime->item_field_type, item_object);
                        if (type_ptr) {
                            memcpy(&slot_item_type, type_ptr, sizeof(slot_item_type));
                            slot_read_ok = true;
                        }
                        if (g_adapter.runtime->item_field_stack) {
                            stack_ptr = patchlib_field_get_pointer(
                                g_adapter.runtime->item_field_stack, item_object);
                            if (stack_ptr) {
                                memcpy(&slot_item_stack, stack_ptr,
                                       sizeof(slot_item_stack));
                                stack_read_ok = true;
                            }
                        }
                    }
#endif
                    OR_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_SLOT_ABI] slot=%d object=%p typePtr=%p stackPtr=%p "
                           "type=%d stack=%d",
                           (int)result_slot, (void *)item_object, type_ptr, stack_ptr,
                           (int)slot_item_type, (int)slot_item_stack);
                }
            }
        }
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_CALL_COMMIT] item=%s expectedId=%d readbackId=%d idRead=%s position=%s width=%s:%d "
               "height=%s:%d sizeFallback=%s spawnSize=%dx%d invoke=%s mutationSubmitted=%s slot=%d slotType=%s:%d stackRead=%s:%d noBroadcast=true prefix=0 ownership=0",
               drop_name, (int)item_type_arg, (int)slot_item_type, id_ok ? "ok" : "failed",
               position_ok ? "ok" : "failed", width_ok ? "ok" : "failed", width,
               height_ok ? "ok" : "failed", height, size_fallback ? "yes" : "no",
               (int)spawn_width, (int)spawn_height,
               call_ok ? "ok" : "blocked_or_failed", call_ok ? "yes" : "no", (int)result_slot,
               slot_read_ok ? "ok" : "unavailable", (int)slot_item_type,
               stack_read_ok ? "ok" : "unavailable", (int)slot_item_stack);
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ITEM_WRITEBACK_VERIFY] expected=%d readback=%d match=%s stackReadback=%d "
               "stackMatch=%s invoke=%s slot=%d verification=field-readback",
               (int)item_type_arg, (int)slot_item_type,
               slot_read_ok && item_type_arg == slot_item_type ? "yes" : "no",
               (int)slot_item_stack, stack_read_ok && slot_item_stack == 1 ? "yes" : "no",
               call_ok ? "ok" : "failed", (int)result_slot);
#endif
    }
}

static uint64_t update_tick(void);

static void log_lifecycle_health(void) {
    uint64_t tick;
    uint32_t occupied = 0u;
    uint32_t pending = 0u;
    uint32_t elite = 0u;
    size_t i;
    if (!g_adapter.installed) return;
    tick = update_tick();
    if (g_adapter.last_health_tick != 0u &&
        tick < g_adapter.last_health_tick + OR_LIFECYCLE_HEALTH_INTERVAL) return;
    g_adapter.last_health_tick = tick;
    for (i = 0; i < OR_MAX_TRACKED_NPCS; ++i) {
        OR_NativeBinding *binding = &g_adapter.bindings[i];
        if (binding->occupied && binding->pending && binding->last_seen_tick != 0u &&
            tick > binding->last_seen_tick + OR_PENDING_STALE_TICKS) {
            g_adapter.stale_pending_clear_total += 1u;
            clear_binding(binding);
        }
        if (!binding->occupied) continue;
        occupied += 1u;
        if (binding->pending) pending += 1u;
        if (binding->elite) elite += 1u;
    }
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[LIFECYCLE_HEALTH] tick=%llu occupied=%u pending=%u elite=%u "
           "setdefaults=%llu ai=%llu commits=%llu reclaim=%llu full=%llu "
           "pendingClear=%llu eliteClear=%llu reuse=%llu",
           (unsigned long long)tick, (unsigned)occupied, (unsigned)pending,
           (unsigned)elite, (unsigned long long)g_adapter.setdefaults_total,
           (unsigned long long)g_adapter.ai_callback_total,
           (unsigned long long)g_adapter.commit_total,
           (unsigned long long)g_adapter.binding_reclaim_total,
           (unsigned long long)g_adapter.binding_full_total,
           (unsigned long long)g_adapter.pending_clear_total,
           (unsigned long long)g_adapter.elite_clear_total,
           (unsigned long long)g_adapter.lifecycle_reuse_total);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[LIFECYCLE_CLEANUP] stalePending=%llu thresholdTicks=%u",
           (unsigned long long)g_adapter.stale_pending_clear_total,
           (unsigned)OR_PENDING_STALE_TICKS);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BATCH_SUMMARY] commits=%llu deaths=%llu depthSurface=%llu "
           "depthUnderground=%llu depthCavern=%llu depthUnderworld=%llu "
           "aiMelee=%llu aiRanged=%llu aiFlying=%llu aiWorm=%llu "
           "aiSwarm=%llu aiSpecial=%llu",
           (unsigned long long)g_adapter.commit_total,
           (unsigned long long)g_adapter.elite_clear_total,
           (unsigned long long)g_adapter.batch_depth_counts[OR_DEPTH_SURFACE],
           (unsigned long long)g_adapter.batch_depth_counts[OR_DEPTH_UNDERGROUND],
           (unsigned long long)g_adapter.batch_depth_counts[OR_DEPTH_CAVERN],
           (unsigned long long)g_adapter.batch_depth_counts[OR_DEPTH_UNDERWORLD],
           (unsigned long long)g_adapter.batch_archetype_counts[OR_AI_ARCHETYPE_MELEE],
           (unsigned long long)g_adapter.batch_archetype_counts[OR_AI_ARCHETYPE_RANGED],
           (unsigned long long)g_adapter.batch_archetype_counts[OR_AI_ARCHETYPE_FLYING],
           (unsigned long long)g_adapter.batch_archetype_counts[OR_AI_ARCHETYPE_WORM],
           (unsigned long long)g_adapter.batch_archetype_counts[OR_AI_ARCHETYPE_SWARM],
           (unsigned long long)g_adapter.batch_archetype_counts[OR_AI_ARCHETYPE_SPECIAL]);
}

static uint64_t world_session_id(void) {
    int32_t world_id = 0;
    if (g_adapter.runtime && read_i32(g_adapter.runtime->main_world_id, NULL, &world_id) &&
        world_id > 0) return (uint64_t)(uint32_t)world_id;
    return 1u;
}

static uint64_t update_tick(void) {
    uint64_t tick = 0;
    if (g_adapter.runtime && read_u64(g_adapter.runtime->main_update_count, NULL, &tick)) {
        return tick;
    }
    return ++g_adapter.fallback_tick;
}

static bool host_authority(bool *single_player, bool *known) {
    int32_t net_mode = 0;
    if (single_player) *single_player = false;
    if (known) *known = false;
    if (g_adapter.runtime && g_adapter.runtime->main_net_mode) {
        (void)read_i32(g_adapter.runtime->main_net_mode, NULL, &net_mode);
    }
    /* SetDefaults is also the proven local stat boundary.  PatchLib's static
     * getter is void and may not populate an optional Main field on some
     * Android builds.  Treat every value except Terraria's explicit client
     * value (1) as local/authoritative, matching the working reference mod;
     * an unknown read must never disable the core stat overlay. */
    if (net_mode == 1) {
        if (known) *known = true;
        return false;
    }
    if (known) *known = net_mode == 0 || net_mode == 2;
    if (single_player) *single_player = net_mode != 2;
    return true;
}

static OR_GameMode current_mode(void) {
    int32_t mode = 0;
    bool zenith = false;
    if (g_adapter.runtime) {
        (void)read_i32(g_adapter.runtime->main_game_mode, NULL, &mode);
        (void)read_bool(g_adapter.runtime->main_zenith_world, NULL, &zenith);
    }
    if (zenith) return OR_MODE_ZENITH;
    if (mode == 1) return OR_MODE_EXPERT;
    if (mode == 2) return OR_MODE_MASTER;
    if (mode == 3) return OR_MODE_JOURNEY;
    return OR_MODE_CLASSIC;
}

static OR_ProgressStage current_progress(void) {
    bool hard = false;
    bool mech = false;
    bool plant = false;
    bool golem = false;
    bool moonlord = false;
    if (g_adapter.runtime) {
        (void)read_bool(g_adapter.runtime->main_hard_mode, NULL, &hard);
        (void)read_bool(g_adapter.runtime->npc_downed_mech, NULL, &mech);
        (void)read_bool(g_adapter.runtime->npc_downed_plant, NULL, &plant);
        (void)read_bool(g_adapter.runtime->npc_downed_golem, NULL, &golem);
        (void)read_bool(g_adapter.runtime->npc_downed_moonlord, NULL, &moonlord);
    }
    if (moonlord) return OR_PROGRESS_ENDGAME;
    if (plant || golem) return OR_PROGRESS_POST_PLANTERA;
    if (mech) return OR_PROGRESS_PRE_PLANTERA;
    if (hard) return OR_PROGRESS_HARDMODE_PRE_MECH;
    return OR_PROGRESS_PRE_HARDMODE;
}

static void capture_world_context(patch_handle_t instance,
                                  OR_TerrainSnapshot *terrain,
                                  OR_Weather *weather,
                                  bool *is_night) {
    static uint32_t context_log_samples;
    bool log_context = context_log_samples < 8u;
    bool day_time = true;
    bool blood_moon = false;
    bool raining = false;
    bool eclipse = false;
    bool pumpkin_moon = false;
    bool snow_moon = false;
    bool slime_rain = false;
    double world_surface = 0.0;
    float top_world = 0.0f;
    float bottom_world = 0.0f;
    bool world_surface_ok = false;
    bool top_world_ok = false;
    bool bottom_world_ok = false;
    int32_t max_tiles_y = 0;
    bool max_tiles_y_ok = false;
    int32_t underworld_layer = 0;
    bool underworld_layer_ok = false;
    float position_x = 0.0f;
    float position_y = 0.0f;
    OR_DepthTag decoded_depth = OR_DEPTH_SURFACE;
    bool terrain_read_ok = false;
    static bool terrain_change_initialized;
    static OR_DepthTag last_terrain_depth;
    static OR_BiomeTag last_terrain_biome;
    static OR_SpecialLocationTag last_terrain_special;
    static uint32_t terrain_change_log_count;
    double rock_layer = 0.0;
    bool rock_layer_ok = false;
    unsigned char position_raw[8];
    bool position_raw_ok;
    uint64_t context_tick = update_tick();
    if (terrain) {
        *terrain = (OR_TerrainSnapshot){OR_DEPTH_SURFACE, OR_BIOME_FOREST,
                                        OR_SPECIAL_NONE};
    }
    if (!g_adapter.runtime) {
        if (weather) *weather = OR_WEATHER_CLEAR;
        if (is_night) *is_night = false;
        return;
    }
    if (log_context) ++context_log_samples;
    position_raw_ok = read_position_raw(g_adapter.runtime->field_position_probe,
                                        instance, position_raw);
    if (log_context) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_POSITION_VALUE] read=%s size=8 type=12 raw=%02x%02x%02x%02x%02x%02x%02x%02x "
               "interpretation=deferred",
               position_raw_ok ? "ok" : "unavailable",
               position_raw[0], position_raw[1], position_raw[2], position_raw[3],
               position_raw[4], position_raw[5], position_raw[6], position_raw[7]);
    }
    (void)read_bool(g_adapter.runtime->main_day_time, NULL, &day_time);
    (void)read_bool(g_adapter.runtime->main_blood_moon, NULL, &blood_moon);
    (void)read_bool(g_adapter.runtime->main_raining, NULL, &raining);
    (void)read_bool(g_adapter.runtime->main_eclipse, NULL, &eclipse);
    (void)read_bool(g_adapter.runtime->main_pumpkin_moon, NULL, &pumpkin_moon);
    (void)read_bool(g_adapter.runtime->main_snow_moon, NULL, &snow_moon);
    (void)read_bool(g_adapter.runtime->main_slime_rain, NULL, &slime_rain);
    world_surface_ok = read_double(g_adapter.runtime->main_world_surface, NULL,
                                   &world_surface);
    top_world_ok = read_float(g_adapter.runtime->main_top_world, NULL, &top_world);
    bottom_world_ok = read_float(g_adapter.runtime->main_bottom_world, NULL,
                                 &bottom_world);
    max_tiles_y_ok = read_i32(g_adapter.runtime->main_max_tiles_y, NULL,
                               &max_tiles_y);
    if (log_context) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BOUNDARY_VALUE] maxTilesY=%s:%d interpretation=deferred",
               max_tiles_y_ok ? "ok" : "unread", max_tiles_y);
    }
    underworld_layer_ok = read_static_i32_method(
        g_adapter.runtime->method_underworld_layer_get, &underworld_layer);
    rock_layer_ok = read_double(g_adapter.runtime->main_rock_layer, NULL,
                                &rock_layer);
    terrain_read_ok = position_raw_ok && underworld_layer_ok && rock_layer_ok &&
        decode_position_depth(position_raw, world_surface, rock_layer,
                              underworld_layer, top_world, bottom_world,
                              &decoded_depth, &position_x, &position_y);
    if (terrain_read_ok && terrain) {
        terrain->depth = decoded_depth;
    }
    (void)capture_player_biome(terrain, context_tick, log_context);
    if (terrain_read_ok && terrain &&
        (!terrain_change_initialized || last_terrain_depth != terrain->depth ||
         last_terrain_biome != terrain->biome ||
         last_terrain_special != terrain->special) &&
        terrain_change_log_count < 128u) {
        terrain_change_initialized = true;
        last_terrain_depth = terrain->depth;
        last_terrain_biome = terrain->biome;
        last_terrain_special = terrain->special;
        ++terrain_change_log_count;
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_CHANGE] sample=%u depth=%s biome=%s special=%s "
               "position=%.2f,%.2f worldSurface=%s:%.2f rockLayer=%s:%.2f "
               "topWorld=%s:%.2f bottomWorld=%s:%.2f underworldLayer=%s:%d "
               "readOnly=yes",
               (unsigned)terrain_change_log_count,
               terrain_depth_name(terrain->depth), biome_name(terrain->biome),
               special_name(terrain->special), position_x, position_y,
               world_surface_ok ? "ok" : "fail", world_surface,
               rock_layer_ok ? "ok" : "fail", rock_layer,
               top_world_ok ? "ok" : "fail", top_world,
               bottom_world_ok ? "ok" : "fail", bottom_world,
               underworld_layer_ok ? "ok" : "fail", underworld_layer);
    }
    if (log_context) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_DEPTH_VALUE] read=%s underworldLayer=%s:%d position=%s:%.2f,%.2f depth=%s",
               terrain_read_ok ? "ok" : "deferred",
               underworld_layer_ok ? "ok" : "unread", underworld_layer,
               position_raw_ok ? "ok" : "unread", position_x, position_y,
               terrain_read_ok ? terrain_depth_name(decoded_depth) : "safe_default");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_BATCH_VERIFY] position=%s worldSurface=%s rockLayer=%s "
               "topWorld=%s bottomWorld=%s maxTilesY=%s underworldLayer=%s "
               "depth=%s biome=%s special=%s readOnly=yes",
               position_raw_ok ? "ok" : "fail",
               world_surface_ok ? "ok" : "fail",
               rock_layer_ok ? "ok" : "fail",
               top_world_ok ? "ok" : "fail",
               bottom_world_ok ? "ok" : "fail",
               max_tiles_y_ok ? "ok" : "fail",
               underworld_layer_ok ? "ok" : "fail",
               terrain_read_ok ? terrain_depth_name(decoded_depth) : "unresolved",
               terrain ? biome_name(terrain->biome) : "unresolved",
               terrain ? special_name(terrain->special) : "unresolved");
    }
    if (is_night) *is_night = !day_time;
    if (!weather) return;
    if (blood_moon) {
        *weather = OR_WEATHER_BLOOD_MOON;
    } else if (eclipse) {
        *weather = OR_WEATHER_ECLIPSE;
    } else if (raining) {
        *weather = OR_WEATHER_RAIN;
    } else {
        *weather = OR_WEATHER_CLEAR;
    }
    if (log_context) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[WORLD_CONTEXT] dayTime=%s night=%s raining=%s bloodMoon=%s "
               "eclipse=%s pumpkinMoon=%s snowMoon=%s slimeRain=%s weather=%s "
               "terrain=%s/%s source=verified_main_fields",
               day_time ? "yes" : "no", day_time ? "no" : "yes",
               raining ? "yes" : "no", blood_moon ? "yes" : "no",
               eclipse ? "yes" : "no", pumpkin_moon ? "yes" : "no",
               snow_moon ? "yes" : "no", slime_rain ? "yes" : "no",
               or_weather_name(*weather), terrain ? biome_name(terrain->biome) : "forest",
               terrain ? special_name(terrain->special) : "none");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[TERRAIN_CONTEXT] depth=%s biome=%s special=%s "
               "source=%s position=%s positionField=%s "
               "worldSurface=%s:%.2f topWorld=%s:%.2f bottomWorld=%s:%.2f",
               terrain && terrain_read_ok ? terrain_depth_name(terrain->depth) : "surface",
               terrain ? biome_name(terrain->biome) : "forest",
               terrain ? special_name(terrain->special) : "none",
               terrain_read_ok ? "verified_position_boundaries" : "safe_default",
               terrain_read_ok ? "verified" : "deferred",
               (g_adapter.runtime->field_position_probe && instance) ? "present" : "unavailable",
               world_surface_ok ? "ok" : "unread", world_surface,
               top_world_ok ? "ok" : "unread", top_world,
               bottom_world_ok ? "ok" : "unread", bottom_world);
    }
}

static int32_t clamp_i32(int64_t value) {
    if (value > INT32_MAX) return INT32_MAX;
    if (value < 0) return 0;
    return (int32_t)value;
}

static float clamp_float(double value) {
    if (!isfinite(value) || value <= 0.0) return 0.0f;
    if (value >= (double)FLT_MAX) return FLT_MAX;
    return (float)value;
}

static const char *tier_prefix(OR_EliteTier tier) {
    switch (tier) {
        case OR_TIER_ALTERED: return "异化体";
        case OR_TIER_CALAMITY: return "灾变体";
        case OR_TIER_APOCALYPSE: return "终焉体";
        default: return NULL;
    }
}

static uint32_t tier_color(OR_EliteTier tier) {
    switch (tier) {
        case OR_TIER_ALTERED: return 0xFF4CAF50u;
        case OR_TIER_CALAMITY: return 0xFF3D8DFFu;
        case OR_TIER_APOCALYPSE: return 0xFFE53935u;
        default: return 0xFFFFFFFFu;
    }
}

static bool apply_color_marker(patch_handle_t instance, OR_EliteTier tier,
                               uint32_t *readback) {
    uint32_t packed;
    uint32_t check = 0u;
    void *raw;
    if (readback) *readback = 0u;
    if (!g_adapter.runtime || !instance ||
        !g_adapter.runtime->capabilities.color_marker_ready ||
        !g_adapter.runtime->field_color || !patchlib_field_get_pointer) {
        return false;
    }
    packed = tier_color(tier);
    raw = patchlib_field_get_pointer(g_adapter.runtime->field_color, instance);
    if (!raw) return false;
    memcpy(raw, &packed, sizeof(packed));
    memcpy(&check, raw, sizeof(check));
    if (readback) *readback = check;
    return check == packed;
}

static void probe_color_member(patch_handle_t instance, uint32_t npc_type) {
    void *storage;
    uint64_t raw = 0u;
    uint64_t getter_raw = 0u;
    size_t size = 0u;
    patch_type_t type = PATCH_VOID;
    if (!instance || !g_adapter.runtime ||
        !g_adapter.runtime->capabilities.color_marker_probe_ready ||
        !g_adapter.runtime->field_color || !patchlib_field_get_pointer ||
        !patchlib_field_get_size || !patchlib_field_get_type ||
        !patchlib_field_get_value ||
        g_adapter.color_probe_count >= OR_COLOR_PROBE_LIMIT) return;
    storage = patchlib_field_get_pointer(g_adapter.runtime->field_color, instance);
    size = patchlib_field_get_size(g_adapter.runtime->field_color);
    type = patchlib_field_get_type(g_adapter.runtime->field_color);
    if (storage && size >= sizeof(raw)) memcpy(&raw, storage, sizeof(raw));
    patchlib_field_get_value(g_adapter.runtime->field_color, instance, &getter_raw);
    g_adapter.color_probe_count += 1u;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[COLOR_PROBE] sample=%u type=%u fieldType=%d fieldSize=%zu "
           "storage=%p raw=%016llx getterRaw=%016llx write=disabled "
           "reason=pointer8_unverified",
           (unsigned)g_adapter.color_probe_count, (unsigned)npc_type, (int)type,
           size, storage, (unsigned long long)raw,
           (unsigned long long)getter_raw);
}

static bool write_given_name_marker(patch_handle_t instance,
                                    OR_EliteTier tier,
                                    const char **failure_reason) {
    patch_handle_t original = PATCH_NULL;
    patch_handle_t replacement;
    patch_handle_t readback = PATCH_NULL;
    const char *prefix;
    char *name;
    char *readback_name;
    char decorated[512];
    void *setter_args[1];
    uint64_t ignored_return = 0u;
    const char *reason = "unknown";
    bool used_empty_fallback = false;
    bool used_display_name = false;
    if (failure_reason) *failure_reason = NULL;
    if (!g_adapter.runtime || !instance ||
        !g_adapter.runtime->capabilities.given_name_property_ready ||
        !g_adapter.runtime->method_given_name_get ||
        !g_adapter.runtime->method_given_name_set ||
        !patchlib_method_invoke_args || !patchlib_string_cstr ||
        !patchlib_string_create) {
        reason = "capability_missing";
        goto fail;
    }
    prefix = tier_prefix(tier);
    if (!prefix) {
        reason = "tier_prefix_missing";
        goto fail;
    }
    if (!patchlib_method_invoke_args(g_adapter.runtime->method_given_name_get,
                                     instance, &original, NULL)) {
        reason = "getter_invoke_failed";
        goto fail;
    }
    name = handle_valid(original) ? patchlib_string_cstr(original) : NULL;
    if (!name || name[0] == '\0') {
        free(name);
        name = NULL;
        if (g_adapter.runtime->method_display_name_get &&
            patchlib_method_invoke_args(g_adapter.runtime->method_display_name_get,
                                        instance, &original, NULL) &&
            handle_valid(original)) {
            name = patchlib_string_cstr(original);
            used_display_name = name && name[0] != '\0';
        }
    }
    if (name && name[0] != '\0') {
        if (strstr(name, prefix) != NULL) {
            free(name);
            if (failure_reason) *failure_reason = "already_prefixed";
            return true;
        }
        if (snprintf(decorated, sizeof(decorated), "%s·%s", prefix, name) >=
            (int)sizeof(decorated)) {
            free(name);
            reason = "decorated_name_too_long";
            goto fail;
        }
        free(name);
    } else {
        /* Hostile NPCs commonly have no custom GivenName. Keep the write
         * compatible with that normal state instead of rejecting it; the
         * runtime can then decide whether FullName uses the assigned value. */
        free(name);
        if (snprintf(decorated, sizeof(decorated), "%s", prefix) >=
            (int)sizeof(decorated)) {
            reason = "prefix_too_long";
            goto fail;
        }
        used_empty_fallback = true;
        reason = "empty_given_name_fallback";
    }
    replacement = patchlib_string_create(decorated);
    if (!handle_valid(replacement)) {
        reason = "replacement_create_failed";
        goto fail;
    }
    setter_args[0] = &replacement;
    if (!patchlib_method_invoke_args(g_adapter.runtime->method_given_name_set,
                                     instance, &ignored_return, setter_args)) {
        reason = "setter_invoke_failed";
        goto fail;
    }
    if (!patchlib_method_invoke_args(g_adapter.runtime->method_given_name_get,
                                     instance, &readback, NULL)) {
        reason = "readback_invoke_failed";
        goto fail;
    }
    if (!handle_valid(readback)) {
        reason = "readback_handle_invalid";
        goto fail;
    }
    readback_name = patchlib_string_cstr(readback);
    if (!readback_name) {
        reason = "readback_cstr_failed";
        goto fail;
    }
    if (strcmp(readback_name, decorated) != 0) {
        free(readback_name);
        reason = "readback_mismatch";
        goto fail;
    }
    free(readback_name);
    if (failure_reason) *failure_reason = used_empty_fallback
        ? reason : (used_display_name ? "display_name_property" : "ok");
    return true;

fail:
    if (failure_reason) *failure_reason = reason;
    return false;
}

static void __attribute__((unused)) apply_visual_markers(
    patch_handle_t instance, OR_EliteTier tier, uint32_t npc_type,
    bool announce) {
    uint32_t color_readback = 0u;
    bool color_ok = apply_color_marker(instance, tier, &color_readback);
    bool name_ok = write_given_name_marker(instance, tier, NULL);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[COLOR_WRITE] instance=%p type=%u tier=%s color=%08x colorOk=%s readback=%08x",
           (void *)instance, (unsigned)npc_type, or_elite_tier_name(tier),
           (unsigned)tier_color(tier), color_ok ? "yes" : "no",
           (unsigned)color_readback);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[NAME_WRITE] type=%u tier=%s writeOk=%s",
           (unsigned)npc_type, or_elite_tier_name(tier), name_ok ? "yes" : "no");
    if (announce) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ELITE_NOTICE] type=%u tier=%s noticeOk=deferred_to_broadcast_commit",
               (unsigned)npc_type, or_elite_tier_name(tier));
    }
}

static bool read_vanilla_stats(patch_handle_t instance,
                               uint32_t *npc_type,
                               OR_VanillaStats *stats,
                               bool *is_boss,
                               bool *is_town,
                               bool *is_friendly,
                               bool *active,
                               const char **failed_field) {
    int32_t type = 0;
    int32_t life_max = 0;
    int32_t life = 0;
    int32_t damage = 0;
    int32_t defense = 0;
    int32_t ai_style = -1;
    float knockback = 0.0f;
    float scale = 1.0f;
    float slots = 1.0f;
    float value = 0.0f;
    bool local_active = false;
    if (failed_field) *failed_field = NULL;
    if (!g_adapter.runtime || !stats || !npc_type) {
        if (failed_field) *failed_field = "arguments/runtime";
        return false;
    }
    /* BNM is the primary metadata accessor in the new architecture. It is
     * enabled only after an exact Unity-family and IL2CPP-symbol gate. The
     * existing PatchLib path remains a per-call fallback so metadata drift
     * degrades safely instead of breaking the spawn lifecycle. */
    if (or_bnm_read_npc(instance, npc_type, stats, is_boss, is_town,
                        is_friendly, active)) {
        /* Read-only acceptance gate for BNM's Unity-2021.3 field layout.
         * An independent PatchLib read must agree; otherwise fall through to
         * the established PatchLib path without writing NPC memory. */
        int32_t patch_type = 0;
        int32_t patch_life_max = 0;
        int32_t patch_life = 0;
        const bool patch_core_ok =
            read_i32(g_adapter.runtime->field_type, instance, &patch_type) &&
            read_i32(g_adapter.runtime->field_life_max, instance, &patch_life_max) &&
            read_i32(g_adapter.runtime->field_life, instance, &patch_life);
        const bool match = patch_core_ok && patch_type > 0 && patch_life_max > 0 &&
            *npc_type == (uint32_t)patch_type && stats->life_max == patch_life_max &&
            stats->life_current == (patch_life > 0 ? patch_life : 0);
        OR_DIAG_LOG("[BNM_READ_CROSSCHECK] patchCore=%s bnmType=%u patchType=%d "
                    "bnmLifeMax=%d patchLifeMax=%d bnmLife=%d patchLife=%d match=%s "
                    "write=disabled fallback=%s",
                    patch_core_ok ? "ok" : "failed", (unsigned)*npc_type,
                    (int)patch_type, (int)stats->life_max, (int)patch_life_max,
                    (int)stats->life_current, (int)patch_life,
                    match ? "yes" : "no", match ? "no" : "yes");
        if (match) return true;
    }
    if (!read_i32(g_adapter.runtime->field_type, instance, &type)) {
        if (failed_field) *failed_field = "type";
        return false;
    }
    if (!read_i32(g_adapter.runtime->field_life_max, instance, &life_max)) {
        if (failed_field) *failed_field = "lifeMax";
        return false;
    }
    if (!read_i32(g_adapter.runtime->field_life, instance, &life)) life = life_max;
    (void)read_i32(g_adapter.runtime->field_damage, instance, &damage);
    (void)read_i32(g_adapter.runtime->field_defense, instance, &defense);
    (void)read_float(g_adapter.runtime->field_knockback_resist, instance, &knockback);
    (void)read_float(g_adapter.runtime->field_scale, instance, &scale);
    if (type <= 0) {
        if (failed_field) *failed_field = "type_value";
        return false;
    }
    if (life_max <= 0) {
        if (failed_field) *failed_field = "lifeMax_value";
        return false;
    }
    /* SetDefaults is the verified activation boundary on the target mobile
     * build; active may still be false during that method. AI will re-check it
     * after the object enters the live pool. */
    (void)read_bool(g_adapter.runtime->field_active, instance, &local_active);
    (void)read_i32(g_adapter.runtime->field_ai_style, instance, &ai_style);
    (void)read_float(g_adapter.runtime->field_npc_slots, instance, &slots);
    (void)read_float(g_adapter.runtime->field_value, instance, &value);
    if (is_boss) *is_boss = false;
    if (is_town) *is_town = false;
    if (is_friendly) *is_friendly = false;
    (void)read_bool(g_adapter.runtime->field_boss, instance, is_boss);
    (void)read_bool(g_adapter.runtime->field_town_npc, instance, is_town);
    (void)read_bool(g_adapter.runtime->field_friendly, instance, is_friendly);
    if (active) *active = local_active;
    *npc_type = type > 0 ? (uint32_t)type : 0u;
    stats->life_max = life_max > 0 ? life_max : 0;
    stats->life_current = life > 0 ? life : 0;
    stats->damage = damage > 0 ? damage : 0;
    stats->defense = defense > 0 ? defense : 0;
    stats->knockback_resist = isfinite(knockback) && knockback >= 0.0f ? knockback : 0.0f;
    stats->scale = isfinite(scale) && scale > 0.0f ? scale : 1.0f;
    stats->npc_slots = isfinite(slots) && slots > 0.0f ? slots : 1.0f;
    stats->money = isfinite(value) && value > 0.0f ? (int64_t)llround((double)value) : 0;
    stats->ai_style = ai_style;
    return true;
}

static bool apply_final_stats(patch_handle_t instance, const OR_FinalStats *stats) {
    bool ok = true;
    int32_t i32;
    float f32;
    int32_t width = 0;
    int32_t height = 0;
    float vanilla_scale = 1.0f;
    bool have_body = false;
    if (!g_adapter.runtime || !stats) return false;
    if (or_bnm_write_npc_stats(instance, stats)) return true;
    /* Capture the vanilla body values before writing the final scale.  Reading
     * scale after the write would make the ratio 1.0 and silently cancel the
     * width/height growth. */
    if (g_adapter.runtime->field_width && g_adapter.runtime->field_height &&
        read_i32(g_adapter.runtime->field_width, instance, &width) &&
        read_i32(g_adapter.runtime->field_height, instance, &height) &&
        read_float(g_adapter.runtime->field_scale, instance, &vanilla_scale) &&
        vanilla_scale > 0.0f && isfinite(vanilla_scale)) {
        have_body = true;
    }
    i32 = clamp_i32(stats->life_max);
    ok = field_write(g_adapter.runtime->field_life_max, instance, &i32) && ok;
    i32 = clamp_i32(stats->life_current);
    ok = field_write(g_adapter.runtime->field_life, instance, &i32) && ok;
    i32 = stats->damage < 0 ? 0 : stats->damage;
    if (g_adapter.runtime->field_damage) {
        ok = field_write(g_adapter.runtime->field_damage, instance, &i32) && ok;
    }
    i32 = stats->defense < 0 ? 0 : stats->defense;
    if (g_adapter.runtime->field_defense) {
        ok = field_write(g_adapter.runtime->field_defense, instance, &i32) && ok;
    }
    f32 = stats->knockback_resist;
    if (g_adapter.runtime->field_knockback_resist) {
        ok = field_write(g_adapter.runtime->field_knockback_resist, instance, &f32) && ok;
    }
    f32 = clamp_float(stats->scale);
    if (g_adapter.runtime->field_scale) {
        ok = field_write(g_adapter.runtime->field_scale, instance, &f32) && ok;
    }
    f32 = clamp_float(stats->money);
    if (g_adapter.runtime->field_value) {
        ok = field_write(g_adapter.runtime->field_value, instance, &f32) && ok;
    }
    if (have_body && stats->scale > 0.0f) {
        double body_ratio = (double)stats->scale / (double)vanilla_scale;
        width = clamp_i32((int64_t)llround((double)width * body_ratio));
        height = clamp_i32((int64_t)llround((double)height * body_ratio));
        ok = field_write(g_adapter.runtime->field_width, instance, &width) && ok;
        ok = field_write(g_adapter.runtime->field_height, instance, &height) && ok;
    }
    if (g_adapter.runtime->field_npc_slots) {
        f32 = clamp_float(stats->npc_slots);
        ok = field_write(g_adapter.runtime->field_npc_slots, instance, &f32) && ok;
    }
    return ok;
}

static bool commit_elite_from_baseline(patch_handle_t instance,
                                       OR_NativeBinding *binding,
                                       const OR_VanillaStats *vanilla,
                                       uint32_t npc_type,
                                       bool is_boss,
                                       bool is_town,
                                       bool is_friendly) {
    OR_SpawnContext context;
    OR_SpawnResult spawn;
    OR_ProgressStage progress;
    OR_GameMode mode;
    bool single_player = false;
    bool authority_known = false;
    bool archetype_known = false;
    OR_AiArchetype native_archetype;
    uint64_t session;
    uint64_t tick;
    const OR_EliteRecord *record;
    if (!binding || !vanilla || !g_adapter.runtime || !g_adapter.config ||
        !g_adapter.state || binding->roll_resolved) return false;
    progress = current_progress();
    mode = current_mode();
    /* A known multiplayer client must not mutate the authoritative state. If
     * Main.netMode is unavailable, keep the reference mod's SetDefaults
     * behavior and allow the local stat overlay; this optional field must not
     * disable the core feature on Android. */
    if (!host_authority(&single_player, &authority_known)) {
        if (authority_known) {
            binding->roll_resolved = true;
            OR_DIAG_LOG("authority_skip type=%u reason=multiplayer_client",
                        (unsigned)npc_type);
            return false;
        }
        single_player = true;
    }
    binding->roll_resolved = true;
    session = world_session_id();
    tick = update_tick();
    memset(&context, 0, sizeof(context));
    context.world_session_id = session;
    context.world_rule_seed = session;
    context.spawn_tick = tick;
    context.npc_slot = (int32_t)(binding - g_adapter.bindings);
    context.npc_type = npc_type;
    context.npc_active = true;
    context.host_authority = true;
    context.single_player = single_player;
    context.is_boss = is_boss;
    context.is_town_npc = is_town;
    context.is_friendly = is_friendly;
    context.is_dummy = false;
    context.is_segment = false;
    context.source = OR_SPAWN_NORMAL;
    context.progress = progress;
    context.mode = mode;
    capture_world_context(instance, &context.terrain, &context.weather, &context.is_night);
    native_archetype = or_ai_classify_native_style(vanilla->ai_style, &archetype_known);
    context.archetype = archetype_known ? native_archetype : OR_AI_ARCHETYPE_MELEE;
    context.max_active_elites = g_adapter.config->max_active_elites;
    context.transient_prepare = false;
    context.vanilla = *vanilla;
    memset(&spawn, 0, sizeof(spawn));
    {
        static uint32_t roll_log_samples;
        bool spawn_ok = or_spawn_try_commit(g_adapter.config, g_adapter.state, &context,
                                             session ^ (uint64_t)(uintptr_t)instance ^ tick,
                                             &spawn);
        bool log_roll = spawn_ok && spawn.committed;
        if (!log_roll && roll_log_samples < 32u) {
            ++roll_log_samples;
            log_roll = true;
        }
        if (log_roll) {
            OR_LOG(MOD_LOG_LEVEL_INFO,
                   "[ROLL] type=%u baseChance=%.3f effectiveChance=%.3f passed=%s committed=%s reason=%s",
                   (unsigned)npc_type, (double)spawn.base_chance,
                   (double)spawn.effective_chance, spawn.chance_passed ? "yes" : "no",
                   spawn_ok && spawn.committed ? "yes" : "no",
                   or_spawn_reject_reason_name(spawn.reason));
        }
        if (!spawn_ok || !spawn.committed) {
        OR_DIAG_LOG("commit_fail type=%u vanillaLife=%lld reason=%s",
                    (unsigned)npc_type, (long long)vanilla->life_max,
                    or_spawn_reject_reason_name(spawn.reason));
            return false;
        }
        g_adapter.commit_total += 1u;
    }
    binding->elite = true;
    binding->pending = false;
    binding->key = spawn.key;
    binding->previous_life_ratio = vanilla->life_max > 0
        ? (float)vanilla->life_current / (float)vanilla->life_max : 1.0f;
    if (!isfinite(binding->previous_life_ratio) || binding->previous_life_ratio < 0.0f) {
        binding->previous_life_ratio = 1.0f;
    }
    or_ai_runtime_init(&binding->ai_runtime);
    record = or_state_find_const(g_adapter.state, spawn.key);
    if (!record) {
        OR_DIAG_LOG("record_missing type=%u", (unsigned)npc_type);
        return false;
    }
    binding->key = spawn.key;
    if (record->rules.terrain.depth < OR_DEPTH_COUNT) {
        g_adapter.batch_depth_counts[record->rules.terrain.depth] += 1u;
    }
    if ((unsigned)context.archetype < 6u) {
        g_adapter.batch_archetype_counts[context.archetype] += 1u;
    }
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[AI_TYPE] type=%u aiStyle=%d archetype=%s applied=%s reason=%s",
           (unsigned)npc_type, record->native_ai_style,
           or_ai_archetype_name(context.archetype),
           archetype_known ? "yes" : "no",
           archetype_known ? "verified_ai_style_mapping" : "unknown_ai_style_fallback");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[TERRAIN_EFFECT] type=%u depth=%s snapshot=committed "
           "lifeMultiplier=%.3f damageMultiplier=%.3f defenseMultiplier=%.3f "
           "calamityWeight=%.3f apocalypseWeight=%.3f",
           (unsigned)npc_type, terrain_depth_name(record->rules.terrain.depth),
           (double)record->rules.life_multiplier,
           (double)record->rules.damage_multiplier,
           (double)record->rules.defense_multiplier,
           (double)record->rules.tier_weight_multiplier[OR_TIER_CALAMITY],
           (double)record->rules.tier_weight_multiplier[OR_TIER_APOCALYPSE]);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[AI_PLAN_OBSERVE] type=%u tier=%s primary=%s finisher=%s "
           "hasLight=%s hasFinisher=%s fanShots=%u summons=%u "
           "rageOnce=%s intensity=%.3f apply=disabled reason=special_ai_safe_off",
           (unsigned)npc_type, or_elite_tier_name(spawn.tier),
           ai_template_name(record->ai_plan.primary),
           ai_template_name(record->ai_plan.finisher),
           record->ai_plan.has_light_layer ? "yes" : "no",
           record->ai_plan.has_finisher ? "yes" : "no",
           (unsigned)record->ai_plan.fan_shot_count,
           (unsigned)record->ai_plan.summon_count,
           record->ai_plan.rage_once ? "yes" : "no",
           (double)record->ai_plan.intensity);
    probe_color_member(instance, npc_type);
    {
        bool write_ok = apply_final_stats(instance, &record->final_stats);
        int32_t readback_life_max = -1;
        int32_t readback_life = -1;
        int32_t readback_damage = -1;
        int32_t readback_defense = -1;
        float readback_knockback = -1.0f;
        float readback_scale = -1.0f;
        float readback_value = -1.0f;
        bool readback_life_max_ok = read_i32(g_adapter.runtime->field_life_max,
                                             instance, &readback_life_max);
        bool readback_life_ok = read_i32(g_adapter.runtime->field_life,
                                         instance, &readback_life);
        bool readback_damage_ok = read_i32(g_adapter.runtime->field_damage,
                                           instance, &readback_damage);
        bool readback_defense_ok = read_i32(g_adapter.runtime->field_defense,
                                            instance, &readback_defense);
        bool readback_knockback_ok = read_float(g_adapter.runtime->field_knockback_resist,
                                                instance, &readback_knockback);
        bool readback_scale_ok = read_float(g_adapter.runtime->field_scale,
                                            instance, &readback_scale);
        bool readback_value_ok = read_float(g_adapter.runtime->field_value,
                                            instance, &readback_value);
        const int32_t expected_damage = record->final_stats.damage < 0
            ? 0 : record->final_stats.damage;
        const int32_t expected_defense = record->final_stats.defense < 0
            ? 0 : record->final_stats.defense;
        const float expected_knockback = record->final_stats.knockback_resist;
        const float expected_scale = clamp_float(record->final_stats.scale);
        OR_STAT_DIAG_LOG("stat_write type=%u tier=%s vanillaLife=%lld finalLife=%lld "
                    "writeOk=%s readbackLifeMax=%s:%d readbackLife=%s:%d",
                    (unsigned)npc_type, or_elite_tier_name(spawn.tier),
                    (long long)vanilla->life_max,
                    (long long)record->final_stats.life_max,
                    write_ok ? "yes" : "no",
                    readback_life_max_ok ? "ok" : "fail", readback_life_max,
                    readback_life_ok ? "ok" : "fail", readback_life);
        OR_STAT_DIAG_LOG("stat_field type=%u field=damage expected=%d writeOk=%s "
                    "readback=%s:%d match=%s",
                    (unsigned)npc_type, expected_damage,
                    write_ok ? "yes" : "no",
                    readback_damage_ok ? "ok" : "fail", readback_damage,
                    readback_damage_ok && readback_damage == expected_damage ? "yes" : "no");
        OR_STAT_DIAG_LOG("stat_field type=%u field=defense expected=%d writeOk=%s "
                    "readback=%s:%d match=%s",
                    (unsigned)npc_type, expected_defense,
                    write_ok ? "yes" : "no",
                    readback_defense_ok ? "ok" : "fail", readback_defense,
                    readback_defense_ok && readback_defense == expected_defense ? "yes" : "no");
        OR_STAT_DIAG_LOG("stat_field type=%u field=knockBackResist expected=%.4f writeOk=%s "
                    "readback=%s:%.4f match=%s",
                    (unsigned)npc_type, (double)expected_knockback,
                    write_ok ? "yes" : "no",
                    readback_knockback_ok ? "ok" : "fail", (double)readback_knockback,
                    readback_knockback_ok && fabsf(readback_knockback - expected_knockback) <= 0.0001f
                        ? "yes" : "no");
        OR_STAT_DIAG_LOG("stat_field type=%u field=scale expected=%.4f writeOk=%s "
                    "readback=%s:%.4f match=%s",
                    (unsigned)npc_type, (double)expected_scale,
                    write_ok ? "yes" : "no",
                    readback_scale_ok ? "ok" : "fail", (double)readback_scale,
                    readback_scale_ok && fabsf(readback_scale - expected_scale) <= 0.0001f
                        ? "yes" : "no");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[MONEY_WRITE] type=%u tier=%s vanillaValue=%.2f finalValue=%lld "
               "writeOk=%s readbackValue=%s:%.2f backend=%s",
               (unsigned)npc_type, or_elite_tier_name(spawn.tier),
               (double)vanilla->money, (long long)record->final_stats.money,
               write_ok ? "yes" : "no", readback_value_ok ? "ok" : "fail",
               (double)readback_value,
               g_adapter.runtime->field_value ? "vanilla_value_override" : "unavailable");
        if (!write_ok) {
            OR_LOG(MOD_LOG_LEVEL_WARNING,
                   "Elite committed: concept=重构体, but native stat write was incomplete: type=%u",
                   (unsigned)npc_type);
        }
    }
    /* The name setter is the first player-facing P0-C marker. It runs only
     * after the real active=true commit and is never retried on later AI ticks.
     * Color, loot, and special-AI bridges remain closed. */
    {
        const char *prefix = tier_prefix(spawn.tier);
        const char *name_reason = NULL;
        bool name_ok = write_given_name_marker(instance, spawn.tier, &name_reason);
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[NAME_WRITE] type=%u tier=%s prefix=%s writeOk=%s reason=%s",
               (unsigned)npc_type, or_elite_tier_name(spawn.tier),
               prefix ? prefix : "unavailable", name_ok ? "yes" : "no",
               name_reason ? name_reason : "not_recorded");
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[REWRITE_MARK] concept=重构体 tier=%s prefix=%s marker=%s",
               or_elite_tier_name(spawn.tier), prefix ? prefix : "unavailable",
               name_ok ? "GivenName" : "log-only");
    }
    /* Main.NewText is opened only for the two highest currently-enabled
     * tiers. The ABI was verified during runtime probing, and the call is
     * made once, after the authoritative commit, never from SetDefaults or
     * the recurring AI path. A failed notice must not invalidate the already
     * committed elite or touch its gameplay state. */
    {
        bool terrain_notice_ok = or_broadcast_emit_world(
            &g_adapter.broadcast, g_adapter.runtime, record->rules.weather,
            record->rules.is_night, record->key.world_session_id, update_tick());
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[WORLD_NOTICE] requested=yes noticeOk=%s weather=%s night=%s",
               terrain_notice_ok ? "yes" : "no",
               or_weather_name(record->rules.weather),
               record->rules.is_night ? "yes" : "no");
    }
    {
        bool notice_requested = spawn.tier >= OR_TIER_CALAMITY;
        bool notice_ok = !notice_requested ||
            or_broadcast_emit_elite(&g_adapter.broadcast, g_adapter.runtime,
                                    spawn.tier, npc_type, spawn.key.generation_id,
                                    spawn.key.world_session_id, update_tick());
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ELITE_NOTICE] type=%u tier=%s requested=%s noticeOk=%s",
               (unsigned)npc_type, or_elite_tier_name(spawn.tier),
               notice_requested ? "yes" : "no",
               notice_requested ? (notice_ok ? "yes" : "no") : "skip");
    }
    OR_LOG(MOD_LOG_LEVEL_WARNING,
           "[SAFE_MODE] color/loot/special-AI skipped; name marker active; "
           "NewText notice only for calamity+");
    OR_LOG(MOD_LOG_LEVEL_INFO, "Elite committed: concept=重构体 prefix=%s type=%u tier=%s progress=%s mode=%s",
           tier_prefix(spawn.tier) ? tier_prefix(spawn.tier) : "unavailable",
           (unsigned)npc_type, or_elite_tier_name(spawn.tier),
           or_progress_stage_name(progress), or_game_mode_name(mode));
    return true;
}

static void setdefaults_postfix(patch_handle_t instance, void **args, void *result,
                                const patch_method_signature_t *sig_info) {
    OR_NativeBinding *binding;
    (void)args;
    (void)result;
    (void)sig_info;
    /* SetDefaults is observation only under v0.5. It must not roll, commit,
     * consume the active limit, or write native stats. */
    if (!instance || !g_adapter.runtime || !g_adapter.config || !g_adapter.state) return;
    binding = get_or_create_binding(instance);
    if (!binding) return;
    /* A new SetDefaults lifecycle invalidates any state left by a reused
     * Terraria NPC object, including a pending baseline that never activated. */
    if (binding->elite || binding->pending || binding->roll_resolved) {
        g_adapter.lifecycle_reuse_total += 1u;
        clear_binding(binding);
        binding = get_or_create_binding(instance);
        if (!binding) return;
    }
    g_adapter.setdefaults_total += 1u;
    binding->last_seen_tick = update_tick();
    binding->roll_resolved = false;
    binding->loot_seen = false;
    binding->ai_ticks = 0u;
    or_ai_runtime_init(&binding->ai_runtime);
    {
        OR_VanillaStats vanilla;
        uint32_t npc_type = 0;
        bool is_boss = false;
        bool is_town = false;
        bool is_friendly = false;
        bool active = false;
        const char *failed_field = NULL;
        if (read_vanilla_stats(instance, &npc_type, &vanilla, &is_boss, &is_town,
                               &is_friendly, &active, &failed_field)) {
            if (g_adapter.diagnostic_callback_count < OR_DIAGNOSTIC_LOG_LIMIT) {
                ++g_adapter.diagnostic_callback_count;
        OR_DIAG_LOG("setdefaults_callback count=%u type=%u vanillaLife=%lld life=%lld aiStyle=%d",
                            (unsigned)g_adapter.diagnostic_callback_count,
                            (unsigned)npc_type, (long long)vanilla.life_max,
                            (long long)vanilla.life_current, vanilla.ai_style);
            }
            binding->pending = true;
            binding->pending_vanilla = vanilla;
            binding->pending_npc_type = npc_type;
            binding->pending_is_boss = is_boss;
            binding->pending_is_town = is_town;
            binding->pending_is_friendly = is_friendly;
            OR_DIAG_LOG("setdefaults_pending type=%u vanillaLife=%lld life=%lld active=%s aiStyle=%d",
                        (unsigned)npc_type, (long long)vanilla.life_max,
                        (long long)vanilla.life_current, active ? "yes" : "no", vanilla.ai_style);
        } else {
            OR_DIAG_LOG("setdefaults_read_fail field=%s",
                        failed_field ? failed_field : "unknown");
            clear_binding(binding);
        }
    }
}

static void ai_postfix(
    patch_handle_t instance, void **args, void *result,
    const patch_method_signature_t *sig_info) {
    OR_NativeBinding *binding;
    OR_VanillaStats vanilla;
    uint32_t npc_type = 0;
    bool is_boss = false;
    bool is_town = false;
    bool is_friendly = false;
    bool active = false;
    bool single_player = false;
    const char *failed_field = NULL;
    float current_ratio;
    const OR_EliteRecord *record;
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance || !g_adapter.installed || !g_adapter.config || !g_adapter.state) return;
    g_adapter.ai_callback_total += 1u;
    log_lifecycle_health();
    if (g_adapter.diagnostic_ai_callback_count < OR_AI_DIAGNOSTIC_SAMPLE_LIMIT) {
        ++g_adapter.diagnostic_ai_callback_count;
        OR_LOG(MOD_LOG_LEVEL_WARNING, "[OR_DIAG] ai_callback sample=%u instance=%p",
               (unsigned)g_adapter.diagnostic_ai_callback_count, (void *)instance);
    }
    binding = get_or_create_binding(instance);
    if (!binding) return;
    binding->last_seen_tick = update_tick();
    if (!read_vanilla_stats(instance, &npc_type, &vanilla, &is_boss, &is_town,
                            &is_friendly, &active, &failed_field)) {
        OR_DIAG_LOG("ai_read_fail field=%s", failed_field ? failed_field : "unknown");
        return;
    }
    if (binding->pending) {
        if (!active) {
            /* A real NPC may expose active one or two AI calls after
             * SetDefaults. Template objects never become active and are
             * released after this short grace window. */
            binding->ai_ticks += 1u;
            if (binding->ai_ticks < 3u) return;
            clear_binding(binding);
            return;
        }
        /* Read the post-AI baseline at the real activation point. The
         * SetDefaults snapshot is retained only as a diagnostic fallback. */
        binding->pending = false;
        binding->roll_resolved = false;
        if (!commit_elite_from_baseline(instance, binding, &vanilla, npc_type,
                                        is_boss, is_town, is_friendly)) {
            /* Rejected rolls and failed transactions must not leave a binding
             * that depends on a future death hook for cleanup. */
            clear_binding(binding);
            return;
        }
    }
    if (!active && !binding->elite) {
        clear_binding(binding);
        return;
    }
    if (binding->elite) {
        binding->ai_ticks += 1u;
        record = or_state_find_const(g_adapter.state, binding->key);
        if (!record) {
            clear_binding(binding);
            return;
        }
        current_ratio = vanilla.life_max > 0
            ? (float)vanilla.life_current / (float)vanilla.life_max : 0.0f;
        if (!isfinite(current_ratio) || current_ratio < 0.0f) current_ratio = 0.0f;
        if (current_ratio > 1.0f) current_ratio = 1.0f;
        if (host_authority(&single_player, NULL)) {
            (void)or_ai_tick(&record->ai_plan, &binding->ai_runtime,
                             (uint32_t)binding->ai_ticks);
            if (or_ai_try_trigger_rage(&record->ai_plan, &binding->ai_runtime,
                                       binding->previous_life_ratio, current_ratio)) {
                int32_t damage = record->final_stats.damage;
                damage = clamp_i32((int64_t)llround((double)damage * 1.10));
                (void)field_write(g_adapter.runtime->field_damage, instance, &damage);
            }
        }
        binding->previous_life_ratio = current_ratio;
        return;
    }
    if (binding->roll_resolved) return;
    /* An active object may have missed SetDefaults because the binding table
     * was full. Capture it here and commit exactly once at this real boundary. */
    binding->roll_resolved = false;
    if (!commit_elite_from_baseline(instance, binding, &vanilla, npc_type,
                                    is_boss, is_town, is_friendly)) {
        clear_binding(binding);
    }
}

static void __attribute__((unused)) loot_postfix(
    patch_handle_t instance, void **args, void *result,
    const patch_method_signature_t *sig_info) {
    OR_NativeBinding *binding;
    OR_LootContext context;
    OR_LootResult loot;
    bool single_player = false;
    const OR_EliteRecord *record;
    (void)args;
    (void)result;
    (void)sig_info;
    if (!instance || !g_adapter.installed || !g_adapter.state || !g_adapter.config) return;
    binding = find_binding(instance);
    if (!binding || !binding->elite || binding->loot_seen) return;
    binding->loot_seen = true;
    if (!host_authority(&single_player, NULL)) return;
    record = or_state_find_const(g_adapter.state, binding->key);
    if (!record) {
        clear_binding(binding);
        return;
    }
    (void)or_state_mark_death(g_adapter.state, binding->key);
    memset(&context, 0, sizeof(context));
    context.host_authority = true;
    context.single_player = single_player;
    context.original_vanilla_loot_preserved = true;
    context.coin_backend_verified = g_adapter.runtime->field_value != NULL;
    context.progress = record->progress;
    context.tier = record->tier;
    context.terrain_snapshot = record->rules.terrain;
    context.reward_chance_bonus = record->rules.reward_chance_bonus;
    context.reward_quality_multiplier = record->rules.reward_quality_multiplier;
    context.random_seed = record->key.world_session_id ^ record->key.generation_id;
    memset(&loot, 0, sizeof(loot));
    if (or_loot_commit(g_adapter.state, binding->key, g_adapter.config,
                       &context, &loot) && loot.committed && loot.extra_reward) {
        /* Item registry/factory remains deliberately unverified; the vanilla
         * loot and the final NPC value are still preserved without inventing
         * an unsafe item-spawn call. */
        OR_LOG(MOD_LOG_LEVEL_DEBUG, "Extra reward policy committed: tier=%s pool=%s",
               or_elite_tier_name(record->tier), loot.pool_id ? loot.pool_id : "none");
    }
    clear_binding(binding);
}

static bool install_postfix(patch_handle_t method, postfix_callback_t callback,
                            patch_hook_id_t *out_id) {
    patch_hook_id_t hook_id;
    if (!method || !callback || !out_id || !patchlib_install_prepost_hook) return false;
    hook_id = patchlib_install_prepost_hook(method, NULL, callback);
    if (hook_id == PATCH_HOOK_INVALID_ID) return false;
    *out_id = hook_id;
    return true;
}

bool or_adapter_start(OR_Runtime *runtime, OR_Config *config, OR_StateStore *state) {
    size_t i;
    bool any_setdefaults = false;
    bool ai_hook_ok = false;
    if (g_adapter.installed) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[ADAPTER_STATE] duplicate start ignored; existing hooks remain installed");
        return true;
    }
    if (!runtime || !config || !state || !config->enable_gameplay_hooks ||
        !runtime->capabilities.patchlib_available ||
        !runtime->capabilities.stats_fields_resolved) return false;
    memset(&g_adapter, 0, sizeof(g_adapter));
    or_broadcast_init(&g_adapter.broadcast);
    g_adapter.runtime = runtime;
    g_adapter.config = config;
    g_adapter.state = state;
    if (runtime->field_color && patchlib_field_get_size && patchlib_field_get_type) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[COLOR_MARK] field=color size=%zu type=%d ready=%s",
               patchlib_field_get_size(runtime->field_color),
               (int)patchlib_field_get_type(runtime->field_color),
               runtime->capabilities.color_marker_ready ? "yes" : "no");
    } else {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[COLOR_MARK] field=color unavailable ready=no");
    }
    if (runtime->color_type && patchlib_type_get_name &&
        patchlib_type_get_namespace && patchlib_type_get_field &&
        patchlib_field_get_size && patchlib_field_get_type) {
        const char *const component_names[] = {"R", "G", "B", "A"};
        char *full_name = patchlib_type_get_full_name
            ? patchlib_type_get_full_name(runtime->color_type) : NULL;
        size_t component_index;
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[COLOR_TYPE] type=available namespace=%s name=%s full=%s",
               patchlib_type_get_namespace(runtime->color_type),
               patchlib_type_get_name(runtime->color_type),
               full_name ? full_name : "unavailable");
        free(full_name);
        for (component_index = 0u;
             component_index < sizeof(component_names) / sizeof(component_names[0]);
             ++component_index) {
            patch_handle_t component = patchlib_type_get_field(
                runtime->color_type, component_names[component_index]);
            if (handle_valid(component)) {
                OR_LOG(MOD_LOG_LEVEL_INFO,
                       "[COLOR_COMPONENT] name=%s size=%zu type=%d",
                       component_names[component_index],
                       patchlib_field_get_size(component),
                       (int)patchlib_field_get_type(component));
            }
            if (component) {
                release_adapter_handle(component);
            }
        }
    } else {
        OR_LOG(MOD_LOG_LEVEL_INFO, "[COLOR_TYPE] type=unavailable");
    }
    scan_visual_members(runtime);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[NAME_FIELD] property=%s getter=%s setter=%s",
           runtime->capabilities.given_name_property_ready ? "available" : "unavailable",
           runtime->method_given_name_get ? "available" : "unavailable",
           runtime->method_given_name_set ? "available" : "unavailable");
    OR_LOG(MOD_LOG_LEVEL_INFO, "[NAME_SOURCE] property=%s getter=%s",
           runtime->property_display_name && patchlib_property_get_name &&
                   patchlib_property_get_name(runtime->property_display_name)
               ? patchlib_property_get_name(runtime->property_display_name)
               : "unavailable",
           runtime->method_display_name_get ? "available" : "unavailable");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[NOTICE_API] NewText=%s params=%d colorType=%d",
           runtime->capabilities.new_text_ready ? "available" : "unavailable",
           runtime->main_new_text_arg_count,
           (int)runtime->main_new_text_color_type);
    OR_DIAG_LOG("adapter_ready setdefaults_candidates=%u stats_fields=ok",
                (unsigned)runtime->method_setdefaults_count);
    if (runtime->method_setdefaults_count == 0u && runtime->setdefaults_probe_seen) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[ENTRY_PROBE] SetDefaults rejected params=%d arg0=%d arg1=%d expected=int32,pointer",
               runtime->setdefaults_probe_param_count,
               (int)runtime->setdefaults_probe_arg_types[0],
               (int)runtime->setdefaults_probe_arg_types[1]);
    }
    for (i = 0; i < runtime->method_setdefaults_count; ++i) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[ENTRY_PROBE] SetDefaults candidate=%u params=2 abi=int32,pointer verified=yes",
               (unsigned)i);
        if (install_postfix(runtime->method_setdefaults[i], setdefaults_postfix,
                            &runtime->setdefaults_hook_ids[runtime->setdefaults_hook_count])) {
            runtime->setdefaults_hook_count += 1u;
            any_setdefaults = true;
        }
    }
    if (runtime->method_ai &&
        or_runtime_signature_matches(runtime->method_ai, true, PATCH_VOID, NULL, 0u)) {
        ai_hook_ok = install_postfix(runtime->method_ai, ai_postfix,
                                     &runtime->ai_hook_id);
    }
    if (runtime->method_strike_npc &&
        or_runtime_signature_matches(runtime->method_strike_npc, true,
                                     PATCH_INT32,
                                     (const patch_type_t[]){PATCH_INT32, PATCH_FLOAT,
                                         PATCH_INT32, PATCH_BOOL, PATCH_BOOL, PATCH_INT32},
                                     6u)) {
        or_death_probe_configure(runtime->field_type, runtime->field_life,
                                 runtime->field_active);
        (void)install_postfix(runtime->method_strike_npc, or_death_probe_postfix,
                              &runtime->strike_hook_id);
    }
    if (runtime->method_npcloot &&
        or_runtime_signature_matches(runtime->method_npcloot, true,
                                     PATCH_VOID, NULL, 0u)) {
        (void)install_postfix(runtime->method_npcloot,
                              or_death_probe_loot_postfix,
                              &runtime->loot_observer_hook_id);
    }
    /* v0.5 P0 gate: SetDefaults without a verified AI activation callback is
     * observation-only and is not installed, otherwise pending bindings could
     * accumulate without a real generation boundary. */
    if (!ai_hook_ok) {
        for (i = 0; i < runtime->setdefaults_hook_count; ++i) {
            if (runtime->setdefaults_hook_ids[i] != PATCH_HOOK_INVALID_ID &&
                patchlib_uninstall_hook) {
                (void)patchlib_uninstall_hook(runtime->setdefaults_hook_ids[i]);
            }
            runtime->setdefaults_hook_ids[i] = PATCH_HOOK_INVALID_ID;
        }
        runtime->setdefaults_hook_count = 0u;
        any_setdefaults = false;
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[P0_GATE] AI postfix unavailable; SetDefaults capture disabled");
    }
    OR_LOG(MOD_LOG_LEVEL_WARNING,
           "[SAFE_MODE] color/NPCLoot/extra-loot/special-AI disabled; "
           "name marker active after commit; NewText gated to calamity+");
    runtime->capabilities.exact_spawn_commit_resolved = any_setdefaults && ai_hook_ok;
    runtime->capabilities.exact_death_hook_resolved = false;
    runtime->capabilities.exact_loot_hook_resolved = runtime->loot_hook_id != PATCH_HOOK_INVALID_ID;
    runtime->capabilities.gameplay_enabled = any_setdefaults && ai_hook_ok;
    g_adapter.installed = runtime->capabilities.gameplay_enabled;
    OR_DIAG_LOG("adapter_hooks setdefaults=%u ai=%s loot=%s gameplay=%s safe_mode=on",
                (unsigned)runtime->setdefaults_hook_count,
                runtime->ai_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off",
                runtime->loot_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off",
                g_adapter.installed ? "on" : "off");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[DEATH_BOUNDARY] strikeObserver=%s deathHook=off lootObserver=%s "
           "lootHook=off rewards=off",
           runtime->strike_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off",
           runtime->loot_observer_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off");
    return g_adapter.installed;
}

void or_adapter_stop(void) {
    size_t i;
    /* Idempotent by design: runtime cleanup owns hook uninstallation, while
     * this function only releases adapter state. */
    if (!g_adapter.state) {
        memset(&g_adapter, 0, sizeof(g_adapter));
        return;
    }
    for (i = 0; i < OR_MAX_TRACKED_NPCS; ++i) {
        if (g_adapter.bindings[i].occupied) clear_binding(&g_adapter.bindings[i]);
    }
    memset(&g_adapter, 0, sizeof(g_adapter));
}
