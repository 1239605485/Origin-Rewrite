#ifndef ORIGINREWRITE_RUNTIME_H
#define ORIGINREWRITE_RUNTIME_H

#include "or_types.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OR_RuntimeCapabilities {
    bool patchlib_available;
    bool npc_type_resolved;
    bool stats_fields_resolved;
    bool exact_spawn_commit_resolved;
    bool exact_death_hook_resolved;
    bool exact_loot_hook_resolved;
    bool gameplay_enabled;
} OR_RuntimeCapabilities;

typedef struct OR_Runtime {
    patch_handle_t npc_type;
    patch_handle_t field_active;
    patch_handle_t field_life_max;
    patch_handle_t field_life;
    patch_handle_t field_damage;
    patch_handle_t field_defense;
    patch_handle_t field_knockback_resist;
    patch_handle_t field_scale;
    patch_handle_t field_value;
    patch_hook_id_t spawn_hook_id;
    patch_hook_id_t death_hook_id;
    patch_hook_id_t loot_hook_id;
    OR_RuntimeCapabilities capabilities;
} OR_Runtime;

void or_runtime_init(OR_Runtime *runtime);
/* Caller must initialize a fresh runtime, or cleanup it before probing again. */
bool or_runtime_probe(OR_Runtime *runtime);
void or_runtime_cleanup(OR_Runtime *runtime);
bool or_runtime_signature_matches(patch_handle_t method,
                                  bool expected_instance,
                                  patch_type_t expected_return,
                                  const patch_type_t *expected_args,
                                  size_t expected_arg_count);
bool or_runtime_field_matches(patch_handle_t field,
                               bool expected_instance,
                               patch_type_t expected_type,
                               size_t expected_size);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_RUNTIME_H */
