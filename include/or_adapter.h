#ifndef ORIGINREWRITE_ADAPTER_H
#define ORIGINREWRITE_ADAPTER_H

#include "or_config.h"
#include "or_runtime.h"
#include "or_state.h"

#ifdef __cplusplus
extern "C" {
#endif

bool or_adapter_start(OR_Runtime *runtime,
                      OR_Config *config,
                      OR_StateStore *state);
void or_adapter_stop(void);

void or_adapter_observe_death_state(patch_handle_t instance,
                                    int32_t npc_type,
                                    int32_t life,
                                    bool active,
                                    int32_t strike_result);
void or_adapter_observe_loot_boundary(patch_handle_t instance);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_ADAPTER_H */
