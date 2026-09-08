#include <stddef.h>

#include "mod_core.h"
#include "mod_logger.h"

#include "or_log.h"

#include "or_config.h"
#include "or_config_io.h"
#include "or_adapter.h"
#include "or_bnm_bridge.h"
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
    .version_code = 2026090828,
    .api_version = 1,
    .version = "0.9.7-tefmanager-feature-enums"
};

static void init_mod(kernel_mod_handle_t *handle) {
    bool config_ok;
    bool runtime_ok;
    bool unity_version_ok;
    bool bnm_ok = false;
    char unity_version[64] = {0};
    OR_ConfigIoReport config_report = {0};
    or_log_init(handle);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[MODULE_BEACON] version=0.9.7-tefmanager-feature-enums versionCode=2026090828 stage=enter");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[ARCHITECTURE] core=pure-c metadata=BNM-2.5.2 hooks=TEFKernel-PatchLib "
           "lifecycle=SetDefaults-observe/AI-active-commit failPolicy=SAFE-OFF");

    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] config_begin");
    or_config_default(&g_config);
    (void)or_config_io_apply_private(
        handle ? handle->private_dir : NULL, &g_config, &config_report);
    config_ok = or_config_validate(&g_config);
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[INIT_STAGE] config_done ok=%s file=%s parsed=%s overrides=%u invalid=%u "
           "classicChance=%.3f maxActive=%u",
           config_ok ? "yes" : "no",
           config_report.file_found ? "yes" : "no",
           config_report.parsed ? "yes" : "no",
           (unsigned)config_report.overrides_applied,
           (unsigned)config_report.invalid_values,
           (double)g_config.modes[OR_MODE_CLASSIC].elite_chance,
           (unsigned)g_config.max_active_elites);
    or_state_store_init(&g_state);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] state_done");
    or_runtime_init(&g_runtime);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] runtime_probe_begin");
    runtime_ok = or_runtime_probe(&g_runtime);
    OR_LOG(MOD_LOG_LEVEL_INFO, "[INIT_STAGE] runtime_probe_done ok=%s", runtime_ok ? "yes" : "no");

    unity_version_ok = runtime_ok &&
        or_runtime_query_unity_version(unity_version, sizeof(unity_version));
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[UNITY_PROBE] version=%s read=%s",
           unity_version_ok ? unity_version : "unavailable",
           unity_version_ok ? "yes" : "no");
    if (runtime_ok) {
        bnm_ok = or_bnm_bridge_init(unity_version_ok ? unity_version : NULL);
    }
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[BNM_STATE] enabled=%s status=%s fallback=PatchLib",
           bnm_ok ? "yes" : "no", or_bnm_bridge_status());

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
    OR_LOG(MOD_LOG_LEVEL_INFO, "[HOOK_STATE] version=0.9.7-tefmanager-feature-enums gameplay=%s metadata=%s",
           g_runtime.capabilities.gameplay_enabled ? "on" : "off",
           bnm_ok ? "BNM" : "PatchLib-fallback");
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[MODULE_BEACON] version=0.9.7-tefmanager-feature-enums versionCode=2026090828 stage=ready");
}

static void cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    or_adapter_stop();
    or_bnm_bridge_shutdown();
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
