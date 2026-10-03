#include "presentation/or_notice.h"
#include <string.h>
void or_notice_init(OR_NoticeQueue *q, bool enabled) { memset(q,0,sizeof(*q)); q->boss_enabled=enabled; }
void or_notice_set_boss(OR_NoticeQueue *q, bool enabled) {
    q->boss_enabled=enabled;
    if (!enabled) {
        unsigned j=0;
        for (unsigned i=0;i<q->count;++i) if (q->items[i].channel!=OR_NOTICE_BOSS) q->items[j++]=q->items[i];
        q->count=j; q->busy_until=0;
    }
}
bool or_notice_push(OR_NoticeQueue *q, OR_Notice n) {
    if (!n.text || n.expires<=n.created || (n.channel==OR_NOTICE_BOSS && !q->boss_enabled)) return false;
    for (unsigned i=0;i<q->count;++i)
        if (q->items[i].channel==n.channel && q->items[i].event==n.event && or_key_equal(q->items[i].key,n.key)) return false;
    if (q->count==OR_NOTICE_CAPACITY) {
        if (n.channel!=OR_NOTICE_BOSS) return false;
        unsigned at=OR_NOTICE_CAPACITY;
        for (unsigned i=0;i<q->count;++i) if (q->items[i].channel!=OR_NOTICE_BOSS) { at=i; break; }
        if (at==OR_NOTICE_CAPACITY) return false;
        memmove(&q->items[at],&q->items[at+1],(q->count-at-1)*sizeof(n)); --q->count;
    }
    q->items[q->count++]=n; return true;
}
bool or_notice_next(OR_NoticeQueue *q, uint64_t tick, OR_Notice *out) {
    unsigned j=0;
    for (unsigned i=0;i<q->count;++i) if (q->items[i].expires>tick) q->items[j++]=q->items[i];
    q->count=j; if (!q->count || tick<q->busy_until) return false;
    unsigned at=0;
    for (unsigned i=0;i<q->count;++i) if (q->items[i].channel==OR_NOTICE_BOSS) { at=i; break; }
    *out=q->items[at]; memmove(&q->items[at],&q->items[at+1],(q->count-at-1)*sizeof(*out)); --q->count;
    q->busy_until=tick+(out->channel==OR_NOTICE_BOSS ? 300 : 90); return true;
}
