#include "tef_private.h"
#include "or_log.h"
#include <string.h>
/* PatchLib callbacks have no userdata: only this backend uses a singleton. */
OR_Tef or_tef={.hooks={PATCH_HOOK_INVALID_ID,PATCH_HOOK_INVALID_ID,PATCH_HOOK_INVALID_ID,PATCH_HOOK_INVALID_ID}};
static void world_after(patch_handle_t instance,void **args,void *result,const patch_method_signature_t *sig) {
    (void)instance;(void)args;(void)result;(void)sig;
    if (!or_tef.running) return;
    if (!or_tef_read_world(&or_tef)) { or_tef.world.valid=false; }
    if (or_tef.observer.world) or_tef.observer.world(or_tef.observer.ctx,&or_tef.world);
    or_tef_scan(&or_tef);
}
static void observe(patch_handle_t instance,unsigned signal) {
    if (!or_tef.running || !or_tef.world.valid || or_tef.world.menu || or_tef.world.multiplayer) return;
    OR_NpcView v;
    if (!or_tef_read_npc(&or_tef,instance,&v)) {
        if (or_tef.read_failures++<8) or_log("READ","NPC snapshot unavailable");
        return;
    }
    if (or_tef.observer.npc) or_tef.observer.npc(or_tef.observer.ctx,&v,signal);
}
#define CALLBACK(NAME,SIGNAL) static void NAME(patch_handle_t i,void **a,void *r,const patch_method_signature_t *s) { \
    (void)a;(void)r;(void)s; observe(i,SIGNAL); }
CALLBACK(ai_after,OR_SIGNAL_AI)
CALLBACK(spawn_after,OR_SIGNAL_SPAWN)
CALLBACK(loot_after,OR_SIGNAL_LOOT)
#undef CALLBACK
bool or_tef_stop(void) {
    or_tef.running=false; bool removed=true;
    for (int i=3;i>=0;--i) if (or_tef.hooks[i]!=PATCH_HOOK_INVALID_ID) {
        if (patchlib_uninstall_hook && patchlib_uninstall_hook(or_tef.hooks[i])) or_tef.hooks[i]=PATCH_HOOK_INVALID_ID;
        else { removed=false; or_log("HOOK","remove failed index=%d; do not live-unload library",i); }
    }
    return removed;
}
bool or_tef_start(OR_ObserverPort port) {
    if (or_tef.running || !or_tef_stop()) return false;
    memset(&or_tef,0,sizeof(or_tef)); or_tef.observer=port;
    for (int i=0;i<4;++i) or_tef.hooks[i]=PATCH_HOOK_INVALID_ID;
    if (!or_tef_probe(&or_tef)) return false;
    postfix_callback_t callbacks[]={world_after,ai_after,spawn_after,loot_after};
    for (int i=0;i<4;++i) {
        if (!or_tef.methods[i]) continue;
        or_tef.hooks[i]=patchlib_install_prepost_hook(or_tef.methods[i],NULL,callbacks[i]);
        or_log("HOOK","index=%d id=%d",i,or_tef.hooks[i]);
        if (or_tef.hooks[i]==PATCH_HOOK_INVALID_ID && i<3) { (void)or_tef_stop(); return false; }
    }
    or_tef.running=true; return true;
}
bool or_tef_text(void *unused,const char *text) {
    (void)unused;
    if (!or_tef.running || !or_tef.text_ready || !text) return false;
    patch_handle_t message=patchlib_string_create(text);
    if (!message) return false;
    uint8_t r=220,g=195,b=255; bool only=true;
    void *args[]={&message,&r,&g,&b,&only};
    bool ok=patchlib_method_invoke_args(or_tef.newtext,NULL,NULL,args);
    if (!ok) { or_tef.text_ready=false; or_log("CAPABILITY","text invoke failed; channel is log-only"); }
    return ok;
}
