#include "or_boss_ai.h"

#include <math.h>
#include <string.h>

/* Single-body pilot set. Worms, multipart encounters, limbs, and segments are
 * deliberately excluded until each family has a verified root/child policy. */
bool or_boss_ai_type_supported(uint32_t npc_type) {
    switch (npc_type) {
        case 4u:   /* Eye of Cthulhu */
        case 50u:  /* King Slime */
        case 222u: /* Queen Bee */
        case 262u: /* Plantera */
        case 370u: /* Duke Fishron */
        case 636u: /* Empress of Light */
        case 657u: /* Queen Slime */
        case 668u: /* Deerclops */
            return true;
        default:
            return false;
    }
}

void or_boss_ai_reset(OR_BossAiState *state) {
    if (state) memset(state, 0, sizeof(*state));
}

static void set_phase(OR_BossAiState *state, OR_BossAiPhase phase) {
    state->phase = phase;
    state->phase_started_tick = state->tick;
}

bool or_boss_ai_step(OR_BossAiState *state,
                     const OR_BossAiInput *input,
                     OR_BossAiOutput *output) {
    float dx;
    float dy;
    float distance_sq;
    OR_BossAiPhase previous;

    if (!state || !input || !output ||
        !or_boss_ai_type_supported(input->npc_type) ||
        !isfinite(input->npc_x) || !isfinite(input->npc_y) ||
        !isfinite(input->player_x) || !isfinite(input->player_y) ||
        !isfinite(input->life_ratio)) return false;

    memset(output, 0, sizeof(*output));
    state->tick += 1u;
    previous = state->phase;
    dx = input->player_x - input->npc_x;
    dy = input->player_y - input->npc_y;
    distance_sq = dx * dx + dy * dy;
    if (!isfinite(distance_sq) || distance_sq > 1440000.0f ||
        input->life_ratio <= 0.0f) {
        if (state->phase != OR_BOSS_AI_IDLE) set_phase(state, OR_BOSS_AI_IDLE);
        output->phase = state->phase;
        output->phase_changed = previous != state->phase;
        return true;
    }

    switch (state->phase) {
        case OR_BOSS_AI_IDLE:
            if (distance_sq >= 14400.0f) {
                set_phase(state, OR_BOSS_AI_TELEGRAPH);
            }
            break;
        case OR_BOSS_AI_TELEGRAPH:
            if (state->tick - state->phase_started_tick >= 48u) {
                set_phase(state, OR_BOSS_AI_PRESSURE);
            }
            break;
        case OR_BOSS_AI_PRESSURE: {
            const float distance = sqrtf(distance_sq);
            const float reach = distance > 0.0f ? 1.0f / distance : 0.0f;
            float strength = input->life_ratio <= 0.25f ? 0.72f : 0.52f;
            /* Additive impulse, intentionally small. It layers over native
             * movement and never replaces velocity or writes NPC.ai[]. */
            if (((state->tick - state->phase_started_tick) % 3u) == 1u) {
                if (input->npc_type == 50u || input->npc_type == 262u ||
                    input->npc_type == 657u || input->npc_type == 668u) {
                    output->velocity_delta_x = (dx < 0.0f ? -strength : strength);
                    output->velocity_delta_y = dy < -100.0f ? -0.12f : 0.0f;
                } else {
                    output->velocity_delta_x = dx * reach * strength;
                    output->velocity_delta_y = dy * reach * strength * 0.35f;
                }
            }
            if (state->tick - state->phase_started_tick >= 10u) {
                set_phase(state, OR_BOSS_AI_RECOVERY);
            }
            break;
        }
        case OR_BOSS_AI_RECOVERY:
            if (state->tick - state->phase_started_tick >= 36u) {
                set_phase(state, OR_BOSS_AI_COOLDOWN);
            }
            break;
        case OR_BOSS_AI_COOLDOWN:
            if (state->tick - state->phase_started_tick >= 180u) {
                set_phase(state, OR_BOSS_AI_IDLE);
            }
            break;
        default:
            or_boss_ai_reset(state);
            break;
    }

    /* Hard bound on the external impulse; downstream code still validates the
     * final Vector2 before touching the managed object. */
    if (output->velocity_delta_x > 0.8f) output->velocity_delta_x = 0.8f;
    if (output->velocity_delta_x < -0.8f) output->velocity_delta_x = -0.8f;
    if (output->velocity_delta_y > 0.5f) output->velocity_delta_y = 0.5f;
    if (output->velocity_delta_y < -0.5f) output->velocity_delta_y = -0.5f;
    output->phase = state->phase;
    output->phase_changed = previous != state->phase;
    return true;
}
