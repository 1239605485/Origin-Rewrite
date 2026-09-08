#include "or_ai.h"

#include "or_config.h"
#include "or_prng.h"

#include <string.h>

enum {
    OR_AI_PHASE_READY = 0,
    OR_AI_PHASE_TELEGRAPH,
    OR_AI_PHASE_ACTIVE,
    OR_AI_PHASE_RECOVERY,
    OR_AI_PHASE_COOLDOWN
};

OR_AiArchetype or_ai_classify(bool is_melee,
                              bool is_ranged,
                              bool is_flying,
                              bool is_worm,
                              bool is_swarm,
                              bool is_special) {
    if (is_special) return OR_AI_ARCHETYPE_SPECIAL;
    if (is_worm) return OR_AI_ARCHETYPE_WORM;
    if (is_flying) return OR_AI_ARCHETYPE_FLYING;
    if (is_swarm) return OR_AI_ARCHETYPE_SWARM;
    if (is_ranged) return OR_AI_ARCHETYPE_RANGED;
    if (is_melee) return OR_AI_ARCHETYPE_MELEE;
    return OR_AI_ARCHETYPE_MELEE;
}

const char *or_ai_archetype_name(OR_AiArchetype archetype) {
    switch (archetype) {
        case OR_AI_ARCHETYPE_RANGED: return "ranged";
        case OR_AI_ARCHETYPE_FLYING: return "flying";
        case OR_AI_ARCHETYPE_WORM: return "worm";
        case OR_AI_ARCHETYPE_SWARM: return "swarm";
        case OR_AI_ARCHETYPE_SPECIAL: return "special";
        case OR_AI_ARCHETYPE_MELEE:
        default: return "melee";
    }
}

OR_AiArchetype or_ai_classify_native_style(int32_t ai_style, bool *known) {
    if (known) *known = true;
    switch (ai_style) {
        case 2:  /* Demon Eye-compatible flying family. */
        case 5:  /* Flying AI. */
        case 14: /* Bat AI; treated as flying for compatibility budgeting. */
            return OR_AI_ARCHETYPE_FLYING;
        case 6:  /* Worm AI. */
            return OR_AI_ARCHETYPE_WORM;
        case 3:  /* Fighter AI. */
            return OR_AI_ARCHETYPE_MELEE;
        default:
            if (known) *known = false;
            return OR_AI_ARCHETYPE_MELEE;
    }
}

static OR_AiTemplate or_primary_for_archetype(OR_AiArchetype archetype) {
    switch (archetype) {
        case OR_AI_ARCHETYPE_RANGED: return OR_AI_TEMPLATE_PROJECTILE_BURST;
        case OR_AI_ARCHETYPE_FLYING: return OR_AI_TEMPLATE_DASH;
        case OR_AI_ARCHETYPE_WORM: return OR_AI_TEMPLATE_BURROW;
        case OR_AI_ARCHETYPE_SWARM: return OR_AI_TEMPLATE_DASH;
        case OR_AI_ARCHETYPE_SPECIAL: return OR_AI_TEMPLATE_PHASE;
        case OR_AI_ARCHETYPE_MELEE:
        default: return OR_AI_TEMPLATE_LUNGE;
    }
}

