#ifndef OR_NOTICE_H
#define OR_NOTICE_H
#include "domain/or_lifecycle.h"
#define OR_NOTICE_CAPACITY 24
enum { OR_NOTICE_SYSTEM, OR_NOTICE_BOSS };
typedef struct {
    OR_Key key;
    uint64_t created, expires;
    unsigned channel, event;
    const char *text;
} OR_Notice;
typedef struct {
    OR_Notice items[OR_NOTICE_CAPACITY]; unsigned count;
    uint64_t busy_until; bool boss_enabled;
} OR_NoticeQueue;
void or_notice_init(OR_NoticeQueue *q, bool boss_enabled);
void or_notice_set_boss(OR_NoticeQueue *q, bool enabled);
bool or_notice_push(OR_NoticeQueue *q, OR_Notice n);
bool or_notice_next(OR_NoticeQueue *q, uint64_t tick, OR_Notice *out);
#endif
