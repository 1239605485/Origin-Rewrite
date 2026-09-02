#include "or_runtime.h"

#include "tefkernel/patchlib/type.h"
#include "tefkernel/tefstd/vector.h"

#include <string.h>

static void or_release_handle(patch_handle_t handle) {
    if (!handle) return;
#if defined(__ANDROID__)
    /* Android handles are owned by the loader and patchlib_free is a no-op. */
    (void)handle;
#else
    if (patchlib_free) patchlib_free(handle);
#endif
}

static bool or_handle_is_valid(patch_handle_t handle) {
    if (!handle) return false;
    if (patchlib_is_valid) return patchlib_is_valid(handle);
    return true;
}

void or_runtime_init(OR_Runtime *runtime) {
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->spawn_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->death_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook_id = PATCH_HOOK_INVALID_ID;
}

bool or_runtime_field_matches(patch_handle_t field,
                               bool expected_instance,
                               patch_type_t expected_type,
                               size_t expected_size) {
    if (!or_handle_is_valid(field) || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_is_instance) return false;
    if (patchlib_field_is_instance(field) != expected_instance) return false;
    if (patchlib_field_get_type(field) != expected_type) return false;
    return expected_size == 0u || patchlib_field_get_size(field) == expected_size;
}

bool or_runtime_signature_matches(patch_handle_t method,
                                  bool expected_instance,
                                  patch_type_t expected_return,
                                  const patch_type_t *expected_args,
                                  size_t expected_arg_count) {
    patch_method_signature_t signature;
    size_t actual_count;
    size_t i;
    bool matches = false;

    if (!or_handle_is_valid(method) || !patchlib_method_get_signature ||
        !tefstd_vector_size || !tefstd_vector_at) return false;
    memset(&signature, 0, sizeof(signature));
    if (!patchlib_method_get_signature(method, &signature)) return false;
    actual_count = tefstd_vector_size(&signature.arg_types);
    matches = signature.is_instance == expected_instance &&
              signature.return_type == expected_return &&
              actual_count == expected_arg_count;
    if (matches) {
        for (i = 0; i < expected_arg_count; ++i) {
            patch_type_t *actual = (patch_type_t *)tefstd_vector_at(&signature.arg_types, i);
            if (!actual || *actual != expected_args[i]) {
                matches = false;
                break;
            }
        }
    }
    if (patchlib_method_signature_free) (void)patchlib_method_signature_free(&signature);
    return matches;
}

static patch_handle_t or_resolve_field(patch_handle_t type,
                                       const char *name,
                                       patch_type_t expected_type,
                                       size_t expected_size) {
    patch_handle_t field;
    if (!patchlib_type_get_field) return PATCH_NULL;
    field = patchlib_type_get_field(type, name);
    if (!or_runtime_field_matches(field, true, expected_type, expected_size)) {
        or_release_handle(field);
        return PATCH_NULL;
    }
    return field;
}

bool or_runtime_probe(OR_Runtime *runtime) {
    bool fields_ok;
    if (!runtime) return false;

    runtime->capabilities.patchlib_available = patchlib_type_get_type != NULL &&
                                               patchlib_type_get_field != NULL;
    if (!runtime->capabilities.patchlib_available) return false;
    runtime->npc_type = patchlib_type_get_type("Terraria", "NPC");
    if (!or_handle_is_valid(runtime->npc_type)) {
        or_release_handle(runtime->npc_type);
        runtime->npc_type = PATCH_NULL;
        return false;
    }
    runtime->capabilities.npc_type_resolved = true;

    runtime->field_active = or_resolve_field(runtime->npc_type, "active", PATCH_BOOL, sizeof(bool));
    runtime->field_life_max = or_resolve_field(runtime->npc_type, "lifeMax", PATCH_INT32, sizeof(int32_t));
    runtime->field_life = or_resolve_field(runtime->npc_type, "life", PATCH_INT32, sizeof(int32_t));
    runtime->field_damage = or_resolve_field(runtime->npc_type, "damage", PATCH_INT32, sizeof(int32_t));
    runtime->field_defense = or_resolve_field(runtime->npc_type, "defense", PATCH_INT32, sizeof(int32_t));
    runtime->field_knockback_resist = or_resolve_field(runtime->npc_type, "knockBackResist", PATCH_FLOAT, sizeof(float));
    runtime->field_scale = or_resolve_field(runtime->npc_type, "scale", PATCH_FLOAT, sizeof(float));
    runtime->field_value = or_resolve_field(runtime->npc_type, "value", PATCH_FLOAT, sizeof(float));

    fields_ok = runtime->field_active && runtime->field_life_max && runtime->field_life &&
                runtime->field_damage && runtime->field_defense && runtime->field_knockback_resist &&
                runtime->field_scale && runtime->field_value;
    runtime->capabilities.stats_fields_resolved = fields_ok;

    /* No method name is treated as a spawn/death contract until its exact signature is supplied. */
    runtime->capabilities.exact_spawn_commit_resolved = false;
    runtime->capabilities.exact_death_hook_resolved = false;
    runtime->capabilities.exact_loot_hook_resolved = false;
    runtime->capabilities.gameplay_enabled = false;
    return true;
}

void or_runtime_cleanup(OR_Runtime *runtime) {
    if (!runtime) return;
    if (runtime->spawn_hook_id != PATCH_HOOK_INVALID_ID && patchlib_uninstall_hook) {
        (void)patchlib_uninstall_hook(runtime->spawn_hook_id);
    }
    if (runtime->death_hook_id != PATCH_HOOK_INVALID_ID && patchlib_uninstall_hook) {
        (void)patchlib_uninstall_hook(runtime->death_hook_id);
    }
    if (runtime->loot_hook_id != PATCH_HOOK_INVALID_ID && patchlib_uninstall_hook) {
        (void)patchlib_uninstall_hook(runtime->loot_hook_id);
    }
    runtime->spawn_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->death_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook_id = PATCH_HOOK_INVALID_ID;
    or_release_handle(runtime->field_active);
    or_release_handle(runtime->field_life_max);
    or_release_handle(runtime->field_life);
    or_release_handle(runtime->field_damage);
    or_release_handle(runtime->field_defense);
    or_release_handle(runtime->field_knockback_resist);
    or_release_handle(runtime->field_scale);
    or_release_handle(runtime->field_value);
    or_release_handle(runtime->npc_type);
    runtime->field_active = PATCH_NULL;
    runtime->field_life_max = PATCH_NULL;
    runtime->field_life = PATCH_NULL;
    runtime->field_damage = PATCH_NULL;
    runtime->field_defense = PATCH_NULL;
    runtime->field_knockback_resist = PATCH_NULL;
    runtime->field_scale = PATCH_NULL;
    runtime->field_value = PATCH_NULL;
    runtime->npc_type = PATCH_NULL;
    memset(&runtime->capabilities, 0, sizeof(runtime->capabilities));
}
