#include "or_boss_ai.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    OR_BossAiState state;
    OR_BossAiInput input = {262u, 0.0f, 0.0f, 400.0f, 50.0f, 0.80f,
                            0.0f, 0.0f};
    OR_BossAiOutput output;
    unsigned i;

    assert(or_boss_ai_type_supported(4u));
    assert(or_boss_ai_type_supported(50u));
    assert(or_boss_ai_type_supported(13u));
    assert(or_boss_ai_type_supported(35u));
    assert(or_boss_ai_type_supported(266u));
    assert(or_boss_ai_type_supported(222u));
    assert(or_boss_ai_type_supported(668u));
    assert(!or_boss_ai_type_supported(14u)); /* Eater body segment */
    assert(!or_boss_ai_type_supported(15u)); /* Eater tail segment */
    assert(!or_boss_ai_type_supported(36u)); /* Skeletron hand */
    assert(!or_boss_ai_type_supported(267u)); /* Brain creeper */
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

    /* Eye of Cthulhu runs its own shorter telegraph/dash/recovery loop. */
    input.npc_type = 4u;
    input.life_ratio = 0.70f;
    input.npc_velocity_x = 1.5f;
    input.npc_velocity_y = -2.0f;
    or_boss_ai_reset(&state);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_TELEGRAPH);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(output.velocity_delta_x < 0.0f);
    assert(output.velocity_delta_y > 0.0f);
    for (i = 0; i < 41u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_PRESSURE);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(output.velocity_delta_x != 0.0f);
    for (i = 0; i < 17u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_RECOVERY);
    for (i = 0; i < 30u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_COOLDOWN);
    for (i = 0; i < 108u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_IDLE);

    /* Low life shortens telegraph/cooldown and increases dash cadence. */
    input.life_ratio = 0.30f;
    assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_TELEGRAPH);
    for (i = 0; i < 36u; ++i) assert(or_boss_ai_step(&state, &input, &output));
    assert(state.phase == OR_BOSS_AI_PRESSURE);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(output.velocity_delta_x == 0.0f);
    assert(or_boss_ai_step(&state, &input, &output));
    assert(output.velocity_delta_x != 0.0f);

    /* Every early Boss profile has the documented phase boundaries. */
    {
        const struct {
            uint32_t type;
            uint32_t telegraph;
            uint32_t pressure;
            uint32_t recovery;
            uint32_t cooldown;
        } profiles[] = {
            {50u, 45u, 12u, 34u, 120u},
            {13u, 54u, 12u, 36u, 132u},
            {266u, 48u, 12u, 38u, 126u},
            {222u, 42u, 15u, 34u, 120u},
            {35u, 54u, 12u, 42u, 144u},
            {668u, 60u, 14u, 48u, 156u}
        };
        size_t boss_index;
        for (boss_index = 0u; boss_index < sizeof(profiles) / sizeof(profiles[0]);
             ++boss_index) {
            input.npc_type = profiles[boss_index].type;
            input.life_ratio = 0.8f;
            or_boss_ai_reset(&state);
            assert(or_boss_ai_step(&state, &input, &output));
            assert(state.phase == OR_BOSS_AI_TELEGRAPH);
            assert(or_boss_ai_type_supported(input.npc_type));
            for (i = 0u; i < profiles[boss_index].telegraph; ++i)
                assert(or_boss_ai_step(&state, &input, &output));
            assert(state.phase == OR_BOSS_AI_PRESSURE);
            for (i = 0u; i < profiles[boss_index].pressure; ++i)
                assert(or_boss_ai_step(&state, &input, &output));
            assert(state.phase == OR_BOSS_AI_RECOVERY);
            for (i = 0u; i < profiles[boss_index].recovery; ++i)
                assert(or_boss_ai_step(&state, &input, &output));
            assert(state.phase == OR_BOSS_AI_COOLDOWN);
            for (i = 0u; i < profiles[boss_index].cooldown; ++i)
                assert(or_boss_ai_step(&state, &input, &output));
            assert(state.phase == OR_BOSS_AI_IDLE);
        }
    }
    input.npc_x = NAN;
    assert(!or_boss_ai_step(&state, &input, &output));
    puts("boss_ai_test: ok");
    return 0;
}
