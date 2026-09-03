#include "fw.h"
#include "fw_runtime.h"

#include "mod_logger.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
#include <android/log.h>
#endif

static FILE *g_runtime_log = NULL;

/* The loader logger is optional.  Keep an independent diagnostic channel so
 * the P0 gate remains observable when the module logger symbol is unavailable
 * or when a TEFManager export omits module-local logger records. */
static void fw_log(mod_log_level_t level, const char *fmt, ...) {
    va_list args;
    if (!fmt) return;

    va_start(args, fmt);
    if (mod_logger_write) {
        va_list logger_args;
        va_copy(logger_args, args);
        /* The public logger is variadic, so use a small formatted buffer for
         * the optional bridge to preserve identical output on every target. */
        {
            char message[512];
            vsnprintf(message, sizeof(message), fmt, logger_args);
            mod_logger_write(level, "OriginRewrite", "%s", message);
        }
        va_end(logger_args);
    }
    if (g_runtime_log) {
        va_list file_args;
        va_copy(file_args, args);
        vfprintf(g_runtime_log, fmt, file_args);
        fputc('\n', g_runtime_log);
        fflush(g_runtime_log);
        va_end(file_args);
    }
#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
    __android_log_vprint(ANDROID_LOG_INFO, "OriginRewrite", fmt, args);
#else
    fputs("[OriginRewrite] ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
#endif
    va_end(args);
}

void fw_core_set_log_file(const char *path) {
    if (g_runtime_log) {
        fclose(g_runtime_log);
        g_runtime_log = NULL;
    }
    if (path && path[0] != '\0') {
        g_runtime_log = fopen(path, "a");
    }
}

#define FW_LOG(level, ...) \
    do { fw_log((level), __VA_ARGS__); } while (0)

#define FW_DIAGNOSTIC_LOG_LIMIT 512u

/* TEFManager 导出的日志包不一定包含模组自身 logger 流；保持双通道，
 * Android 上用 logcat 过滤 OriginRewrite 即可独立确认每次回调与回读。 */
#define FW_DIAG(...) \
    do { \
        if (g_diag_count < FW_DIAGNOSTIC_LOG_LIMIT) { \
            ++g_diag_count; \
            FW_LOG(MOD_LOG_LEVEL_WARNING, "[FW_DIAG] " __VA_ARGS__); \
        } \
    } while (0)

typedef struct fw_binding {
    bool occupied;
    bool pending;        /* SetDefaults 已记录基准，等待真实激活提交 */
    bool roll_resolved;  /* 本次 SetDefaults->激活 生命周期只判定一次 */
    bool elite;
    uint32_t inactive_ticks;
    uint64_t generation;
    patch_handle_t instance;
    uint32_t npc_type;
    bool boss;
    bool town_npc;
    bool friendly;
    fw_vanilla_stats vanilla;
    fw_tier tier;
    fw_final_stats final_stats;
} fw_binding;

static fw_runtime g_runtime;
static fw_binding g_bindings[FW_MAX_BINDINGS];
static bool g_started = false;
static bool g_gameplay_ready = false;
static uint32_t g_diag_count = 0u;
static uint32_t g_setdefaults_hits = 0u;
static uint32_t g_ai_hits = 0u;

#define FW_HOOK_HIT_LOG_LIMIT 16u
static uint64_t g_fallback_tick = 0u;
static uint64_t g_next_generation = 1u;

static bool fw_handle_valid(patch_handle_t handle) {
    if (!handle) return false;
    return !patchlib_is_valid || patchlib_is_valid(handle);
}

static bool fw_field_read(patch_handle_t field, patch_handle_t instance,
                          void *out) {
    if (!fw_handle_valid(field) || !out || !patchlib_field_get_value) {
        return false;
    }
#if defined(__ANDROID__)
    if (!instance && patchlib_field_is_static &&
        patchlib_field_is_static(field) &&
        patchlib_field_is_const && patchlib_field_is_const(field)) {
        patchlib_field_get_value(field, NULL, out);
        return true;
    }
    if (!instance && patchlib_field_is_static &&
        patchlib_field_is_static(field) &&
        patchlib_field_is_thread_static &&
        patchlib_field_is_thread_static(field)) {
        patchlib_field_get_value(field, NULL, out);
        return true;
    }
    if (!instance && patchlib_field_is_static &&
        patchlib_field_is_static(field) &&
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

static bool fw_field_write(patch_handle_t field, patch_handle_t instance,
                           void *value) {
    if (!fw_handle_valid(field) || !value || !patchlib_field_set_value) {
        return false;
    }
#if defined(__ANDROID__)
    if (!instance && patchlib_field_is_static &&
        patchlib_field_is_static(field) &&
        patchlib_field_is_const && patchlib_field_is_const(field)) {
        return false;
    }
    if (!instance && patchlib_field_is_static &&
        patchlib_field_is_static(field) &&
        patchlib_field_is_thread_static &&
        patchlib_field_is_thread_static(field)) {
        return false;
    }
    if (!instance && patchlib_field_is_static &&
        patchlib_field_is_static(field) &&
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

static bool fw_read_i32(patch_handle_t field, patch_handle_t instance,
                        int32_t *out) {
    if (!out || !fw_handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_INT32) return false;
    return fw_field_read(field, instance, out);
}

static bool fw_read_bool(patch_handle_t field, patch_handle_t instance,
                         bool *out) {
    if (!out || !fw_handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_BOOL) return false;
    return fw_field_read(field, instance, out);
}

static bool fw_read_float(patch_handle_t field, patch_handle_t instance,
                          float *out) {
    if (!out || !fw_handle_valid(field) || !patchlib_field_get_type ||
        patchlib_field_get_type(field) != PATCH_FLOAT) return false;
    return fw_field_read(field, instance, out);
}

static bool fw_read_u64(patch_handle_t field, patch_handle_t instance,
                        uint64_t *out) {
    patch_type_t type;
    int64_t signed_value;
    int32_t i32;
    if (!out || !fw_handle_valid(field) || !patchlib_field_get_type) {
        return false;
    }
    type = patchlib_field_get_type(field);
    if (type == PATCH_UINT64) return fw_field_read(field, instance, out);
    if (type == PATCH_INT64) {
        signed_value = 0;
        if (!fw_field_read(field, instance, &signed_value)) return false;
        *out = signed_value > 0 ? (uint64_t)signed_value : 0u;
        return true;
    }
    if (type == PATCH_INT32) {
        i32 = 0;
        if (!fw_field_read(field, instance, &i32)) return false;
        *out = i32 > 0 ? (uint64_t)i32 : 0u;
        return true;
    }
    return false;
}

static fw_binding *fw_find_binding(patch_handle_t instance) {
    size_t i;
    if (!instance) return NULL;
    for (i = 0; i < FW_MAX_BINDINGS; ++i) {
        if (g_bindings[i].occupied &&
            g_bindings[i].instance == instance) {
            return &g_bindings[i];
        }
    }
    return NULL;
}

static fw_binding *fw_get_or_create_binding(patch_handle_t instance) {
    fw_binding *binding;
    size_t i;
    if (!instance) return NULL;
    binding = fw_find_binding(instance);
    if (binding) return binding;
    for (i = 0; i < FW_MAX_BINDINGS; ++i) {
        if (!g_bindings[i].occupied) {
            memset(&g_bindings[i], 0, sizeof(g_bindings[i]));
            g_bindings[i].occupied = true;
            g_bindings[i].instance = instance;
            return &g_bindings[i];
        }
    }
    return NULL;
}

static void fw_clear_binding(fw_binding *binding) {
    if (!binding) return;
    memset(binding, 0, sizeof(*binding));
}

static uint32_t fw_binding_index(const fw_binding *binding) {
    if (!binding) return 0u;
    return (uint32_t)(binding - g_bindings);
}

static uint64_t fw_world_session_id(void) {
    int32_t world_id = 0;
    if (g_runtime.main_world_id &&
        fw_read_i32(g_runtime.main_world_id, NULL, &world_id) &&
        world_id > 0) {
        return (uint64_t)(uint32_t)world_id;
    }
    return 1u;
}

static uint64_t fw_update_tick(void) {
    uint64_t tick = 0;
    if (g_runtime.main_update_count &&
        fw_read_u64(g_runtime.main_update_count, NULL, &tick)) {
        return tick;
    }
    return ++g_fallback_tick;
}

static bool fw_host_authority(bool *single_player, bool *known) {
    int32_t net_mode = 0;
    bool read_ok = false;
    if (single_player) *single_player = false;
    if (known) *known = false;
    if (g_runtime.main_net_mode) {
        read_ok = fw_read_i32(g_runtime.main_net_mode, NULL, &net_mode);
    }
    /* netMode 不可读时按本地权威处理，不能让未知读取关闭核心链路。 */
    if (read_ok && net_mode == 1) {
        if (known) *known = true;
        return false;
    }
    if (known) *known = read_ok && (net_mode == 0 || net_mode == 2);
    if (single_player) *single_player = !read_ok || net_mode != 2;
    return true;
}

static fw_game_mode fw_current_mode(void) {
    int32_t mode = 0;
    bool zenith = false;
    if (g_runtime.main_game_mode) {
        (void)fw_read_i32(g_runtime.main_game_mode, NULL, &mode);
    }
    if (g_runtime.main_zenith_world) {
        (void)fw_read_bool(g_runtime.main_zenith_world, NULL, &zenith);
    }
    if (zenith) return FW_MODE_ZENITH;
    if (mode == 1) return FW_MODE_EXPERT;
    if (mode == 2) return FW_MODE_MASTER;
    if (mode == 3) return FW_MODE_JOURNEY;
    return FW_MODE_CLASSIC;
}

static fw_progress fw_current_progress(void) {
    bool hard = false;
    bool mech = false;
    bool plant = false;
    bool golem = false;
    bool moonlord = false;
    if (g_runtime.main_hard_mode) {
        (void)fw_read_bool(g_runtime.main_hard_mode, NULL, &hard);
    }
    if (g_runtime.npc_downed_mech) {
        (void)fw_read_bool(g_runtime.npc_downed_mech, NULL, &mech);
    }
    if (g_runtime.npc_downed_plant) {
        (void)fw_read_bool(g_runtime.npc_downed_plant, NULL, &plant);
    }
    if (g_runtime.npc_downed_golem) {
        (void)fw_read_bool(g_runtime.npc_downed_golem, NULL, &golem);
    }
    if (g_runtime.npc_downed_moonlord) {
        (void)fw_read_bool(g_runtime.npc_downed_moonlord, NULL, &moonlord);
    }
    if (moonlord) return FW_PROGRESS_ENDGAME;
    if (plant || golem) return FW_PROGRESS_POST_PLANTERA;
    if (mech) return FW_PROGRESS_PRE_PLANTERA;
    if (hard) return FW_PROGRESS_HARDMODE_PRE_MECH;
    return FW_PROGRESS_PRE_HARDMODE;
}

static int32_t fw_clamp_i32(int64_t value) {
    if (value > INT32_MAX) return INT32_MAX;
    if (value < 0) return 0;
    return (int32_t)value;
}

static float fw_clamp_float(double value) {
    if (!isfinite(value) || value <= 0.0) return 0.0f;
    if (value >= (double)FLT_MAX) return FLT_MAX;
    return (float)value;
}

static bool fw_read_vanilla_stats(patch_handle_t instance,
                                  uint32_t *npc_type,
                                  fw_vanilla_stats *vanilla,
                                  bool *boss,
                                  bool *town_npc,
                                  bool *friendly,
                                  bool *active,
                                  const char **failed_field) {
    int32_t type = 0;
    int32_t life_max = 0;
    int32_t life = 0;
    float knockback = 0.0f;
    float scale = 1.0f;
    float slots = 1.0f;
    float value = 0.0f;
    bool local_active = false;
    if (failed_field) *failed_field = NULL;
    if (!vanilla || !npc_type) {
        if (failed_field) *failed_field = "arguments";
        return false;
    }
    if (!fw_read_i32(g_runtime.field_type, instance, &type)) {
        if (failed_field) *failed_field = "type";
        return false;
    }
    if (!fw_read_i32(g_runtime.field_life_max, instance, &life_max)) {
        if (failed_field) *failed_field = "lifeMax";
        return false;
    }
    if (type <= 0 || life_max <= 0) {
        if (failed_field) *failed_field = "value";
        return false;
    }
    if (!fw_read_i32(g_runtime.field_life, instance, &life)) life = life_max;
    if (life <= 0) life = life_max;

    vanilla->life_max = life_max;
    vanilla->life = life;
    vanilla->damage = 0;
    vanilla->defense = 0;
    vanilla->knockback_resist = 0.0f;
    vanilla->scale = 1.0f;
    vanilla->npc_slots = 1.0f;
    vanilla->money = 0.0f;

    if (g_runtime.field_damage) {
        int32_t damage = 0;
        (void)fw_read_i32(g_runtime.field_damage, instance, &damage);
        vanilla->damage = damage > 0 ? damage : 0;
    }
    if (g_runtime.field_defense) {
        int32_t defense = 0;
        (void)fw_read_i32(g_runtime.field_defense, instance, &defense);
        vanilla->defense = defense > 0 ? defense : 0;
    }
    if (g_runtime.field_knockback_resist) {
        (void)fw_read_float(g_runtime.field_knockback_resist, instance,
                            &knockback);
        vanilla->knockback_resist =
            isfinite(knockback) && knockback >= 0.0f ? knockback : 0.0f;
    }
    if (g_runtime.field_scale) {
        (void)fw_read_float(g_runtime.field_scale, instance, &scale);
        vanilla->scale =
            isfinite(scale) && scale > 0.0f ? scale : 1.0f;
    }
    if (g_runtime.field_npc_slots) {
        (void)fw_read_float(g_runtime.field_npc_slots, instance, &slots);
        vanilla->npc_slots =
            isfinite(slots) && slots > 0.0f ? slots : 1.0f;
    }
    if (g_runtime.field_value) {
        (void)fw_read_float(g_runtime.field_value, instance, &value);
        vanilla->money =
            isfinite(value) && value > 0.0f
                ? (float)(int64_t)llround((double)value) : 0.0f;
    }
    if (active) *active = false;
    if (boss) *boss = false;
    if (town_npc) *town_npc = false;
    if (friendly) *friendly = false;
    if (g_runtime.field_active) {
        (void)fw_read_bool(g_runtime.field_active, instance, &local_active);
        if (active) *active = local_active;
    }
    if (g_runtime.field_boss && boss) {
        (void)fw_read_bool(g_runtime.field_boss, instance, boss);
    }
    if (g_runtime.field_town_npc && town_npc) {
        (void)fw_read_bool(g_runtime.field_town_npc, instance, town_npc);
    }
    if (g_runtime.field_friendly && friendly) {
        (void)fw_read_bool(g_runtime.field_friendly, instance, friendly);
    }
    *npc_type = (uint32_t)type;
    return true;
}

static bool fw_apply_final_stats(patch_handle_t instance,
                                 const fw_final_stats *stats,
                                 const fw_vanilla_stats *vanilla) {
    bool ok = true;
    int32_t i32;
    float f32;
    int32_t width = 0;
    int32_t height = 0;
    float vanilla_scale = 1.0f;
    bool have_body = false;

    if (!instance || !stats || !vanilla) return false;
    if (g_runtime.field_width && g_runtime.field_height &&
        fw_read_i32(g_runtime.field_width, instance, &width) &&
        fw_read_i32(g_runtime.field_height, instance, &height) &&
        g_runtime.field_scale &&
        fw_read_float(g_runtime.field_scale, instance, &vanilla_scale) &&
        vanilla_scale > 0.0f && isfinite(vanilla_scale)) {
        have_body = true;
    }

    i32 = fw_clamp_i32(stats->life_max);
    ok = fw_field_write(g_runtime.field_life_max, instance, &i32) && ok;
    i32 = fw_clamp_i32(stats->life);
    ok = fw_field_write(g_runtime.field_life, instance, &i32) && ok;
    i32 = stats->damage < 0 ? 0 : stats->damage;
    if (g_runtime.field_damage) {
        ok = fw_field_write(g_runtime.field_damage, instance, &i32) && ok;
    }
    i32 = stats->defense < 0 ? 0 : stats->defense;
    if (g_runtime.field_defense) {
        ok = fw_field_write(g_runtime.field_defense, instance, &i32) && ok;
    }
    f32 = stats->knockback_resist;
    if (g_runtime.field_knockback_resist) {
        ok = fw_field_write(g_runtime.field_knockback_resist, instance,
                            &f32) && ok;
    }
    f32 = fw_clamp_float(stats->scale);
    if (g_runtime.field_scale) {
        ok = fw_field_write(g_runtime.field_scale, instance, &f32) && ok;
    }
    f32 = fw_clamp_float(stats->money);
    if (g_runtime.field_value) {
        ok = fw_field_write(g_runtime.field_value, instance, &f32) && ok;
    }
    if (have_body && stats->scale > 0.0f) {
        double body_ratio = (double)stats->scale /
                            (double)vanilla_scale;
        width = fw_clamp_i32((int64_t)llround((double)width * body_ratio));
        height = fw_clamp_i32((int64_t)llround((double)height * body_ratio));
        ok = fw_field_write(g_runtime.field_width, instance, &width) && ok;
        ok = fw_field_write(g_runtime.field_height, instance, &height) && ok;
    }
    if (g_runtime.field_npc_slots) {
        f32 = fw_clamp_float(stats->npc_slots);
        ok = fw_field_write(g_runtime.field_npc_slots, instance, &f32) && ok;
    }
    (void)vanilla;
    return ok;
}

static void fw_commit_elite(fw_binding *binding, patch_handle_t instance,
                            const fw_vanilla_stats *vanilla,
                            uint32_t npc_type, bool boss, bool town_npc,
                            bool friendly) {
    fw_spawn_input input;
    fw_final_stats final_stats;
    fw_roll_outcome roll;
    fw_tier tier = FW_TIER_NONE;
    uint64_t session;
    uint64_t tick;
    uint64_t seed;
    bool single_player = false;
    bool authority_known = false;
    bool write_ok;
    int32_t readback_life_max = -1;
    int32_t readback_life = -1;
    bool readback_life_max_ok = false;
    bool readback_life_ok = false;

    if (!binding || !vanilla || binding->roll_resolved) return;
    binding->roll_resolved = true;

    if (!fw_host_authority(&single_player, &authority_known)) {
        FW_DIAG("authority_skip type=%u reason=multiplayer_client",
                (unsigned)npc_type);
        return;
    }
    if (boss || town_npc || friendly || npc_type == 0u) {
        FW_DIAG("exclude type=%u boss=%d town=%d friendly=%d",
                (unsigned)npc_type, boss ? 1 : 0,
                town_npc ? 1 : 0, friendly ? 1 : 0);
        return;
    }

    session = fw_world_session_id();
    tick = fw_update_tick();
    seed = session ^ (uint64_t)(uintptr_t)instance ^ tick ^
           ((uint64_t)npc_type << 32);

    input.mode = fw_current_mode();
    input.progress = fw_current_progress();
    input.vanilla = *vanilla;

    roll = fw_roll_elite(input.mode, input.progress, seed, &tier);
    if (roll != FW_ROLL_ELITE) {
        FW_DIAG("roll_skip type=%u mode=%s progress=%s reason=%s",
                (unsigned)npc_type, fw_mode_name(input.mode),
                fw_progress_name(input.progress),
                roll == FW_ROLL_NOT_ELITE ? "chance" : "tier_disabled");
        return;
    }
    if (!fw_stats_build(&input, tier, &final_stats)) {
        FW_DIAG("stats_fail type=%u tier=%s",
                (unsigned)npc_type, fw_tier_name(tier));
        return;
    }
    write_ok = fw_apply_final_stats(instance, &final_stats, vanilla);
    if (g_runtime.field_life_max) {
        readback_life_max_ok = fw_read_i32(g_runtime.field_life_max,
                                           instance, &readback_life_max);
    }
    if (g_runtime.field_life) {
        readback_life_ok = fw_read_i32(g_runtime.field_life,
                                       instance, &readback_life);
    }

    binding->elite = write_ok;
    binding->tier = tier;
    binding->final_stats = final_stats;
    binding->generation = g_next_generation++;
    if (g_next_generation == 0u) g_next_generation = 1u;

    FW_DIAG("stat_write type=%u slot=%u tier=%s vanillaLife=%lld "
            "finalLife=%lld writeOk=%s readbackLifeMax=%s:%d "
            "readbackLife=%s:%d mode=%s progress=%s single=%d",
            (unsigned)npc_type, fw_binding_index(binding),
            fw_tier_name(tier), (long long)vanilla->life_max,
            (long long)final_stats.life_max, write_ok ? "yes" : "no",
            readback_life_max_ok ? "ok" : "fail", readback_life_max,
            readback_life_ok ? "ok" : "fail", readback_life,
            fw_mode_name(input.mode), fw_progress_name(input.progress),
            single_player ? 1 : 0);
    FW_LOG(MOD_LOG_LEVEL_INFO,
           "Elite committed: type=%u tier=%s mode=%s progress=%s",
           (unsigned)npc_type, fw_tier_name(tier),
           fw_mode_name(input.mode), fw_progress_name(input.progress));
}

static void fw_setdefaults_postfix(patch_handle_t instance, void **args,
                                   void *result,
                                   const patch_method_signature_t *sig_info) {
    fw_binding *binding;
    fw_vanilla_stats vanilla;
    uint32_t npc_type = 0;
    bool boss = false;
    bool town_npc = false;
    bool friendly = false;
    bool active = false;
    const char *failed_field = NULL;

    ++g_setdefaults_hits;
    if (g_setdefaults_hits <= FW_HOOK_HIT_LOG_LIMIT) {
        FW_LOG(MOD_LOG_LEVEL_INFO,
               "[HOOK_HIT] SetDefaults count=%u instance=%p",
               (unsigned)g_setdefaults_hits, (void *)instance);
    }
    (void)args;
    (void)result;
    (void)sig_info;

    if (!instance || !g_started || !g_runtime.stats_fields_resolved) return;
    binding = fw_get_or_create_binding(instance);
    if (!binding) return;

    /* NPC 对象池会复用同一指针；新的 SetDefaults 生命周期必须清掉旧状态。 */
    if (binding->elite || binding->pending || binding->roll_resolved) {
        fw_clear_binding(binding);
        binding = fw_get_or_create_binding(instance);
        if (!binding) return;
    }

    /* v0.5 核心不变量：SetDefaults 只记录待初始化与基准，不 roll、不改属性。 */
    binding->pending = false;
    binding->roll_resolved = false;
    binding->inactive_ticks = 0u;
    if (!fw_read_vanilla_stats(instance, &npc_type, &vanilla,
                               &boss, &town_npc, &friendly, &active,
                               &failed_field)) {
        FW_DIAG("setdefaults_read_fail field=%s",
                failed_field ? failed_field : "unknown");
        return;
    }
    binding->pending = true;
    binding->npc_type = npc_type;
    binding->boss = boss;
    binding->town_npc = town_npc;
    binding->friendly = friendly;
    binding->vanilla = vanilla;
    FW_DIAG("setdefaults_pending type=%u vanillaLife=%lld life=%lld",
            (unsigned)npc_type, (long long)vanilla.life_max,
            (long long)vanilla.life);
}

static void fw_ai_postfix(patch_handle_t instance, void **args,
                          void *result,
                          const patch_method_signature_t *sig_info) {
    fw_binding *binding;
    fw_vanilla_stats vanilla;
    uint32_t npc_type = 0;
    bool boss = false;
    bool town_npc = false;
    bool friendly = false;
    bool active = false;
    const char *failed_field = NULL;

    ++g_ai_hits;
    if (g_ai_hits <= FW_HOOK_HIT_LOG_LIMIT) {
        FW_LOG(MOD_LOG_LEVEL_INFO, "[HOOK_HIT] AI count=%u instance=%p",
               (unsigned)g_ai_hits, (void *)instance);
    }
    (void)args;
    (void)result;
    (void)sig_info;

    if (!instance || !g_started || !g_runtime.stats_fields_resolved) return;
    binding = fw_get_or_create_binding(instance);
    if (!binding) return;
    if (!fw_read_vanilla_stats(instance, &npc_type, &vanilla,
                               &boss, &town_npc, &friendly, &active,
                               &failed_field)) {
        FW_DIAG("ai_read_fail field=%s",
                failed_field ? failed_field : "unknown");
        if (binding->pending) {
            binding->inactive_ticks += 1u;
            if (binding->inactive_ticks > FW_INACTIVE_GRACE_TICKS) {
                fw_clear_binding(binding);
            }
        }
        return;
    }

    if (!active && !binding->elite) {
        /* 模板对象不会激活；给几次 AI 宽限后清掉，避免长期占槽。 */
        binding->inactive_ticks += 1u;
        if (binding->inactive_ticks > FW_INACTIVE_GRACE_TICKS) {
            fw_clear_binding(binding);
        }
        return;
    }

    if (binding->pending) {
        binding->pending = false;
    }
    if (binding->elite) {
        return; /* 属性只应用一次；后续 AI 状态机在此之后接入。 */
    }
    if (binding->roll_resolved) return;
    fw_commit_elite(binding, instance, &vanilla, npc_type,
                    boss, town_npc, friendly);
}

static bool fw_install_postfix(patch_handle_t method,
                               postfix_callback_t callback,
                               patch_hook_id_t *out_id) {
    patch_hook_id_t hook_id;
    if (!method || !callback || !patchlib_install_prepost_hook) return false;
    hook_id = patchlib_install_prepost_hook(method, NULL, callback);
    if (hook_id == PATCH_HOOK_INVALID_ID) return false;
    *out_id = hook_id;
    return true;
}

bool fw_core_init(void) {
    size_t i;
    bool any_setdefaults = false;
    bool runtime_ok;
    if (g_started) return g_gameplay_ready;

    memset(g_bindings, 0, sizeof(g_bindings));
    g_diag_count = 0u;
    g_setdefaults_hits = 0u;
    g_ai_hits = 0u;
    g_fallback_tick = 0u;
    g_gameplay_ready = false;

    runtime_ok = fw_runtime_probe(&g_runtime);
    if (!runtime_ok) {
        FW_LOG(MOD_LOG_LEVEL_WARNING,
               "Runtime probe failed; gameplay hooks remain disabled");
        fw_runtime_cleanup(&g_runtime);
        g_started = true;
        return false;
    }
    if (!g_runtime.stats_fields_resolved ||
        g_runtime.method_setdefaults_count == 0u) {
        FW_LOG(MOD_LOG_LEVEL_WARNING,
               "Core fields or SetDefaults methods not resolved; "
               "gameplay hooks disabled");
        fw_runtime_cleanup(&g_runtime);
        g_started = true;
        return false;
    }

    FW_DIAG("adapter_ready setdefaults_candidates=%u main_fields=%s",
            (unsigned)g_runtime.method_setdefaults_count,
            g_runtime.main_fields_resolved ? "ok" : "optional");

    for (i = 0; i < g_runtime.method_setdefaults_count; ++i) {
        FW_LOG(MOD_LOG_LEVEL_INFO,
               "[ENTRY_PROBE] SetDefaults candidate=%u name=%s params=%d",
               (unsigned)i,
               patchlib_method_get_name &&
                       patchlib_method_get_name(g_runtime.method_setdefaults[i])
                   ? patchlib_method_get_name(g_runtime.method_setdefaults[i])
                   : "unknown",
               patchlib_method_get_param_count
                   ? patchlib_method_get_param_count(
                         g_runtime.method_setdefaults[i])
                   : -1);
        if (fw_install_postfix(g_runtime.method_setdefaults[i],
                               fw_setdefaults_postfix,
                               &g_runtime.setdefaults_hooks[
                                   g_runtime.setdefaults_hook_count])) {
            ++g_runtime.setdefaults_hook_count;
            any_setdefaults = true;
        }
    }

    if (g_runtime.ai_known_dispatcher ||
        fw_signature_matches(g_runtime.method_ai, true, PATCH_VOID,
                             NULL, 0u)) {
        FW_LOG(MOD_LOG_LEVEL_INFO,
               "[ENTRY_PROBE] AI candidate name=%s params=%d dispatcher=%s",
               patchlib_method_get_name && g_runtime.method_ai &&
                       patchlib_method_get_name(g_runtime.method_ai)
                   ? patchlib_method_get_name(g_runtime.method_ai)
                   : "unknown",
               patchlib_method_get_param_count && g_runtime.method_ai
                   ? patchlib_method_get_param_count(g_runtime.method_ai)
                   : -1,
               g_runtime.ai_known_dispatcher ? "metadata-fallback" : "exact");
        if (!fw_install_postfix(g_runtime.method_ai, fw_ai_postfix,
                                &g_runtime.ai_hook)) {
            FW_LOG(MOD_LOG_LEVEL_WARNING,
                   "NPC AI hook installation failed; elite gameplay disabled");
        }
    } else {
        FW_LOG(MOD_LOG_LEVEL_WARNING,
               "NPC AI entry not resolved; elite gameplay disabled");
    }

    if (g_runtime.method_npcloot &&
        fw_signature_matches(g_runtime.method_npcloot, true, PATCH_VOID,
                             NULL, 0u)) {
        /* 掉落是下一层；核心链路版先不安装，避免不可验证逻辑干扰。 */
        FW_DIAG("npcloot_candidate_ready");
    }

    g_gameplay_ready = any_setdefaults &&
                       g_runtime.ai_hook != PATCH_HOOK_INVALID_ID;
    g_started = true;
    FW_DIAG("adapter_hooks setdefaults=%u ai=%s gameplay=%s",
            (unsigned)g_runtime.setdefaults_hook_count,
            g_runtime.ai_hook != PATCH_HOOK_INVALID_ID ? "on" : "off",
            g_gameplay_ready ? "on" : "off");
    FW_LOG(MOD_LOG_LEVEL_INFO,
           "[HOOK_STATE] version=1.0.6-file-beacon setdefaults=%u ai=%s "
           "gameplay=%s; waiting_for_runtime_callbacks",
           (unsigned)g_runtime.setdefaults_hook_count,
           g_runtime.ai_hook != PATCH_HOOK_INVALID_ID ? "on" : "off",
           g_gameplay_ready ? "on" : "off");
    return g_gameplay_ready;
}

void fw_core_shutdown(void) {
    memset(g_bindings, 0, sizeof(g_bindings));
    fw_runtime_cleanup(&g_runtime);
    g_started = false;
    g_gameplay_ready = false;
    FW_LOG(MOD_LOG_LEVEL_INFO, "Origin Rewrite core unloaded");
}

bool fw_core_gameplay_ready(void) {
    return g_gameplay_ready;
}
