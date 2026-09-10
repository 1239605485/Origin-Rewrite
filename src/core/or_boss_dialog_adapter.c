#include "or_boss_dialog_adapter.h"
bool or_boss_dialog_adapter_observe(OR_BossDialogAdapter *state, float life_ratio, OR_BossDialogEvent *event) { if (!state || !event) return false; if (!state->seen) { state->seen=true; *event=OR_BOSS_DIALOG_SPAWN; return true; } if (!state->half_announced && life_ratio <= .5f) { state->half_announced=true; *event=OR_BOSS_DIALOG_HALF; return true; } return false; }
bool or_boss_dialog_adapter_observe_death(OR_BossDialogAdapter *state, OR_BossDialogEvent *event) { if (!state || !event || !state->seen) return false; *event = OR_BOSS_DIALOG_DEATH; return true; }
void or_boss_dialog_adapter_reset(OR_BossDialogAdapter *state) { if (state) { state->seen = false; state->half_announced = false; } }
