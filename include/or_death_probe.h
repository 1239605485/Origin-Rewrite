#ifndef ORIGINREWRITE_DEATH_PROBE_H
#define ORIGINREWRITE_DEATH_PROBE_H

#include "or_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

void or_death_probe_postfix(patch_handle_t instance, void **args,
                            void *result,
                            const patch_method_signature_t *sig_info);

void or_death_probe_configure(patch_handle_t field_type,
                              patch_handle_t field_life,
                              patch_handle_t field_active);
void or_death_probe_loot_postfix(patch_handle_t instance, void **args,
                                 void *result,
                                 const patch_method_signature_t *sig_info);

#ifdef __cplusplus
}
#endif

#endif
