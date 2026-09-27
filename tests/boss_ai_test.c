#include "or_boss_ai.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    OR_BossAiState state;
    OR_BossAiInput input = {4u, 0.0f, 0.0f, 400.0f, 50.0f, 0.80f};
    OR_BossAiOutput output;
    unsigned i;

    assert(or_boss_ai_type_supported(4u));
    assert(or_boss_ai_type_supported(668u));
    assert(!or_boss_ai_type_supported(13u)); /* Eater segment/root family withheld */
    or_boss_ai_reset(&state);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_TELEGRAPH);
    for (i = 0; i < 48u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_PRESSURE);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(fabsf(output.velocity_delta_x) <= 0.8f);
    assert(fabsf(output.velocity_delta_y) <= 0.5f);
    for (i = 0; i < 9u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_RECOVERY);
    for (i = 0; i < 36u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_COOLDOWN);
    for (i = 0; i < 180u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_IDLE);
    input.npc_x = NAN;
    assert(!or_boss_ai_step(&state, &input, &output));
    puts("boss_ai_test: ok");
    return 0;
}
