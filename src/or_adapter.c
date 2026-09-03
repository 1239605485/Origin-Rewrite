#include "or_adapter.h"

#include "or_log.h"
#include "or_ai.h"
#include "or_loot.h"
#include "or_spawn.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/struct/string.h"

#include <limits.h>
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

#define OR_DIAG_LOG(...) \
    do { \
        if (g_adapter.diagnostic_log_count < OR_DIAGNOSTIC_LOG_LIMIT) { \
            ++g_adapter.diagnostic_log_count; \
            or_log_write(MOD_LOG_LEVEL_WARNING, "[OR_DIAG] " __VA_ARGS__); \
        } \
    } while (0)

typedef struct OR_NativeBinding {
    bool occupied;
    bool roll_resolved;
    bool elite;
    bool pending;
    bool loot_seen;
    patch_handle_t instance;
    OR_InstanceKey key;
    OR_VanillaStats pending_vanilla;
    uint32_t pending_npc_type;
    bool pending_is_boss;
    bool pending_is_town;
    bool pending_is_friendly;
    OR_AiRuntimeState ai_runtime;
    uint64_t ai_ticks;
    float previous_life_ratio;
} OR_NativeBinding;

typedef struct OR_Adapter {
    OR_Runtime *runtime;
    OR_Config *config;
    OR_StateStore *state;
    OR_NativeBinding bindings[OR_MAX_TRACKED_NPCS];
    uint64_t fallback_tick;
    uint32_t diagnostic_log_count;
    uint32_t diagnostic_callback_count;
    uint32_t diagnostic_ai_callback_count;
    bool installed;
} OR_Adapter;

static OR_Adapter g_adapter;

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
            memset(&g_adapter.bindings[i], 0, sizeof(g_adapter.bindings[i]));
            g_adapter.bindings[i].occupied = true;
            g_adapter.bindings[i].instance = instance;
            or_ai_runtime_init(&g_adapter.bindings[i].ai_runtime);
            OR_DIAG_LOG("binding_reclaimed slot=%u", (unsigned)i);
            return &g_adapter.bindings[i];
        }
    }
    OR_DIAG_LOG("binding_table_full instance=%p", (void *)instance);
    return NULL;
}

static void clear_binding(OR_NativeBinding *binding) {
    if (!binding) return;
    if (binding->elite && g_adapter.state) {
        (void)or_state_cleanup(g_adapter.state, binding->key);
    }
    memset(binding, 0, sizeof(*binding));
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
        ? reason : "ok";
    return true;

fail:
    if (failure_reason) *failure_reason = reason;
    return false;
}

