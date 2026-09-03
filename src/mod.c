#include <stddef.h>

#include "mod_core.h"
#include "mod_logger.h"

#include "or_config.h"
#include "or_adapter.h"
#include "or_runtime.h"
#include "or_state.h"

#if defined(__ANDROID__) && defined(ORIGINREWRITE_USE_ANDROID_LOG)
#include <android/log.h>
#define OR_STARTUP_EMIT(...) \
    __android_log_print(ANDROID_LOG_INFO, "OriginRewrite", __VA_ARGS__)
#else
#define OR_STARTUP_EMIT(...) do { } while (0)
#endif

void (*mod_logger_write)(mod_log_level_t level, const char *tag, const char *fmt, ...) = NULL;

static OR_Config g_config;
static OR_StateStore g_state;
static OR_Runtime g_runtime;

#define OR_LOG(level, ...) \
    do { \
        if (mod_logger_write) mod_logger_write((level), "OriginRewrite", __VA_ARGS__); \
    } while (0)

static kernel_mod_info_t g_mod_info = {
    .pkg_id = "li06.originrewrite",
    .version_code = 2026090301,
    .api_version = 1,
    .version = "0.2.1"
};

static void init_mod(kernel_mod_handle_t *handle) {
    bool config_ok;
    bool runtime_ok;
    (void)handle;

    or_config_default(&g_config);
    config_ok = or_config_validate(&g_config);
    or_state_store_init(&g_state);
    or_runtime_init(&g_runtime);
    OR_LOG(MOD_LOG_LEVEL_INFO, "startup_confirmation version=%s stage=configuring",
           g_mod_info.version);
    OR_STARTUP_EMIT("startup_confirmation version=%s stage=configuring", g_mod_info.version);
    runtime_ok = or_runtime_probe(&g_runtime);

    if (!config_ok) {
        OR_LOG(MOD_LOG_LEVEL_ERROR, "Configuration validation failed; elite overlay is disabled");
        g_config.enable_elites = false;
    }
    if (!runtime_ok) {
        OR_LOG(MOD_LOG_LEVEL_WARNING, "TEFKernel PatchLib probe failed; gameplay hooks remain disabled");
    } else if (!or_adapter_start(&g_runtime, &g_config, &g_state)) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "Verified mobile NPC hooks were not installable; gameplay overlay remains disabled");
    }
    OR_LOG(MOD_LOG_LEVEL_INFO, "Origin Rewrite(起源重构) core loaded; runtime gameplay gate=%s",
           g_runtime.capabilities.gameplay_enabled ? "on" : "off");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "startup_confirmation version=%s gameplay=%s setdefaults=%s ai=%s loot=%s",
           g_mod_info.version,
           g_runtime.capabilities.gameplay_enabled ? "on" : "off",
           g_runtime.capabilities.exact_spawn_commit_resolved ? "on" : "off",
           g_runtime.ai_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off",
           g_runtime.loot_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off");
    OR_STARTUP_EMIT("startup_confirmation version=%s gameplay=%s setdefaults=%s ai=%s loot=%s",
                    g_mod_info.version,
                    g_runtime.capabilities.gameplay_enabled ? "on" : "off",
                    g_runtime.capabilities.exact_spawn_commit_resolved ? "on" : "off",
                    g_runtime.ai_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off",
                    g_runtime.loot_hook_id != PATCH_HOOK_INVALID_ID ? "on" : "off");
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    or_adapter_stop();
    or_runtime_cleanup(&g_runtime);
    or_state_store_init(&g_state);
    OR_LOG(MOD_LOG_LEVEL_INFO, "Origin Rewrite(起源重构) core unloaded");
}

static kernel_mod_info_t *get_info(void) {
    return &g_mod_info;
}

static kernel_mod_ops_t g_ops = {
    .init_mod = init_mod,
    .cleanup_mod = cleanup_mod,
    .get_info = get_info
};

kernel_mod_ops_t *create_kernel_mod(void) {
    return &g_ops;
}
