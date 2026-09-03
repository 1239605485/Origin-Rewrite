#ifndef ORIGINREWRITE_AI_H
#define ORIGINREWRITE_AI_H

#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OR_AiArchetype or_ai_classify(bool is_melee,
                              bool is_ranged,
                              bool is_flying,
                              bool is_worm,
                              bool is_swarm,
                              bool is_special);
const char *or_ai_archetype_name(OR_AiArchetype archetype);
bool or_ai_build_plan(const OR_Config *config,
                      OR_ProgressStage progress,
                      OR_EliteTier tier,
                      OR_AiArchetype archetype,
                      uint64_t seed,
                      OR_AiPlan *out_plan);
void or_ai_runtime_init(OR_AiRuntimeState *state);
bool or_ai_begin_action(const OR_AiPlan *plan,
                        OR_AiRuntimeState *state,
                        uint32_t tick);
void or_ai_tick(const OR_AiPlan *plan,
                OR_AiRuntimeState *state,
                uint32_t tick);
bool or_ai_try_trigger_rage(const OR_AiPlan *plan,
                            OR_AiRuntimeState *state,
                            float previous_life_ratio,
                            float current_life_ratio);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_AI_H */
