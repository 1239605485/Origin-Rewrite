#include "app/or_app.h"
#include "domain/or_effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
void or_log(const char *tag,const char *format,...) { (void)tag;(void)format; }
static unsigned shown;
static bool text(void *ctx,const char *s) { (void)ctx; assert(s); ++shown; return true; }
static void lifecycle(void) {
    OR_Lifecycle s; or_lifecycle_reset(&s,1);
    OR_NpcView v={3,1,100,100,true,false,1};
    OR_Entity *e=or_lifecycle_observe(&s,&v,false); assert(e);
    OR_Key old=e->key;
    assert(or_lifecycle_decide_once(&s,old));
    or_lifecycle_sweep(&s,100000,600); assert(s.live==1 && or_key_equal(s.entities[3].key,old));
    for (unsigned i=0;i<10000;++i) {
        assert(or_key_equal(or_lifecycle_observe(&s,&v,false)->key,old));
        assert(!or_lifecycle_decide_once(&s,old));
    }
    e=or_lifecycle_observe(&s,&v,true); assert(e && e->key.generation!=old.generation);
    assert(!or_lifecycle_decide_once(&s,old));
    or_lifecycle_release(&s,old); assert(s.live==1);
    v.life=0; v.tick=2; e=or_lifecycle_observe(&s,&v,false); assert(e->dead);
    or_lifecycle_sweep(&s,3,600); assert(s.live==1);
    or_lifecycle_sweep(&s,123,600); assert(s.live==0);
    for (unsigned i=0;i<50000;++i) {
        v.tick=i+200; v.life=100; e=or_lifecycle_observe(&s,&v,true);
        assert(e); or_lifecycle_release(&s,e->key); assert(s.live==0);
    }
    or_lifecycle_reset(&s,2); e=or_lifecycle_observe(&s,&v,false);
    assert(e && e->key.session!=old.session);
    v.slot=OR_SLOT_COUNT; assert(!or_lifecycle_observe(&s,&v,false));
}
static void effects(void) {
    OR_Contribution a[]={
        {.source=9,.allowed=OR_DAMAGE,.changed=OR_DAMAGE,.damage=1.08},
        {.source=1,.allowed=OR_LIFE,.changed=OR_LIFE,.life=1.2}};
    OR_Effects out,other; assert(or_effects_merge(a,2,&out));
    assert(out.life==1.2 && out.damage==1.08 && out.reward==1 && out.ai==1);
    OR_Contribution reverse[]={a[1],a[0]}; assert(or_effects_merge(reverse,2,&other));
    assert(!memcmp(&out,&other,sizeof(out)));
    a[0].changed|=OR_REWARD; assert(!or_effects_merge(a,2,&out));
    a[0].changed=OR_DAMAGE; a[0].damage=NAN; assert(!or_effects_merge(a,2,&out));
    assert(or_effects_merge(NULL,0,&out) && out.life==1);
}
static void config(void) {
    OR_Config c={true,true};
    assert(or_config_parse("{\"schemaVersion\":1,\"values\":{\"enableBossDialog\":false,\"enableDiagnostics\":true}}",&c));
    assert(!c.boss_dialog && c.diagnostics);
    const char *bad[]={"", "{", "{}", "{\"values\":true}",
        "{\"values\":{\"enableBossDialog\":\"false\"}}", "{\"values\":{\"enableBossDialog\":false,}}",
        "{\"values\":{\"enableBossDialog\":true,\"enableBossDialog\":false}}",
        "{\"schemaVersion\":10,\"values\":{}}", "{\"values\":{}}garbage"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        c=(OR_Config){false,false}; assert(!or_config_parse(bad[i],&c)); assert(!c.boss_dialog && !c.diagnostics);
    }
    assert(or_config_parse("{\"metadata\":{\"enableBossDialog\":false},\"values\":{}}",&c) && c.boss_dialog);
}
static void notice(void) {
    OR_NoticeQueue q; or_notice_init(&q,true); OR_Key key={1,1,1};
    OR_Notice normal={key,1,1000,OR_NOTICE_SYSTEM,1,"normal"};
    OR_Notice boss={key,1,1000,OR_NOTICE_BOSS,1,"boss"};
    assert(or_notice_push(&q,normal)); assert(or_notice_push(&q,boss)); assert(!or_notice_push(&q,boss));
    OR_Notice out; assert(or_notice_next(&q,1,&out) && out.channel==OR_NOTICE_BOSS);
    assert(!or_notice_next(&q,2,&out)); or_notice_set_boss(&q,false);
    assert(or_notice_next(&q,2,&out) && out.channel==OR_NOTICE_SYSTEM);
    assert(!or_notice_push(&q,boss)); or_notice_set_boss(&q,true); assert(q.count==0);
    assert(or_notice_push(&q,boss)); assert(!or_notice_next(&q,1001,&out)); assert(q.count==0);
}
static void boss(void) {
    OR_BossObserver b; OR_NoticeQueue q; or_boss_reset(&b); or_notice_init(&q,true);
    OR_Key key={1,1,7}; OR_NpcView v={7,50,100,100,true,true,10};
    or_boss_observe(&b,&q,key,&v); assert(q.count==1);
    for (int i=0;i<100;++i) or_boss_observe(&b,&q,key,&v);
    assert(q.count==1 && b.arrivals[50]==1);
    v.life=50; or_boss_observe(&b,&q,key,&v); assert(q.count==2);
    or_boss_loot(&b,&q,key,11); or_boss_loot(&b,&q,key,11); assert(q.count==3 && b.kills[50]==1);
    or_notice_set_boss(&q,false); assert(q.count==0);
    key.generation=2; v.life=100; or_boss_observe(&b,&q,key,&v); assert(q.count==0);
    or_notice_set_boss(&q,true); or_boss_observe(&b,&q,key,&v); assert(q.count==0);
    v.life=50; or_boss_observe(&b,&q,key,&v); assert(q.count==1);
}
static void app(void) {
    OR_App a; shown=0; or_app_init(&a,NULL,or_config_default(),(OR_TextPort){NULL,text});
    OR_WorldView w={true,false,false,true,false,8,1}; or_app_world(&a,&w);
    uint64_t session=a.lifecycle.session; assert(session && shown==1);
    OR_NpcView v={2,50,100,100,true,true,1}; or_app_npc(&a,&v,OR_SIGNAL_AI); assert(a.lifecycle.live==1);
    w.menu=true; or_app_world(&a,&w); assert(a.lifecycle.live==0 && a.notices.count==0);
    w.menu=false; w.world_id=9; w.tick=2; or_app_world(&a,&w); assert(a.lifecycle.session>session);
    w.multiplayer=true; or_app_world(&a,&w); or_app_npc(&a,&v,OR_SIGNAL_AI); assert(a.lifecycle.live==0);
    or_app_stop(&a); or_app_stop(&a); assert(!a.started);
}
int main(void) {
    lifecycle(); effects(); config(); notice(); boss(); app();
    puts("PASS: lifecycle/reuse/decision, contribution isolation, config, dialogue queue and world reset");
    return 0;
}
