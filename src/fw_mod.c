#include "fw.h"

#include "mod_core.h"
#include "mod_logger.h"

/* mod_logger.h intentionally undefines its calling-convention helper after
 * publishing the declaration; keep the module-owned nullable hook definition
 * in the portable C spelling. */
void (*mod_logger_write)(
    mod_log_level_t level, const char *tag, const char *fmt, ...) = NULL;

static kernel_mod_info_t g_mod_info = {
    .pkg_id = "li06.originrewrite",
    .version_code = 2026090403,
    .api_version = 1,
    .version = "1.0.2-hook-debug"
};

static void fw_init_mod(kernel_mod_handle_t *handle) {
    bool ready;
    (void)handle;
    ready = fw_core_init();
    if (mod_logger_write) {
        mod_logger_write(MOD_LOG_LEVEL_INFO, "OriginRewrite",
                         "Origin Rewrite(起源重构) v1.0.2-hook-debug "
                         "initialized; gameplay gate=%s",
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
