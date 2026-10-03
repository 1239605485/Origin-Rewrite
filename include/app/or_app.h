#ifndef OR_APP_H
#define OR_APP_H
#include "ports/or_game.h"
#include "presentation/or_boss.h"
#include "storage/or_config.h"
typedef struct {
    OR_Lifecycle lifecycle;
    OR_BossObserver bosses;
    OR_NoticeQueue notices;
    OR_Config config;
    OR_WorldView world;
    OR_TextPort output;
    uint64_t sessions, last_health, last_reload;
    bool started, seen_player, last_dead;
    char private_dir[768];
} OR_App;
void or_app_init(OR_App *a, const char *dir, OR_Config config, OR_TextPort text);
void or_app_stop(OR_App *a);
void or_app_world(void *ctx, const OR_WorldView *v);
void or_app_npc(void *ctx, const OR_NpcView *v, unsigned signal);
#endif
