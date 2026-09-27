#include "or_boss_ai.h"

#include <math.h>
#include <string.h>

typedef enum OR_BossMoveStyle {
    OR_BOSS_MOVE_VECTOR = 0,
    OR_BOSS_MOVE_HORIZONTAL
} OR_BossMoveStyle;

typedef struct OR_BossAiProfile {
    uint32_t telegraph_ticks;
    uint32_t pressure_ticks;
    uint32_t recovery_ticks;
    uint32_t cooldown_ticks;
    float strength;
    float rage_threshold;
    OR_BossMoveStyle move_style;
    bool early_game;
} OR_BossAiProfile;

/* Worm child segments and multipart limbs are deliberately absent. Eater of
 * Worlds is controlled through its head only; its body and tail keep vanilla
 * AI, so this hook never applies the same phase impulse to every segment. */
bool or_boss_ai_type_supported(uint32_t npc_type) {
    switch (npc_type) {
        case 4u:   /* Eye of Cthulhu */
        case 50u:  /* King Slime */
        case 13u:  /* Eater of Worlds head only */
        case 35u:  /* Skeletron head only */
        case 222u: /* Queen Bee */
        case 266u: /* Brain of Cthulhu */
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

static OR_BossAiProfile profile_for(uint32_t npc_type) {
    switch (npc_type) {
        case 4u:   /* Eye: warned vector dash; quicker second-half rhythm. */
            return (OR_BossAiProfile){42u, 18u, 30u, 108u, 0.36f, 0.45f,
                                      OR_BOSS_MOVE_VECTOR, true};
        case 50u:  /* King Slime: lateral pressure preserves native jump/teleport. */
            return (OR_BossAiProfile){45u, 12u, 34u, 120u, 0.25f, 0.45f,
                                      OR_BOSS_MOVE_HORIZONTAL, true};
        case 13u:  /* Eater head: keep pressure horizontal; never steer segments. */
            return (OR_BossAiProfile){54u, 12u, 36u, 132u, 0.27f, 0.45f,
                                      OR_BOSS_MOVE_HORIZONTAL, true};
        case 266u: /* Brain: short pursuit bursts leave creeper/illusion logic native. */
            return (OR_BossAiProfile){48u, 12u, 38u, 126u, 0.30f, 0.45f,
                                      OR_BOSS_MOVE_VECTOR, true};
        case 222u: /* Queen Bee: flight vector pressure, without extra stingers. */
            return (OR_BossAiProfile){42u, 15u, 34u, 120u, 0.30f, 0.45f,
                                      OR_BOSS_MOVE_VECTOR, true};
        case 35u:  /* Skeletron head: horizontal pressure; hands remain vanilla. */
            return (OR_BossAiProfile){54u, 12u, 42u, 144u, 0.25f, 0.40f,
                                      OR_BOSS_MOVE_HORIZONTAL, true};
        case 668u: /* Deerclops: deliberate, low-strength bursts. */
            return (OR_BossAiProfile){60u, 14u, 48u, 156u, 0.19f, 0.45f,
                                      OR_BOSS_MOVE_HORIZONTAL, true};
        case 262u: case 657u:
            return (OR_BossAiProfile){48u, 10u, 36u, 180u, 0.52f, 0.25f,
                                      OR_BOSS_MOVE_HORIZONTAL, false};
        case 370u: case 636u:
            return (OR_BossAiProfile){48u, 10u, 36u, 180u, 0.52f, 0.25f,
                                      OR_BOSS_MOVE_VECTOR, false};
        default:
            return (OR_BossAiProfile){0u, 0u, 0u, 0u, 0.0f, 0.0f,
                                      OR_BOSS_MOVE_VECTOR, false};
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
    OR_BossAiProfile profile;
    bool enraged;

    if (!state || !input || !output ||
        !or_boss_ai_type_supported(input->npc_type) ||
        !isfinite(input->npc_x) || !isfinite(input->npc_y) ||
        !isfinite(input->player_x) || !isfinite(input->player_y) ||
        !isfinite(input->life_ratio) ||
        !isfinite(input->npc_velocity_x) || !isfinite(input->npc_velocity_y)) return false;

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

    profile = profile_for(input->npc_type);
    enraged = input->life_ratio <= profile.rage_threshold;

    switch (state->phase) {
        case OR_BOSS_AI_IDLE:
            set_phase(state, OR_BOSS_AI_TELEGRAPH);
            break;
        case OR_BOSS_AI_TELEGRAPH:
            if (profile.early_game) {
                /* A slight braking beat makes the next attack readable on a
                 * touch screen without adding a new visual/effect pipeline. */
                output->velocity_delta_x = -input->npc_velocity_x * 0.08f;
                if (output->velocity_delta_x > 0.18f) output->velocity_delta_x = 0.18f;
                if (output->velocity_delta_x < -0.18f) output->velocity_delta_x = -0.18f;
                if (profile.move_style == OR_BOSS_MOVE_VECTOR) {
                    output->velocity_delta_y = -input->npc_velocity_y * 0.06f;
                    if (output->velocity_delta_y > 0.12f) output->velocity_delta_y = 0.12f;
                    if (output->velocity_delta_y < -0.12f) output->velocity_delta_y = -0.12f;
                }
            }
            if (state->tick - state->phase_started_tick >=
                (enraged && profile.early_game
                    ? profile.telegraph_ticks - 6u : profile.telegraph_ticks)) {
                set_phase(state, OR_BOSS_AI_PRESSURE);
            }
            break;
        case OR_BOSS_AI_PRESSURE: {
            const float distance = sqrtf(distance_sq);
            const float reach = distance > 0.0f ? 1.0f / distance : 0.0f;
            const uint32_t pulse_period = enraged && profile.early_game ? 2u : 3u;
            const float strength = profile.strength * (enraged ? 1.28f : 1.0f);
            /* Additive impulse, intentionally small. It layers over native
             * movement and never replaces velocity or writes NPC.ai[]. */
            if (((state->tick - state->phase_started_tick) % pulse_period) ==
                (pulse_period == 2u ? 0u : 1u)) {
                if (profile.move_style == OR_BOSS_MOVE_HORIZONTAL) {
                    output->velocity_delta_x = (dx < 0.0f ? -strength : strength);
                } else {
                    output->velocity_delta_x = dx * reach * strength;
                    output->velocity_delta_y = dy * reach * strength * 0.35f;
                }
            }
            if (state->tick - state->phase_started_tick >= profile.pressure_ticks) {
                set_phase(state, OR_BOSS_AI_RECOVERY);
            }
            break;
        }
        case OR_BOSS_AI_RECOVERY:
            if (state->tick - state->phase_started_tick >= profile.recovery_ticks) {
                set_phase(state, OR_BOSS_AI_COOLDOWN);
            }
            break;
        case OR_BOSS_AI_COOLDOWN:
            if (state->tick - state->phase_started_tick >=
                (enraged && profile.early_game
                    ? (profile.cooldown_ticks * 3u) / 4u
                    : profile.cooldown_ticks)) {
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
