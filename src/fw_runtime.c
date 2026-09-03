#include "fw_runtime.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/tefstd/vector.h"

#include <string.h>

static void fw_release_handle(patch_handle_t handle) {
    if (!handle) return;
#if defined(__ANDROID__)
    (void)handle;
#else
    if (patchlib_free) patchlib_free(handle);
#endif
}

static bool fw_handle_is_valid(patch_handle_t handle) {
    if (!handle) return false;
    if (patchlib_is_valid) return patchlib_is_valid(handle);
    return true;
}

static bool fw_field_matches(patch_handle_t field,
                             bool expected_instance,
                             patch_type_t expected_type,
                             size_t expected_size) {
    if (!fw_handle_is_valid(field) || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_is_instance) {
        return false;
    }
    if (patchlib_field_is_instance(field) != expected_instance) return false;
    if (patchlib_field_get_type(field) != expected_type) return false;
    return expected_size == 0u ||
           patchlib_field_get_size(field) == expected_size;
}

static patch_handle_t fw_resolve_field(patch_handle_t type,
                                       const char *name,
                                       bool expected_instance,
                                       patch_type_t expected_type,
                                       size_t expected_size) {
    patch_handle_t field;
    if (!type || !patchlib_type_get_field) return PATCH_NULL;
    field = patchlib_type_get_field(type, name);
    if (!fw_field_matches(field, expected_instance, expected_type,
                          expected_size)) {
        fw_release_handle(field);
        return PATCH_NULL;
    }
    return field;
}

static patch_handle_t fw_resolve_field_any(
    patch_handle_t type, const char *const *names, size_t name_count,
    bool expected_instance, patch_type_t expected_type, size_t expected_size) {
    size_t i;
    for (i = 0; i < name_count; ++i) {
        patch_handle_t field = fw_resolve_field(type, names[i],
                                                 expected_instance,
                                                 expected_type, expected_size);
        if (field) return field;
    }
    return PATCH_NULL;
}

bool fw_signature_matches(patch_handle_t method,
                          bool expected_instance,
                          patch_type_t expected_return,
                          const patch_type_t *expected_args,
                          size_t expected_arg_count) {
    patch_method_signature_t signature;
    size_t actual_count;
    size_t i;
    bool matches = false;

    if (!fw_handle_is_valid(method) || !patchlib_method_get_signature ||
        !tefstd_vector_size || !tefstd_vector_at) return false;
    memset(&signature, 0, sizeof(signature));
    if (!patchlib_method_get_signature(method, &signature)) return false;
    actual_count = tefstd_vector_size(&signature.arg_types);
    matches = signature.is_instance == expected_instance &&
              signature.return_type == expected_return &&
              actual_count == expected_arg_count;
    if (matches && expected_arg_count != 0u && !expected_args) {
        matches = false;
    } else if (matches) {
        for (i = 0; i < expected_arg_count; ++i) {
            patch_type_t *actual = (patch_type_t *)tefstd_vector_at(
                &signature.arg_types, i);
            if (!actual || *actual != expected_args[i]) {
                matches = false;
                break;
            }
        }
    }
    if (patchlib_method_signature_free) {
        (void)patchlib_method_signature_free(&signature);
    }
    return matches;
}

static bool fw_method_instance_void_zero(patch_handle_t method) {
    return fw_handle_is_valid(method) &&
           fw_signature_matches(method, true, PATCH_VOID, NULL, 0u);
}

static bool fw_method_seen(const fw_runtime *runtime,
                           patch_handle_t method) {
    size_t i;
    if (!runtime || !method) return false;
    for (i = 0; i < runtime->method_setdefaults_count; ++i) {
        if (runtime->method_setdefaults[i] == method) return true;
    }
    return runtime->method_ai == method ||
           runtime->method_npcloot == method;
}

