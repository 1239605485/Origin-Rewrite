#include "presentation/or_boss.h"
#include <string.h>
#include <limits.h>
typedef struct { uint32_t id; const char *arrival[4],*half,*death[3],*defeat[3]; } OR_Voice;
#define VOICE(ID,A1,A2,A3,A4,H,D1,D2,D3,P1,P2,P3) {ID,{A1,A2,A3,A4},H,{D1,D2,D3},{P1,P2,P3}}
#include "boss_voices.inc"
#undef VOICE
static const OR_Voice *voice(int32_t type) {
    for (unsigned i=0;i<sizeof(voices)/sizeof(voices[0]);++i) if (voices[i].id==(uint32_t)type) return &voices[i];
    return NULL;
}
static void increment(uint16_t *v) { if (*v<UINT16_MAX) ++*v; }
static void emit(OR_NoticeQueue *q, OR_Key key, unsigned event, uint64_t tick, const char *text) {
    (void)or_notice_push(q,(OR_Notice){key,tick,tick+900,OR_NOTICE_BOSS,event,text});
}
void or_boss_reset(OR_BossObserver *s) { memset(s,0,sizeof(*s)); }
void or_boss_observe(OR_BossObserver *s, OR_NoticeQueue *q, OR_Key key, const OR_NpcView *v) {
    if (!v->boss || !v->active || v->life<=0 || v->life_max<=0 || key.slot>=OR_SLOT_COUNT || v->type>=1024) return;
    const OR_Voice *line=voice(v->type); if (!line) return;
    OR_BossState *b=&s->states[key.slot];
    if (!b->occupied || !or_key_equal(b->key,key)) {
        *b=(OR_BossState){true,false,false,key,v->type,v->tick};
        unsigned n=s->arrivals[v->type]; increment(&s->arrivals[v->type]);
        emit(q,key,0,v->tick,line->arrival[n<4?n:3]);
    }
    b->seen=v->tick;
    if (!b->half && (int64_t)v->life*2<=v->life_max) { b->half=true; emit(q,key,1,v->tick,line->half); }
}
void or_boss_loot(OR_BossObserver *s, OR_NoticeQueue *q, OR_Key key, uint64_t tick) {
    if (key.slot>=OR_SLOT_COUNT) return;
    OR_BossState *b=&s->states[key.slot];
    if (!b->occupied || b->ended || !or_key_equal(b->key,key)) return;
    const OR_Voice *line=voice(b->type); if (!line) return;
    /* Segmented and paired bosses need encounter-level proof in R6.
       Do not misreport one part's loot as the whole encounter's victory. */
    if (b->type==13 || b->type==125 || b->type==126 || b->type==134 || b->type==398) { b->ended=true; return; }
    unsigned n=s->kills[b->type]; increment(&s->kills[b->type]); b->ended=true;
    emit(q,key,2,tick,line->death[n<3?n:2]);
}
void or_boss_player_death(OR_BossObserver *s, OR_NoticeQueue *q, uint64_t tick) {
    OR_BossState *chosen=NULL;
    for (unsigned i=0;i<OR_SLOT_COUNT;++i) {
        OR_BossState *b=&s->states[i];
        if (b->occupied && !b->ended && tick>=b->seen && tick-b->seen<=120 && (!chosen || b->seen>chosen->seen)) chosen=b;
    }
    if (!chosen) return;
    const OR_Voice *line=voice(chosen->type); if (!line) return;
    unsigned n=s->player_losses[chosen->type]; increment(&s->player_losses[chosen->type]);
    emit(q,chosen->key,3u+n,tick,line->defeat[n<3?n:2]);
}
void or_boss_sweep(OR_BossObserver *s, uint64_t tick) {
    for (unsigned i=0;i<OR_SLOT_COUNT;++i) {
        OR_BossState *b=&s->states[i];
        if (b->occupied && tick>=b->seen && tick-b->seen>600) memset(b,0,sizeof(*b));
    }
}
