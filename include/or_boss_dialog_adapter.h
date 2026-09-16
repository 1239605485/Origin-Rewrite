#ifndef ORIGINREWRITE_BOSS_DIALOG_ADAPTER_H
#define ORIGINREWRITE_BOSS_DIALOG_ADAPTER_H
#include <stdbool.h>
typedef enum OR_BossDialogEvent {
    OR_BOSS_DIALOG_SPAWN,
    OR_BOSS_DIALOG_HALF,
    OR_BOSS_DIALOG_DEATH,
    OR_BOSS_DIALOG_PLAYER_DEATH
} OR_BossDialogEvent;
typedef struct OR_BossDialogAdapter { bool seen; bool half_announced; } OR_BossDialogAdapter;
bool or_boss_dialog_adapter_observe(OR_BossDialogAdapter *state, float life_ratio, OR_BossDialogEvent *event);
bool or_boss_dialog_adapter_observe_death(OR_BossDialogAdapter *state, OR_BossDialogEvent *event);
/* Put a spawn/half event back when the broadcast channel is temporarily
 * locked by an earlier Boss line.  This prevents the one-shot threshold from
 * being consumed before the player can actually see the dialogue. */
void or_boss_dialog_adapter_requeue(OR_BossDialogAdapter *state,
                                    OR_BossDialogEvent event);
void or_boss_dialog_adapter_reset(OR_BossDialogAdapter *state);
#endif
