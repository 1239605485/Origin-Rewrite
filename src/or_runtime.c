#include "or_runtime.h"

#include "tefkernel/patchlib/type.h"
#include "tefkernel/tefstd/vector.h"

#include <string.h>

static void or_release_handle(patch_handle_t handle) {
    if (!handle) return;
#if defined(__ANDROID__)
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
    size_t i;
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->spawn_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->death_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->ai_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook_id = PATCH_HOOK_INVALID_ID;
    for (i = 0; i < OR_SETDEFAULTS_METHOD_LIMIT; ++i) {
        runtime->setdefaults_hook_ids[i] = PATCH_HOOK_INVALID_ID;
    }
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
        if (expected_arg_count != 0u && !expected_args) {
            matches = false;
        } else {
            for (i = 0; i < expected_arg_count; ++i) {
                patch_type_t *actual = (patch_type_t *)tefstd_vector_at(
                    &signature.arg_types, i);
                if (!actual || *actual != expected_args[i]) {
                    matches = false;
                    break;
                }
            }
        }
    }
    if (patchlib_method_signature_free) (void)patchlib_method_signature_free(&signature);
    return matches;
}

static patch_handle_t or_resolve_field(patch_handle_t type,
                                       const char *name,
                                       bool expected_instance,
                                       patch_type_t expected_type,
                                       size_t expected_size) {
    patch_handle_t field;
    if (!type || !patchlib_type_get_field) return PATCH_NULL;
    field = patchlib_type_get_field(type, name);
    if (!or_runtime_field_matches(field, expected_instance, expected_type, expected_size)) {
        or_release_handle(field);
        return PATCH_NULL;
    }
    return field;
}

static patch_handle_t or_resolve_field_any(patch_handle_t type,
                                           const char *const *names,
                                           size_t name_count,
                                           bool expected_instance,
                                           patch_type_t expected_type,
                                           size_t expected_size) {
    size_t i;
    for (i = 0; i < name_count; ++i) {
        patch_handle_t field = or_resolve_field(type, names[i], expected_instance,
                                                expected_type, expected_size);
        if (field) return field;
    }
    return PATCH_NULL;
}

static bool or_method_seen(const OR_Runtime *runtime, patch_handle_t method) {
    size_t i;
    if (!runtime || !method) return false;
    for (i = 0; i < runtime->method_setdefaults_count; ++i) {
        if (runtime->method_setdefaults[i] == method) return true;
    }
    return runtime->method_ai == method || runtime->method_npcloot == method;
}

static bool or_method_is_instance_void_zero(patch_handle_t method) {
    return method && or_runtime_signature_matches(method, true, PATCH_VOID, NULL, 0u);
}

static void or_resolve_setdefaults_methods(OR_Runtime *runtime) {
    int args_count;
    if (!runtime || !patchlib_type_get_method_by_param_count ||
        !patchlib_method_get_signature) return;
    for (args_count = 0;
         args_count <= 4 && runtime->method_setdefaults_count < OR_SETDEFAULTS_METHOD_LIMIT;
         ++args_count) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            runtime->npc_type, "SetDefaults", args_count);
        patch_method_signature_t signature;
        if (!or_handle_is_valid(method) || or_method_seen(runtime, method)) {
            or_release_handle(method);
            continue;
        }
        memset(&signature, 0, sizeof(signature));
        if (!patchlib_method_get_signature(method, &signature)) {
            or_release_handle(method);
            continue;
        }
        if (signature.is_instance && runtime->method_setdefaults_count < OR_SETDEFAULTS_METHOD_LIMIT) {
            runtime->method_setdefaults[runtime->method_setdefaults_count++] = method;
        } else {
            or_release_handle(method);
        }
        if (patchlib_method_signature_free) {
            (void)patchlib_method_signature_free(&signature);
        }
    }
}