bool or_ai_build_plan(const OR_Config *config,
                      OR_ProgressStage progress,
                      OR_EliteTier tier,
                      OR_AiArchetype archetype,
                      uint64_t seed,
                      OR_AiPlan *out_plan) {
    OR_Prng rng;

    if (!config || !out_plan || tier <= OR_TIER_NONE || tier >= OR_TIER_COUNT ||
        !config->tiers[tier].enabled) return false;
    if (progress < OR_PROGRESS_PRE_HARDMODE || progress >= OR_PROGRESS_COUNT) return false;
    if (tier == OR_TIER_APOCALYPSE && progress == OR_PROGRESS_PRE_HARDMODE &&
        !config->allow_pre_hardmode_apocalypse) return false;

    memset(out_plan, 0, sizeof(*out_plan));
    out_plan->primary = or_primary_for_archetype(archetype);
    out_plan->intensity = config->tiers[tier].ai_intensity;
    out_plan->telegraph_ticks = tier == OR_TIER_ALTERED ? 24u : tier == OR_TIER_CALAMITY ? 18u : 14u;
    out_plan->active_ticks = tier == OR_TIER_ALTERED ? 12u : tier == OR_TIER_CALAMITY ? 16u : 20u;
    out_plan->recovery_ticks = tier == OR_TIER_ALTERED ? 16u : tier == OR_TIER_CALAMITY ? 12u : 10u;
    out_plan->cooldown_ticks = tier == OR_TIER_ALTERED ? 45u : tier == OR_TIER_CALAMITY ? 34u : 26u;

    or_prng_seed(&rng, seed ^ UINT64_C(0x41495f504c414e31));
    if (tier == OR_TIER_ALTERED) {
        /* A-tier receives one readable, low-frequency behavior layer. */
        out_plan->has_light_layer = true;
        out_plan->has_finisher = false;
    } else if (tier == OR_TIER_CALAMITY) {
        /* C-tier adds exactly one main behavior, never a summon chain. */
        out_plan->has_light_layer = false;
        out_plan->has_finisher = true;
        if (progress == OR_PROGRESS_PRE_HARDMODE) {
            /* Early C-tier has readable charge/shield behavior only; no extra shots. */
            out_plan->finisher = OR_AI_TEMPLATE_DASH;
        } else if (archetype == OR_AI_ARCHETYPE_RANGED) {
            out_plan->finisher = OR_AI_TEMPLATE_FAN_SHOT;
            out_plan->fan_shot_count = 3u;
        } else if ((or_prng_next_u64(&rng) & 1u) == 0u) {
            out_plan->finisher = OR_AI_TEMPLATE_DASH;
        } else {
            out_plan->finisher = OR_AI_TEMPLATE_PHASE;
        }
    } else {
        /* R-tier gets one mutually exclusive signature behavior. */
        out_plan->has_light_layer = false;
        out_plan->has_finisher = true;
        if (progress == OR_PROGRESS_PRE_HARDMODE || progress == OR_PROGRESS_HARDMODE_PRE_MECH) {
            out_plan->finisher = (or_prng_next_u64(&rng) & 1u) == 0u
                ? OR_AI_TEMPLATE_PHASE : OR_AI_TEMPLATE_RAGE;
        } else {
            switch ((unsigned)(or_prng_next_u64(&rng) % 3u)) {
                case 0:
                    out_plan->finisher = OR_AI_TEMPLATE_PHASE;
                    break;
                case 1:
                    out_plan->finisher = OR_AI_TEMPLATE_RAGE;
                    break;
                default:
                    out_plan->finisher = OR_AI_TEMPLATE_SUMMON;
                    out_plan->summon_count = (uint8_t)(1u + (or_prng_next_u64(&rng) % 2u));
                    if (out_plan->summon_count > OR_MAX_AI_SUMMONS) {
                        out_plan->summon_count = OR_MAX_AI_SUMMONS;
                    }
                    break;
            }
        }
        out_plan->rage_once = out_plan->finisher == OR_AI_TEMPLATE_RAGE;
        out_plan->rage_threshold = out_plan->rage_once ? 0.25f : 0.0f;
        if (out_plan->finisher == OR_AI_TEMPLATE_PHASE) {
            out_plan->fan_shot_count = archetype == OR_AI_ARCHETYPE_RANGED ? 5u : 0u;
        }
    }
    return true;
}

void or_ai_runtime_init(OR_AiRuntimeState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->phase = OR_AI_PHASE_READY;
}

bool or_ai_begin_action(const OR_AiPlan *plan,
                        OR_AiRuntimeState *state,
                        uint32_t tick) {
    if (!plan || !state || plan->primary == OR_AI_TEMPLATE_NONE) return false;
    if (state->phase != OR_AI_PHASE_READY || tick < state->cooldown_until_tick) return false;
    state->action_started_tick = tick;
    state->phase = OR_AI_PHASE_TELEGRAPH;
    return true;
}

void or_ai_tick(const OR_AiPlan *plan,
                OR_AiRuntimeState *state,
                uint32_t tick) {
    uint32_t elapsed;
    if (!plan || !state) return;
    switch (state->phase) {
        case OR_AI_PHASE_TELEGRAPH:
            elapsed = tick - state->action_started_tick;
            if (elapsed >= plan->telegraph_ticks) {
                state->action_started_tick = tick;
                state->phase = OR_AI_PHASE_ACTIVE;
            }
            break;
        case OR_AI_PHASE_ACTIVE:
            elapsed = tick - state->action_started_tick;
            if (elapsed >= plan->active_ticks) {
                state->action_started_tick = tick;
                state->phase = OR_AI_PHASE_RECOVERY;
            }
            break;
        case OR_AI_PHASE_RECOVERY:
            elapsed = tick - state->action_started_tick;
            if (elapsed >= plan->recovery_ticks) {
                state->cooldown_until_tick = tick + plan->cooldown_ticks;
                state->phase = OR_AI_PHASE_COOLDOWN;
            }
            break;
        case OR_AI_PHASE_COOLDOWN:
            if (tick >= state->cooldown_until_tick) state->phase = OR_AI_PHASE_READY;
            break;
        case OR_AI_PHASE_READY:
        default:
            break;
    }
}

bool or_ai_try_trigger_rage(const OR_AiPlan *plan,
                            OR_AiRuntimeState *state,
                            float previous_life_ratio,
                            float current_life_ratio) {
    float threshold;
    if (!plan || !state || !plan->rage_once || state->rage_triggered) return false;
    threshold = plan->rage_threshold > 0.0f ? plan->rage_threshold : 0.25f;
    if (previous_life_ratio > threshold && current_life_ratio <= threshold) {
        state->rage_triggered = true;
        return true;
    }
    return false;
}
