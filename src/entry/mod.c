#include "mod_core.h"
#include "app/or_app.h"
#include "ports/or_tef.h"
#include "or_log.h"
#include "or_version.h"
static OR_App application;
static bool initialized;
static void init(kernel_mod_handle_t *handle) {
    if (initialized) return;
    const char *dir=handle?handle->private_dir:NULL;
    or_log_open(dir); OR_Config config; bool found;
    bool valid=or_config_load(dir,&config,&found);
    or_log("BUILD","version=%s code=%d stage=R0/R1 gameplayWrites=off",OR_VERSION,OR_VERSION_CODE);
    or_log("CONFIG","found=%d valid=%d enableBossDialog=%d diagnostics=%d",found,valid,config.boss_dialog,config.diagnostics);
    or_app_init(&application,dir,config,(OR_TextPort){NULL,or_tef_text});
    bool ok=or_tef_start((OR_ObserverPort){&application,or_app_world,or_app_npc});
    if (!ok) or_app_stop(&application);
    initialized=true; or_log("READY","observation=%d dialogue=%d",ok,ok&&config.boss_dialog);
}
static void cleanup(kernel_mod_handle_t *handle) {
    (void)handle; if (!initialized) return;
    or_app_stop(&application); bool ok=or_tef_stop();
    or_log("STOP","hooksRemoved=%d",ok); initialized=false; or_log_close();
}
static kernel_mod_info_t info={OR_PACKAGE_ID,OR_VERSION_CODE,1,OR_VERSION};
static kernel_mod_info_t *get_info(void) { return &info; }
static kernel_mod_ops_t ops={init,cleanup,get_info};
__attribute__((visibility("default"))) kernel_mod_ops_t *create_kernel_mod(void) { return &ops; }