static void or_resolve_ai_method(OR_Runtime *runtime) {
    patch_handle_t direct = PATCH_NULL;
    if (!runtime) return;
    if (patchlib_type_get_method_by_param_count) {
        direct = patchlib_type_get_method_by_param_count(runtime->npc_type, "AI", 0);
    }
    if (!direct && patchlib_type_get_method) {
        direct = patchlib_type_get_method(runtime->npc_type, "AI");
    }
    if (or_handle_is_valid(direct)) {
        if (or_method_is_instance_void_zero(direct)) {
            runtime->method_ai = direct;
            return;
        }
        /* Verified exception for Terraria 1.4.5.6.4: the direct parameterless
         * dispatcher works on mobile even when old metadata reports a hidden
         * MethodInfo argument. */
        if (patchlib_method_get_param_count &&
            patchlib_method_get_param_count(direct) == 0 &&
            patchlib_method_get_name && patchlib_method_get_name(direct) &&
            strcmp(patchlib_method_get_name(direct), "AI") == 0) {
            runtime->method_ai = direct;
            runtime->ai_known_dispatcher = true;
            return;
        }
        or_release_handle(direct);
    }

    if (patchlib_type_get_methods && tefstd_vector_init && tefstd_vector_size &&
        tefstd_vector_at && tefstd_vector_destroy) {
        tefstd_vector_t methods = {0};
        size_t i;
        if (!tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
            !patchlib_type_get_methods(runtime->npc_type, true, &methods)) {
            tefstd_vector_destroy(&methods);
            return;
        }
        for (i = 0; i < tefstd_vector_size(&methods); ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
            patch_handle_t method = entry ? *entry : PATCH_NULL;
            const char *name = patchlib_method_get_name
                ? patchlib_method_get_name(method) : NULL;
            if (name && strncmp(name, "AI", 2) == 0 &&
                or_method_is_instance_void_zero(method)) {
                runtime->method_ai = method;
                break;
            }
        }
        tefstd_vector_destroy(&methods);
    }
}

static void or_resolve_loot_method(OR_Runtime *runtime) {
    patch_handle_t method = PATCH_NULL;
    if (!runtime || !patchlib_type_get_method_by_param_count) return;
    method = patchlib_type_get_method_by_param_count(runtime->npc_type, "NPCLoot", 0);
    if (or_method_is_instance_void_zero(method)) {
        runtime->method_npcloot = method;
    } else {
        or_release_handle(method);
    }
}

