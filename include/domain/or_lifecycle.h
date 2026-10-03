#ifndef OR_LIFECYCLE_H
#define OR_LIFECYCLE_H
#include <stdbool.h>
#include <stdint.h>
#define OR_SLOT_COUNT 256
typedef struct { uint64_t session, generation; uint16_t slot; } OR_Key;
typedef struct {
    uint16_t slot;
    int32_t type, life, life_max;
    bool active, boss;
    uint64_t tick;
} OR_NpcView;
typedef struct {
    bool occupied, decided, dead;
    int32_t type;
    OR_Key key;
    uint64_t last_seen, death_tick;
} OR_Entity;
typedef struct {
    uint64_t session, generations[OR_SLOT_COUNT];
    OR_Entity entities[OR_SLOT_COUNT];
    uint32_t live, births, releases;
} OR_Lifecycle;
bool or_key_equal(OR_Key a, OR_Key b);
void or_lifecycle_reset(OR_Lifecycle *s, uint64_t session);
OR_Entity *or_lifecycle_observe(OR_Lifecycle *s, const OR_NpcView *v, bool birth);
bool or_lifecycle_decide_once(OR_Lifecycle *s, OR_Key key);
void or_lifecycle_release(OR_Lifecycle *s, OR_Key key);
void or_lifecycle_sweep(OR_Lifecycle *s, uint64_t tick, uint64_t timeout);
#endif
