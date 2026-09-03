#include <stddef.h>

#include "mod_core.h"
#include "mod_logger.h"

#include "or_log.h"

#include "or_config.h"
#include "or_adapter.h"
#include "or_runtime.h"
#include "or_state.h"

__attribute__((visibility("default"))) void (*mod_logger_write)(
    mod_log_level_t level, const char *tag, const char *fmt, ...) = NULL;

static OR_Config g_config;
static OR_StateStore g_state;
static OR_Runtime g_runtime;

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

static kernel_mod_info_t g_mod_info = {
    .pkg_id = "li06.originrewrite",
    .version_code = 2026090429,
    .api_version = 1,
    .version = "0.2.8-rewrite-naming"
};

static void init_mod(kernel_mod_handle_t *handle) {
    bool config_ok;
    bool runtime_ok;
    or_log_init(handle);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[MODULE_BEACON] version=0.2.8-rewrite-naming versionCode=2026090429 stage=enter");

    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] config_begin");
    or_config_default(&g_config);
    config_ok = or_config_validate(&g_config);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] config_done ok=%s", config_ok ? "yes" : "no");
    or_state_store_init(&g_state);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] state_done");
    or_runtime_init(&g_runtime);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] runtime_probe_begin");
    runtime_ok = or_runtime_probe(&g_runtime);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] runtime_probe_done ok=%s", runtime_ok ? "yes" : "no");

    if (!config_ok) {
        OR_LOG(MOD_LOG_LEVEL_ERROR, "Configuration validation failed; reconstruction overlay is disabled");
        g_config.enable_elites = false;
    }
    if (!runtime_ok) {
        OR_LOG(MOD_LOG_LEVEL_WARNING, "TEFKernel PatchLib probe failed; gameplay hooks remain disabled");
    } else if (!or_adapter_start(&g_runtime, &g_config, &g_state)) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "Verified mobile NPC hooks were not installable; gameplay overlay remains disabled");
    }
    OR_LOG(MOD_LOG_LEVEL_INFO, "[HOOK_STATE] version=0.2.8-rewrite-naming gameplay=%s",
           g_runtime.capabilities.gameplay_enabled ? "on" : "off");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[MODULE_BEACON] version=0.2.8-rewrite-naming versionCode=2026090429 stage=ready");
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    or_adapter_stop();
    or_runtime_cleanup(&g_runtime);
    or_state_store_init(&g_state);
    OR_LOG(MOD_LOG_LEVEL_INFO, "Origin Rewrite core unloaded");
    or_log_shutdown();
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