static void fw_resolve_setdefaults(fw_runtime *runtime) {
    int args_count;
    if (!runtime || !patchlib_type_get_method_by_param_count ||
        !patchlib_method_get_signature) return;
    for (args_count = 0;
         args_count <= 4 &&
         runtime->method_setdefaults_count < FW_SETDEFAULTS_LIMIT;
         ++args_count) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            runtime->npc_type, "SetDefaults", args_count);
        patch_method_signature_t signature;
        if (!fw_handle_is_valid(method) ||
            fw_method_seen(runtime, method)) {
            fw_release_handle(method);
            continue;
        }
        memset(&signature, 0, sizeof(signature));
        if (!patchlib_method_get_signature(method, &signature)) {
            fw_release_handle(method);
            continue;
        }
        /* Param-count lookup is only a prefilter.  Accept only the real
         * instance void SetDefaults overload; otherwise a same-count method
         * can receive a callback with the wrong semantic entry point. */
        if (signature.is_instance && signature.return_type == PATCH_VOID &&
            patchlib_method_get_name &&
            patchlib_method_get_name(method) &&
            strcmp(patchlib_method_get_name(method), "SetDefaults") == 0 &&
            runtime->method_setdefaults_count < FW_SETDEFAULTS_LIMIT) {
            runtime->method_setdefaults[
                runtime->method_setdefaults_count++] = method;
        } else {
            fw_release_handle(method);
        }
        if (patchlib_method_signature_free) {
            (void)patchlib_method_signature_free(&signature);
        }
    }
}

static void fw_resolve_ai_method(fw_runtime *runtime) {
    patch_handle_t direct = PATCH_NULL;
    if (!runtime) return;
    if (patchlib_type_get_method_by_param_count) {
        direct = patchlib_type_get_method_by_param_count(
            runtime->npc_type, "AI", 0);
    }
    if (!direct && patchlib_type_get_method) {
        direct = patchlib_type_get_method(runtime->npc_type, "AI");
    }
    if (fw_handle_is_valid(direct)) {
        if (fw_method_instance_void_zero(direct)) {
            runtime->method_ai = direct;
            return;
        }
        /* 目标手机版 1.4.5.6.4 已验证：即使旧元数据把无参 AI 报成隐藏
         * MethodInfo 参数，仍可安全安装参数数量为 0 的调度入口。 */
        if (patchlib_method_get_param_count &&
            patchlib_method_get_param_count(direct) == 0 &&
            patchlib_method_get_name &&
            patchlib_method_get_name(direct) &&
            strcmp(patchlib_method_get_name(direct), "AI") == 0) {
            runtime->method_ai = direct;
            runtime->ai_known_dispatcher = true;
            return;
        }
        fw_release_handle(direct);
    }

    if (patchlib_type_get_methods && tefstd_vector_init &&
        tefstd_vector_size && tefstd_vector_at &&
        tefstd_vector_destroy) {
        tefstd_vector_t methods = {0};
        size_t i;
        if (!tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
            !patchlib_type_get_methods(runtime->npc_type, true, &methods)) {
            tefstd_vector_destroy(&methods);
            return;
        }
        for (i = 0; i < tefstd_vector_size(&methods); ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(
                &methods, i);
            patch_handle_t method = entry ? *entry : PATCH_NULL;
            const char *name = patchlib_method_get_name
                ? patchlib_method_get_name(method) : NULL;
            if (name && strncmp(name, "AI", 2) == 0 &&
                fw_method_instance_void_zero(method)) {
                runtime->method_ai = method;
                break;
            }
        }
        tefstd_vector_destroy(&methods);
    }
}

static void fw_resolve_loot_method(fw_runtime *runtime) {
    patch_handle_t method = PATCH_NULL;
    if (!runtime || !patchlib_type_get_method_by_param_count) return;
    method = patchlib_type_get_method_by_param_count(
        runtime->npc_type, "NPCLoot", 0);
    if (fw_method_instance_void_zero(method)) {
        runtime->method_npcloot = method;
    } else {
        fw_release_handle(method);
    }
}

