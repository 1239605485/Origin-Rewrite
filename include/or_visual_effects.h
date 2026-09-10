#ifndef OR_VISUAL_EFFECTS_H
#define OR_VISUAL_EFFECTS_H

#include "or_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Emits one small vanilla visual response after an elite commit.
 * This module never changes NPC stats, AI, loot, or body color. */
bool or_visual_effects_emit_spawn(const OR_Runtime *runtime,
                                  patch_handle_t instance,
                                  OR_EliteTier tier,
                                  uint32_t npc_type);

/* Emits a bounded, particle-only aura during the verified AI phase. */
bool or_visual_effects_emit_aura(const OR_Runtime *runtime,
                                 patch_handle_t instance,
                                 OR_EliteTier tier,
                                 uint32_t npc_type,
                                 uint32_t ai_tick);

#ifdef __cplusplus
}
#endif

#endif
