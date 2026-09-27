#ifndef ORIGINREWRITE_BOSS_AI_H
#define ORIGINREWRITE_BOSS_AI_H

#include <stdbool.h>
#include <stdint.h>

/* Pure policy state. The platform adapter owns all game objects and writes. */
typedef enum OR_BossAiPhase {
    OR_BOSS_AI_IDLE = 0,
    OR_BOSS_AI_TELEGRAPH,
    OR_BOSS_AI_PRESSURE,
    OR_BOSS_AI_RECOVERY,
    OR_BOSS_AI_COOLDOWN
} OR_BossAiPhase;

typedef struct OR_BossAiState {
    uint32_t tick;
    uint32_t phase_started_tick;
    OR_BossAiPhase phase;
} OR_BossAiState;

typedef struct OR_BossAiInput {
    uint32_t npc_type;
    float npc_x;
    float npc_y;
    float player_x;
    float player_y;
    float life_ratio;
} OR_BossAiInput;

typedef struct OR_BossAiOutput {
    float velocity_delta_x;
    float velocity_delta_y;
    OR_BossAiPhase phase;
    bool phase_changed;
} OR_BossAiOutput;

void or_boss_ai_reset(OR_BossAiState *state);
bool or_boss_ai_type_supported(uint32_t npc_type);
bool or_boss_ai_step(OR_BossAiState *state,
                     const OR_BossAiInput *input,
                     OR_BossAiOutput *output);

#endif