static bool show_elite_notice(OR_EliteTier tier, uint32_t npc_type) {
    static uint64_t last_notice_time;
    time_t now;
    char message[192];
    patch_handle_t message_handle;
    const char *prefix;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    int32_t red_i;
    int32_t green_i;
    int32_t blue_i;
    uint64_t ignored_return = 0u;
    void *args[4] = {NULL, NULL, NULL, NULL};
    (void)npc_type;
    if (!g_adapter.runtime || !g_adapter.runtime->capabilities.new_text_ready ||
        !g_adapter.runtime->method_main_new_text || !patchlib_string_create ||
        !patchlib_method_invoke_args) return false;
    now = time(NULL);
    if (last_notice_time != 0u && (uint64_t)now < last_notice_time + 2u) return false;
    prefix = tier_prefix(tier);
    if (!prefix || snprintf(message, sizeof(message), "%s已出现", prefix) >=
                       (int)sizeof(message)) return false;
    message_handle = patchlib_string_create(message);
    if (!handle_valid(message_handle)) return false;
    red = (uint8_t)((tier_color(tier) >> 16) & 0xFFu);
    green = (uint8_t)((tier_color(tier) >> 8) & 0xFFu);
    blue = (uint8_t)(tier_color(tier) & 0xFFu);
    red_i = red;
    green_i = green;
    blue_i = blue;
    args[0] = &message_handle;
    if (g_adapter.runtime->main_new_text_arg_count == 4) {
        if (g_adapter.runtime->main_new_text_color_type == PATCH_UINT8) {
            args[1] = &red;
            args[2] = &green;
            args[3] = &blue;
        } else {
            args[1] = &red_i;
            args[2] = &green_i;
            args[3] = &blue_i;
        }
    }
    if (!patchlib_method_invoke_args(g_adapter.runtime->method_main_new_text,
                                     PATCH_NULL, &ignored_return, args)) return false;
    last_notice_time = (uint64_t)now;
    return true;
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
        OR_LOG(MOD_LOG_LEVEL_INFO, "[ELITE_NOTICE] type=%u tier=%s noticeOk=%s",
               (unsigned)npc_type, or_elite_tier_name(tier),
               show_elite_notice(tier, npc_type) ? "yes" : "no");
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
    context.terrain = (OR_TerrainSnapshot){OR_DEPTH_SURFACE, OR_BIOME_FOREST, OR_SPECIAL_NONE};
    context.weather = OR_WEATHER_CLEAR;
    context.is_night = false;
    context.archetype = OR_AI_ARCHETYPE_MELEE;
    context.max_active_elites = g_adapter.config->max_active_elites;
    context.transient_prepare = false;
    context.vanilla = *vanilla;
    memset(&spawn, 0, sizeof(spawn));
    if (!or_spawn_try_commit(g_adapter.config, g_adapter.state, &context,
                             session ^ (uint64_t)(uintptr_t)instance ^ tick, &spawn) ||
        !spawn.committed) {
        OR_DIAG_LOG("commit_fail type=%u vanillaLife=%lld reason=%s",
                    (unsigned)npc_type, (long long)vanilla->life_max,
                    or_spawn_reject_reason_name(spawn.reason));
        return false;
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
    {
        bool write_ok = apply_final_stats(instance, &record->final_stats);
        int32_t readback_life_max = -1;
        int32_t readback_life = -1;
        bool readback_life_max_ok = read_i32(g_adapter.runtime->field_life_max,
                                             instance, &readback_life_max);
        bool readback_life_ok = read_i32(g_adapter.runtime->field_life,
                                         instance, &readback_life);
        OR_DIAG_LOG("stat_write type=%u tier=%s vanillaLife=%lld finalLife=%lld "
                    "writeOk=%s readbackLifeMax=%s:%d readbackLife=%s:%d",
                    (unsigned)npc_type, or_elite_tier_name(spawn.tier),
                    (long long)vanilla->life_max,
                    (long long)record->final_stats.life_max,
                    write_ok ? "yes" : "no",
                    readback_life_max_ok ? "ok" : "fail", readback_life_max,
                    readback_life_ok ? "ok" : "fail", readback_life);
        if (!write_ok) {
            OR_LOG(MOD_LOG_LEVEL_WARNING,
                   "Elite committed: concept=重构体, but native stat write was incomplete: type=%u",
                   (unsigned)npc_type);
        }
    }
    /* The name setter is the first player-facing P0-C marker. It runs only
     * after the real active=true commit and is never retried on later AI ticks.
     * Color, chat, loot, and special-AI bridges remain closed. */
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
    OR_LOG(MOD_LOG_LEVEL_WARNING,
           "[SAFE_MODE] color/NewText/loot/special-AI skipped; name-only P0-C");
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
        clear_binding(binding);
        binding = get_or_create_binding(instance);
        if (!binding) return;
    }
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
                OR_DIAG_LOG("setdefaults_callback count=%u type=%u vanillaLife=%lld life=%lld",
                            (unsigned)g_adapter.diagnostic_callback_count,
                            (unsigned)npc_type, (long long)vanilla.life_max,
                            (long long)vanilla.life_current);
            }
            binding->pending = true;
            binding->pending_vanilla = vanilla;
            binding->pending_npc_type = npc_type;
            binding->pending_is_boss = is_boss;
            binding->pending_is_town = is_town;
            binding->pending_is_friendly = is_friendly;
            OR_DIAG_LOG("setdefaults_pending type=%u vanillaLife=%lld life=%lld active=%s",
                        (unsigned)npc_type, (long long)vanilla.life_max,
                        (long long)vanilla.life_current, active ? "yes" : "no");
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
    if (g_adapter.diagnostic_ai_callback_count < OR_DIAGNOSTIC_LOG_LIMIT) {
        ++g_adapter.diagnostic_ai_callback_count;
        OR_LOG(MOD_LOG_LEVEL_WARNING, "[OR_DIAG] ai_callback count=%u instance=%p",
               (unsigned)g_adapter.diagnostic_ai_callback_count, (void *)instance);
    }
    binding = get_or_create_binding(instance);
    if (!binding) return;
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
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[NAME_FIELD] property=%s getter=%s setter=%s",
           runtime->capabilities.given_name_property_ready ? "available" : "unavailable",
           runtime->method_given_name_get ? "available" : "unavailable",
           runtime->method_given_name_set ? "available" : "unavailable");
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
           "[SAFE_MODE] color/NewText/NPCLoot/extra-loot/special-AI disabled; name marker gated after commit");
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