bool fw_runtime_probe(fw_runtime *runtime) {
    static const char *const game_mode_names[] = {"GameMode", "gameMode"};
    static const char *const hard_mode_names[] = {"hardMode", "HardMode"};
    static const char *const update_count_names[] = {
        "GameUpdateCount", "gameUpdateCount"
    };
    patch_handle_t main_type;
    bool fields_ok;
    bool combat_ok;

    if (!runtime) return false;
    memset(runtime, 0, sizeof(*runtime));
    runtime->ai_hook = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook = PATCH_HOOK_INVALID_ID;
    {
        size_t i;
        for (i = 0; i < FW_SETDEFAULTS_LIMIT; ++i) {
            runtime->setdefaults_hooks[i] = PATCH_HOOK_INVALID_ID;
        }
    }

    runtime->patchlib_available = patchlib_type_get_type != NULL &&
                                  patchlib_type_get_field != NULL;
    if (!runtime->patchlib_available) return false;

    runtime->npc_type = patchlib_type_get_type("Terraria", "NPC");
    if (!fw_handle_is_valid(runtime->npc_type)) {
        fw_release_handle(runtime->npc_type);
        runtime->npc_type = PATCH_NULL;
        return false;
    }
    runtime->npc_type_resolved = true;

    runtime->field_active = fw_resolve_field(
        runtime->npc_type, "active", true, PATCH_BOOL, sizeof(bool));
    runtime->field_type = fw_resolve_field(
        runtime->npc_type, "type", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_life_max = fw_resolve_field(
        runtime->npc_type, "lifeMax", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_life = fw_resolve_field(
        runtime->npc_type, "life", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_damage = fw_resolve_field(
        runtime->npc_type, "damage", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_defense = fw_resolve_field(
        runtime->npc_type, "defense", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_knockback_resist = fw_resolve_field(
        runtime->npc_type, "knockBackResist", true, PATCH_FLOAT, sizeof(float));
    runtime->field_scale = fw_resolve_field(
        runtime->npc_type, "scale", true, PATCH_FLOAT, sizeof(float));
    runtime->field_width = fw_resolve_field(
        runtime->npc_type, "width", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_height = fw_resolve_field(
        runtime->npc_type, "height", true, PATCH_INT32, sizeof(int32_t));
    runtime->field_value = fw_resolve_field(
        runtime->npc_type, "value", true, PATCH_FLOAT, sizeof(float));
    runtime->field_npc_slots = fw_resolve_field(
        runtime->npc_type, "npcSlots", true, PATCH_FLOAT, sizeof(float));
    runtime->field_friendly = fw_resolve_field(
        runtime->npc_type, "friendly", true, PATCH_BOOL, sizeof(bool));
    runtime->field_town_npc = fw_resolve_field(
        runtime->npc_type, "townNPC", true, PATCH_BOOL, sizeof(bool));
    runtime->field_boss = fw_resolve_field(
        runtime->npc_type, "boss", true, PATCH_BOOL, sizeof(bool));

    combat_ok = runtime->field_type && runtime->field_life_max &&
                runtime->field_life && runtime->field_damage &&
                runtime->field_defense;
    runtime->stats_fields_resolved = combat_ok;

    main_type = patchlib_type_get_type("Terraria", "Main");
    if (fw_handle_is_valid(main_type)) {
        runtime->main_game_mode = fw_resolve_field_any(
            main_type, game_mode_names,
            sizeof(game_mode_names) / sizeof(game_mode_names[0]),
            false, PATCH_INT32, sizeof(int32_t));
        runtime->main_zenith_world = fw_resolve_field(
            main_type, "zenithWorld", false, PATCH_BOOL, sizeof(bool));
        runtime->main_hard_mode = fw_resolve_field_any(
            main_type, hard_mode_names,
            sizeof(hard_mode_names) / sizeof(hard_mode_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_net_mode = fw_resolve_field(
            main_type, "netMode", false, PATCH_INT32, sizeof(int32_t));
        runtime->main_world_id = fw_resolve_field(
            main_type, "worldID", false, PATCH_INT32, sizeof(int32_t));
        runtime->main_update_count = fw_resolve_field_any(
            main_type, update_count_names,
            sizeof(update_count_names) / sizeof(update_count_names[0]),
            false, PATCH_UINT64, sizeof(uint64_t));
        if (!runtime->main_update_count) {
            runtime->main_update_count = fw_resolve_field_any(
                main_type, update_count_names,
                sizeof(update_count_names) / sizeof(update_count_names[0]),
                false, PATCH_INT64, sizeof(int64_t));
        }
        runtime->main_fields_resolved =
            runtime->main_game_mode != PATCH_NULL &&
            runtime->main_hard_mode != PATCH_NULL;
        fw_release_handle(main_type);
    }

    runtime->npc_downed_mech = fw_resolve_field(
        runtime->npc_type, "downedMechBossAny", false,
        PATCH_BOOL, sizeof(bool));
    runtime->npc_downed_plant = fw_resolve_field(
        runtime->npc_type, "downedPlantBoss", false,
        PATCH_BOOL, sizeof(bool));
    runtime->npc_downed_golem = fw_resolve_field(
        runtime->npc_type, "downedGolemBoss", false,
        PATCH_BOOL, sizeof(bool));
    runtime->npc_downed_moonlord = fw_resolve_field(
        runtime->npc_type, "downedMoonlord", false,
        PATCH_BOOL, sizeof(bool));

    fw_resolve_setdefaults(runtime);
    fw_resolve_ai_method(runtime);
    fw_resolve_loot_method(runtime);
    fields_ok = runtime->stats_fields_resolved;
    return fields_ok;
}

static void fw_uninstall_hook(patch_hook_id_t *hook_id) {
    if (!hook_id) return;
    if (*hook_id != PATCH_HOOK_INVALID_ID && patchlib_uninstall_hook) {
        (void)patchlib_uninstall_hook(*hook_id);
    }
    *hook_id = PATCH_HOOK_INVALID_ID;
}

void fw_runtime_cleanup(fw_runtime *runtime) {
    size_t i;
    if (!runtime) return;
    for (i = 0; i < runtime->setdefaults_hook_count; ++i) {
        fw_uninstall_hook(&runtime->setdefaults_hooks[i]);
    }
    fw_uninstall_hook(&runtime->ai_hook);
    fw_uninstall_hook(&runtime->loot_hook);

#define FW_RELEASE(h) do { fw_release_handle(runtime->h); runtime->h = PATCH_NULL; } while (0)
    FW_RELEASE(npc_type);
    FW_RELEASE(field_active);
    FW_RELEASE(field_type);
    FW_RELEASE(field_life_max);
    FW_RELEASE(field_life);
    FW_RELEASE(field_damage);
    FW_RELEASE(field_defense);
    FW_RELEASE(field_knockback_resist);
    FW_RELEASE(field_scale);
    FW_RELEASE(field_width);
    FW_RELEASE(field_height);
    FW_RELEASE(field_value);
    FW_RELEASE(field_npc_slots);
    FW_RELEASE(field_friendly);
    FW_RELEASE(field_town_npc);
    FW_RELEASE(field_boss);
    FW_RELEASE(main_game_mode);
    FW_RELEASE(main_zenith_world);
    FW_RELEASE(main_hard_mode);
    FW_RELEASE(main_net_mode);
    FW_RELEASE(main_world_id);
    FW_RELEASE(main_update_count);
    FW_RELEASE(npc_downed_mech);
    FW_RELEASE(npc_downed_plant);
    FW_RELEASE(npc_downed_golem);
    FW_RELEASE(npc_downed_moonlord);
    for (i = 0; i < runtime->method_setdefaults_count; ++i) {
        fw_release_handle(runtime->method_setdefaults[i]);
    }
    runtime->method_setdefaults_count = 0u;
    fw_release_handle(runtime->method_ai);
    fw_release_handle(runtime->method_npcloot);
#undef FW_RELEASE
    memset(runtime, 0, sizeof(*runtime));
}
