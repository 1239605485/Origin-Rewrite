#include "domain/or_lifecycle.h"
#include <string.h>
bool or_key_equal(OR_Key a, OR_Key b) {
    return a.session == b.session && a.slot == b.slot && a.generation == b.generation;
}
void or_lifecycle_reset(OR_Lifecycle *s, uint64_t session) {
    memset(s, 0, sizeof(*s)); s->session = session;
}
void or_lifecycle_release(OR_Lifecycle *s, OR_Key key) {
    if (key.slot >= OR_SLOT_COUNT) return;
    OR_Entity *e = &s->entities[key.slot];
    if (!e->occupied || !or_key_equal(e->key, key)) return;
    memset(e, 0, sizeof(*e)); --s->live; ++s->releases;
}
OR_Entity *or_lifecycle_observe(OR_Lifecycle *s, const OR_NpcView *v, bool birth) {
    if (!s->session || v->slot >= OR_SLOT_COUNT || v->type <= 0) return NULL;
    OR_Entity *e = &s->entities[v->slot];
    if (birth || (e->occupied && (e->type != v->type || (e->dead && v->active && v->life > 0)))) {
        or_lifecycle_release(s, e->key);
    }
    if (!v->active && !e->occupied) return NULL;
    if (!e->occupied) {
        e->occupied = true; e->type = v->type;
        e->key = (OR_Key){s->session, ++s->generations[v->slot], v->slot};
        ++s->births; ++s->live;
    }
    e->last_seen = v->tick;
    if ((!v->active || v->life <= 0) && !e->dead) { e->dead = true; e->death_tick=v->tick; }
    return e;
}
bool or_lifecycle_decide_once(OR_Lifecycle *s, OR_Key key) {
    if (key.slot >= OR_SLOT_COUNT) return false;
    OR_Entity *e = &s->entities[key.slot];
    if (!e->occupied || !or_key_equal(e->key, key) || e->decided || e->dead) return false;
    e->decided = true; return true;
}
void or_lifecycle_sweep(OR_Lifecycle *s, uint64_t tick, uint64_t timeout) {
    (void)timeout;
    for (unsigned i = 0; i < OR_SLOT_COUNT; ++i) {
        OR_Entity *e = &s->entities[i];
        /* A missed AI callback is not proof of despawn: fixed slot storage
           remains bounded, and real spawn/scan signals invalidate identity. */
        if (e->occupied && e->dead && tick>=e->death_tick && tick-e->death_tick>120)
            or_lifecycle_release(s, e->key);
    }
}
