#ifndef OR_GAME_H
#define OR_GAME_H
#include "domain/or_lifecycle.h"
typedef struct {
    bool valid, menu, multiplayer, player_known, player_dead;
    int32_t world_id;
    uint64_t tick;
} OR_WorldView;
enum { OR_SIGNAL_AI, OR_SIGNAL_SPAWN, OR_SIGNAL_LOOT, OR_SIGNAL_SCAN };
typedef struct {
    void *ctx;
    void (*world)(void *, const OR_WorldView *);
    void (*npc)(void *, const OR_NpcView *, unsigned signal);
} OR_ObserverPort;
typedef struct { void *ctx; bool (*text)(void *,const char *); } OR_TextPort;
#endif
