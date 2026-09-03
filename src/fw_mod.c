#include "fw.h"

#include "mod_core.h"
#include "mod_logger.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

/* The loader fills this exported function-pointer variable when the module is
 * registered.  Keep the symbol visible: without it, the pointer remains NULL
 * and none of the module diagnostics reach the TEFKernel log stream. */
__attribute__((visibility("default"))) void (*mod_logger_write)(
    mod_log_level_t level, const char *tag, const char *fmt, ...) = NULL;

static kernel_mod_info_t g_mod_info = {
    .pkg_id = "li06.originrewrite",
    .version_code = 2026090406,
    .api_version = 1,
    .version = "1.0.5-kernel-log-beacon"
};

static void fw_init_mod(kernel_mod_handle_t *handle) {
    bool ready;
    (void)handle;
#if defined(__ANDROID__)
    /* This beacon is independent of the optional TEF module logger.  It lets
     * logcat prove which binary was loaded before any runtime probe runs. */
    __android_log_print(ANDROID_LOG_INFO, "OriginRewrite",
                        "[MODULE_BEACON] version=1.0.5-kernel-log-beacon "
                        "versionCode=2026090406");
#endif
    ready = fw_core_init();
    if (mod_logger_write) {
        mod_logger_write(MOD_LOG_LEVEL_INFO, "OriginRewrite",
                         "[MODULE_BEACON] version=1.0.5-kernel-log-beacon "
                         "versionCode=2026090406 initialized; "
                         "gameplay gate=%s",
                         ready ? "on" : "off");
    }
}

static void fw_cleanup_mod(kernel_mod_handle_t *handle) {
    (void)handle;
    fw_core_shutdown();
}

static kernel_mod_info_t *fw_get_info(void) {
    return &g_mod_info;
}

static kernel_mod_ops_t g_mod_ops = {
    .init_mod = fw_init_mod,
    .cleanup_mod = fw_cleanup_mod,
    .get_info = fw_get_info
};

kernel_mod_ops_t *create_kernel_mod(void) {
    return &g_mod_ops;
}
