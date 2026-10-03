#include "app/or_app.h"
#include "or_log.h"
#include <string.h>
#include <stdio.h>
static void reset_world(OR_App *a, bool entering) {
    uint64_t session=entering ? ++a->sessions : 0;
    or_lifecycle_reset(&a->lifecycle,session);
    or_boss_reset(&a->bosses); or_notice_init(&a->notices,a->config.boss_dialog);
    a->last_dead=false; a->seen_player=false; a->last_health=0; a->last_reload=0;
}
void or_app_init(OR_App *a, const char *dir, OR_Config config, OR_TextPort text) {
    memset(a,0,sizeof(*a)); a->config=config; a->output=text; a->started=true;
    if (dir) snprintf(a->private_dir,sizeof(a->private_dir),"%s",dir);
    or_notice_init(&a->notices,config.boss_dialog);
}
void or_app_stop(OR_App *a) { a->started=false; reset_world(a,false); }
void or_app_world(void *ctx, const OR_WorldView *v) {
    OR_App *a=ctx; if (!a->started) return;
    bool was_active=a->world.valid && !a->world.menu && !a->world.multiplayer;
    bool is_active=v->valid && !v->menu && !v->multiplayer;
    bool entering=is_active && (!was_active || a->world.world_id!=v->world_id || v->tick<a->world.tick);
    if (entering || (!is_active && was_active)) {
        reset_world(a,entering);
        or_log("WORLD","state=%s world=%d session=%llu",entering?"enter":"leave",v->world_id,(unsigned long long)a->lifecycle.session);
        if (entering) (void)or_notice_push(&a->notices,(OR_Notice){
            {a->lifecycle.session,0,0},v->tick,v->tick+900,OR_NOTICE_SYSTEM,0,
            "[起源重构] 新架构验证版已加载：当前只观测生命周期与战斗对话，精英玩法尚未接入。"});
    }
    a->world=*v;
    if (!is_active) return;
    or_lifecycle_sweep(&a->lifecycle,v->tick,600);
    or_boss_sweep(&a->bosses,v->tick);
    if (v->player_known) {
        if (a->seen_player && !a->last_dead && v->player_dead)
            or_boss_player_death(&a->bosses,&a->notices,v->tick);
        a->last_dead=v->player_dead; a->seen_player=true;
    } else a->seen_player=false;
    if (v->tick>=a->last_reload+120) {
        OR_Config c; bool found;
        if (or_config_load(a->private_dir,&c,&found)) {
            if (c.boss_dialog!=a->config.boss_dialog) {
                or_notice_set_boss(&a->notices,c.boss_dialog);
                or_log("CONFIG","enableBossDialog=%d queued=%u",c.boss_dialog,a->notices.count);
            }
            a->config=c;
        } else or_log("CONFIG","invalid file; previous settings retained");
        a->last_reload=v->tick;
    }
    if (a->config.diagnostics && v->tick>=a->last_health+600) {
        or_log("HEALTH","tick=%llu tracked=%u births=%u releases=%u queued=%u",(unsigned long long)v->tick,
               a->lifecycle.live,a->lifecycle.births,a->lifecycle.releases,a->notices.count);
        a->last_health=v->tick;
    }
    OR_Notice n;
    if (or_notice_next(&a->notices,v->tick,&n)) {
        bool shown=a->output.text && a->output.text(a->output.ctx,n.text);
        or_log("NOTICE","channel=%u event=%u shown=%d text=%s",n.channel,n.event,shown,n.text);
    }
}
void or_app_npc(void *ctx, const OR_NpcView *v, unsigned signal) {
    OR_App *a=ctx;
    if (!a->started || !a->world.valid || a->world.menu || a->world.multiplayer) return;
    OR_Entity *e=or_lifecycle_observe(&a->lifecycle,v,signal==OR_SIGNAL_SPAWN);
    if (!e) return;
    if (signal==OR_SIGNAL_AI) {
        /* Decided means observed once; no elite roll or writes in R0/R1. */
        if (v->active && v->life>0) (void)or_lifecycle_decide_once(&a->lifecycle,e->key);
        or_boss_observe(&a->bosses,&a->notices,e->key,v);
    } else if (signal==OR_SIGNAL_LOOT && v->life<=0) {
        or_boss_loot(&a->bosses,&a->notices,e->key,v->tick);
        or_lifecycle_release(&a->lifecycle,e->key);
    }
}