bool or_runtime_probe(OR_Runtime *runtime) {
    static const char *const game_mode_names[] = {"GameMode", "gameMode"};
    static const char *const hard_mode_names[] = {"hardMode", "HardMode"};
    static const char *const update_count_names[] = {"GameUpdateCount", "gameUpdateCount"};
    patch_handle_t main_type;
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

    runtime->field_active = or_resolve_field(runtime->npc_type, "active", true, PATCH_BOOL, sizeof(bool));
    runtime->field_life_max = or_resolve_field(runtime->npc_type, "lifeMax", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_life = or_resolve_field(runtime->npc_type, "life", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_damage = or_resolve_field(runtime->npc_type, "damage", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_defense = or_resolve_field(runtime->npc_type, "defense", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_knockback_resist = or_resolve_field(runtime->npc_type, "knockBackResist", true, PATCH_FLOAT, sizeof(float));
    runtime->field_scale = or_resolve_field(runtime->npc_type, "scale", true, PATCH_FLOAT, sizeof(float));
    runtime->field_value = or_resolve_field(runtime->npc_type, "value", true, PATCH_FLOAT, sizeof(float));
    runtime->field_npc_slots = or_resolve_field(runtime->npc_type, "npcSlots", true, PATCH_FLOAT, sizeof(float));
    runtime->field_width = or_resolve_field(runtime->npc_type, "width", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_height = or_resolve_field(runtime->npc_type, "height", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_type = or_resolve_field(runtime->npc_type, "type", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_friendly = or_resolve_field(runtime->npc_type, "friendly", true, PATCH_BOOL, sizeof(bool));
    runtime->field_town_npc = or_resolve_field(runtime->npc_type, "townNPC", true, PATCH_BOOL, sizeof(bool));
    runtime->field_boss = or_resolve_field(runtime->npc_type, "boss", true, PATCH_BOOL, sizeof(bool));
    runtime->field_ai_style = or_resolve_field(runtime->npc_type, "aiStyle", true, PATCH_INT32, sizeof(int32_t));

    /* active and value are optional compatibility/reward fields. The core
     * elite stat overlay only needs the vanilla combat fields below. */
    fields_ok = runtime->field_type && runtime->field_life_max && runtime->field_life;
    runtime->capabilities.stats_fields_resolved = fields_ok;

    main_type = patchlib_type_get_type("Terraria", "Main");
    if (or_handle_is_valid(main_type)) {
        runtime->main_game_mode = or_resolve_field_any(main_type, game_mode_names,
                                                       sizeof(game_mode_names) / sizeof(game_mode_names[0]),
                                                       false, PATCH_INT32, sizeof(int32_t));
        runtime->main_zenith_world = or_resolve_field(main_type, "zenithWorld", false,
                                                      PATCH_BOOL, sizeof(bool));
        runtime->main_hard_mode = or_resolve_field_any(main_type, hard_mode_names,
                                                       sizeof(hard_mode_names) / sizeof(hard_mode_names[0]),
                                                       false, PATCH_BOOL, sizeof(bool));
        runtime->main_net_mode = or_resolve_field(main_type, "netMode", false,
                                                  PATCH_INT32, sizeof(int32_t));
        runtime->main_world_id = or_resolve_field(main_type, "worldID", false,
                                                  PATCH_INT32, sizeof(int32_t));
        runtime->main_update_count = or_resolve_field_any(main_type, update_count_names,
                                                          sizeof(update_count_names) / sizeof(update_count_names[0]),
                                                          false, PATCH_UINT64, sizeof(uint64_t));
        if (!runtime->main_update_count) {
            runtime->main_update_count = or_resolve_field_any(main_type, update_count_names,
                                                              sizeof(update_count_names) / sizeof(update_count_names[0]),
                                                              false, PATCH_INT64, sizeof(int64_t));
        }
        or_release_handle(main_type);
    }

    runtime->npc_downed_mech = or_resolve_field(runtime->npc_type, "downedMechBossAny",
                                                false, PATCH_BOOL, sizeof(bool));
    runtime->npc_downed_plant = or_resolve_field(runtime->npc_type, "downedPlantBoss",
                                                 false, PATCH_BOOL, sizeof(bool));
    runtime->npc_downed_golem = or_resolve_field(runtime->npc_type, "downedGolemBoss",
                                                 false, PATCH_BOOL, sizeof(bool));
    runtime->npc_downed_moonlord = or_resolve_field(runtime->npc_type, "downedMoonlord",
                                                    false, PATCH_BOOL, sizeof(bool));

    or_resolve_setdefaults_methods(runtime);
    or_resolve_ai_method(runtime);
    or_resolve_loot_method(runtime);
    runtime->capabilities.ai_dispatcher_known_target = runtime->ai_known_dispatcher;
    return true;
}

static void or_uninstall_hook(patch_hook_id_t *hook_id) {
    if (!hook_id) return;
    if (*hook_id != PATCH_HOOK_INVALID_ID && patchlib_uninstall_hook) {
        (void)patchlib_uninstall_hook(*hook_id);
    }
    *hook_id = PATCH_HOOK_INVALID_ID;
}

void or_runtime_cleanup(OR_Runtime *runtime) {
    size_t i;
    if (!runtime) return;
    for (i = 0; i < OR_SETDEFAULTS_METHOD_LIMIT; ++i) {
        or_uninstall_hook(&runtime->setdefaults_hook_ids[i]);
    }
    or_uninstall_hook(&runtime->spawn_hook_id);
    or_uninstall_hook(&runtime->death_hook_id);
    or_uninstall_hook(&runtime->ai_hook_id);
    or_uninstall_hook(&runtime->loot_hook_id);

    or_release_handle(runtime->field_active);
    or_release_handle(runtime->field_life_max);
    or_release_handle(runtime->field_life);
    or_release_handle(runtime->field_damage);
    or_release_handle(runtime->field_defense);
    or_release_handle(runtime->field_knockback_resist);
    or_release_handle(runtime->field_scale);
    or_release_handle(runtime->field_value);
    or_release_handle(runtime->field_npc_slots);
    or_release_handle(runtime->field_width);
    or_release_handle(runtime->field_height);
    or_release_handle(runtime->field_type);
    or_release_handle(runtime->field_friendly);
    or_release_handle(runtime->field_town_npc);
    or_release_handle(runtime->field_boss);
    or_release_handle(runtime->field_ai_style);
    or_release_handle(runtime->main_game_mode);
    or_release_handle(runtime->main_zenith_world);
    or_release_handle(runtime->main_hard_mode);
    or_release_handle(runtime->main_net_mode);
    or_release_handle(runtime->main_world_id);
    or_release_handle(runtime->main_update_count);
    or_release_handle(runtime->npc_downed_mech);
    or_release_handle(runtime->npc_downed_plant);
    or_release_handle(runtime->npc_downed_golem);
    or_release_handle(runtime->npc_downed_moonlord);
    for (i = 0; i < runtime->method_setdefaults_count; ++i) {
        or_release_handle(runtime->method_setdefaults[i]);
    }
    or_release_handle(runtime->method_ai);
    or_release_handle(runtime->method_npcloot);
    or_release_handle(runtime->npc_type);
    memset(runtime, 0, sizeof(*runtime));
    runtime->spawn_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->death_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->ai_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook_id = PATCH_HOOK_INVALID_ID;
    for (i = 0; i < OR_SETDEFAULTS_METHOD_LIMIT; ++i) {
        runtime->setdefaults_hook_ids[i] = PATCH_HOOK_INVALID_ID;
    }
}
