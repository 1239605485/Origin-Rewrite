#include "or_runtime.h"

#include "or_log.h"

#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/struct/array.h"
#include "tefkernel/patchlib/struct/string.h"
#include "tefkernel/tefstd/vector.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__ANDROID__)
extern void *(*patchlib_field_get_pointer)(patch_handle_t field,
                                           void *instance);
#endif

#define OR_WORLD_MEMBER_LIMIT 256u
#define OR_BIOME_MEMBER_LIMIT 96u

#define OR_RUNTIME_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

static void or_release_handle(patch_handle_t handle);
static bool or_handle_is_valid(patch_handle_t handle);
static patch_handle_t or_resolve_field(patch_handle_t type,
                                       const char *name,
                                       bool expected_instance,
                                       patch_type_t expected_type,
                                       size_t expected_size);

static void or_probe_item_new_item(OR_Runtime *runtime) {
    tefstd_vector_t methods = {0};
    size_t i;
    uint32_t overloads = 0u;
    uint32_t exact_matches = 0u;
    if (!runtime || !patchlib_type_get_type || !patchlib_type_get_methods ||
        !patchlib_method_get_name || !tefstd_vector_init ||
        !tefstd_vector_size || !tefstd_vector_at || !tefstd_vector_destroy) return;
    runtime->item_type = patchlib_type_get_type("Terraria", "Item");
    if (!or_handle_is_valid(runtime->item_type)) {
        or_release_handle(runtime->item_type);
        runtime->item_type = PATCH_NULL;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_PROBE] type=unavailable method=NewItem unavailable");
        return;
    }
    runtime->item_field_type = patchlib_type_get_field(runtime->item_type, "type");
    runtime->item_field_stack = patchlib_type_get_field(runtime->item_type, "stack");
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_FIELD_ABI] type=%s typeKind=%d typeSize=%zu stack=%s "
                   "stackKind=%d stackSize=%zu",
                   runtime->item_field_type ? "available" : "unavailable",
                   runtime->item_field_type && patchlib_field_get_type
                       ? (int)patchlib_field_get_type(runtime->item_field_type) : -1,
                   runtime->item_field_type && patchlib_field_get_size
                       ? patchlib_field_get_size(runtime->item_field_type) : 0u,
                   runtime->item_field_stack ? "available" : "unavailable",
                   runtime->item_field_stack && patchlib_field_get_type
                       ? (int)patchlib_field_get_type(runtime->item_field_stack) : -1,
                   runtime->item_field_stack && patchlib_field_get_size
                       ? patchlib_field_get_size(runtime->item_field_stack) : 0u);
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
        !patchlib_type_get_methods(runtime->item_type, true, &methods)) {
        tefstd_vector_destroy(&methods);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_PROBE] type=available method=NewItem unavailable");
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&methods); ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t method = entry ? *entry : PATCH_NULL;
        const char *name = method ? patchlib_method_get_name(method) : NULL;
        patch_method_signature_t signature;
        size_t arg_count;
        size_t arg_index;
        if (!name || strcmp(name, "NewItem") != 0 ||
            !patchlib_method_get_signature) continue;
        memset(&signature, 0, sizeof(signature));
        if (!patchlib_method_get_signature(method, &signature)) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_METHOD] name=NewItem signature=unavailable");
            continue;
        }
        arg_count = tefstd_vector_size(&signature.arg_types);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_METHOD] name=NewItem overload=%u instance=%s returnType=%d "
                       "argCount=%zu invoke=deferred-until-NPCLoot",
                       (unsigned)overloads,
                       signature.is_instance ? "yes" : "no",
                       (int)signature.return_type, arg_count);
        ++overloads;
        for (arg_index = 0u; arg_index < arg_count; ++arg_index) {
            patch_type_t *arg_type = (patch_type_t *)tefstd_vector_at(
                &signature.arg_types, arg_index);
            const char **arg_name = (const char **)tefstd_vector_at(
                &signature.arg_names, arg_index);
            if (arg_type) {
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[ITEM_METHOD_ARG] name=NewItem index=%zu type=%d "
                               "param=%s",
                               arg_index, (int)*arg_type,
                               arg_name && *arg_name ? *arg_name : "unavailable");
            }
        }
        {
            static const patch_type_t expected[] = {
                PATCH_INT32, PATCH_INT32, PATCH_INT32, PATCH_INT32,
                PATCH_INT32, PATCH_INT32, PATCH_BOOL, PATCH_INT32, PATCH_INT32
            };
            bool exact = !signature.is_instance &&
                         signature.return_type == PATCH_INT32 &&
                         arg_count == sizeof(expected) / sizeof(expected[0]);
            for (arg_index = 0u; exact && arg_index < arg_count; ++arg_index) {
                patch_type_t *actual = (patch_type_t *)tefstd_vector_at(
                    &signature.arg_types, arg_index);
                exact = actual && *actual == expected[arg_index];
            }
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_NEWITEM_ABI] overload=%u exact9=%s invoke=deferred-until-NPCLoot",
                           (unsigned)(overloads - 1u),
                           exact ? "yes" : "no");
            if (exact && !runtime->item_new_item_signature_ready) {
                runtime->method_item_new_item = method;
                runtime->item_new_item_signature_ready = true;
                ++exact_matches;
            }
            if (!signature.is_instance && signature.return_type == PATCH_INT32 &&
                arg_count == 12u && !runtime->method_item_new_item_extended) {
                runtime->method_item_new_item_extended = method;
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[ITEM_NEWITEM_EXTENDED_ABI] available=yes "
                               "invoke=deferred reason=pointer-args-unverified");
            }
        }
        if (patchlib_method_signature_free) {
            (void)patchlib_method_signature_free(&signature);
        }
    }
    tefstd_vector_destroy(&methods);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_NEWITEM_SCAN_SUMMARY] overloads=%u exact9=%u invoke=deferred-until-NPCLoot",
                   (unsigned)overloads, (unsigned)exact_matches);
    if (!runtime->item_new_item_signature_ready) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_PROBE] type=available method=NewItem signature=unavailable");
    }
}

static void or_probe_item_ids(OR_Runtime *runtime) {
    static const char *const names[] = {
        "Gel", "Wood", "Torch", "HealingPotion", "CopperCoin"
    };
    patch_handle_t item_id_type;
    size_t i;
    if (!runtime || !patchlib_type_get_type || !patchlib_type_get_field ||
        !patchlib_field_get_name || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_is_static ||
        !patchlib_field_get_value) return;
    item_id_type = patchlib_type_get_type("Terraria.ID", "ItemID");
    if (!or_handle_is_valid(item_id_type)) {
        or_release_handle(item_id_type);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_PROBE] type=Terraria.ID.ItemID unavailable");
        return;
    }
    runtime->item_id_type_resolved = true;
    if (patchlib_type_get_method_by_param_count && patchlib_method_get_signature) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(item_id_type, "FromNetId", 1);
        patch_method_signature_t signature;
        memset(&signature, 0, sizeof(signature));
        if (or_handle_is_valid(method) && patchlib_method_get_signature(method, &signature) &&
            !signature.is_instance && signature.return_type == PATCH_INT16 &&
            tefstd_vector_size(&signature.arg_types) == 1u) {
            patch_type_t *arg = (patch_type_t *)tefstd_vector_at(&signature.arg_types, 0u);
            if (arg && *arg == PATCH_INT16) runtime->method_item_id_from_net_id = method;
        }
        if (patchlib_method_signature_free && signature.method) {
            (void)patchlib_method_signature_free(&signature);
        }
        if (!runtime->method_item_id_from_net_id) or_release_handle(method);
    }
    for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        patch_handle_t field = patchlib_type_get_field(item_id_type, names[i]);
        int32_t value = 0;
        bool valid = or_handle_is_valid(field) && patchlib_field_is_static(field) &&
                     patchlib_field_get_type(field) == PATCH_INT32 &&
                     patchlib_field_get_size(field) == sizeof(value);
        if (valid) patchlib_field_get_value(field, NULL, &value);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_PROBE] name=%s read=%s value=%d type=%d size=%zu",
                       names[i], valid ? "ok" : "unavailable", (int)value,
                       valid ? (int)patchlib_field_get_type(field) : -1,
                       valid ? patchlib_field_get_size(field) : 0u);
        or_release_handle(field);
    }
    if (patchlib_type_get_fields && tefstd_vector_init && tefstd_vector_size &&
        tefstd_vector_at && tefstd_vector_destroy) {
        tefstd_vector_t fields = {0};
        uint32_t logged = 0u;
        if (tefstd_vector_init(&fields, sizeof(patch_handle_t)) &&
            patchlib_type_get_fields(item_id_type, true, &fields)) {
            for (i = 0u; i < tefstd_vector_size(&fields) && logged < 128u; ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&fields, i);
                patch_handle_t field = entry ? *entry : PATCH_NULL;
                const char *field_name = field ? patchlib_field_get_name(field) : NULL;
                int32_t value = 0;
                if (!field_name || !patchlib_field_is_static(field) ||
                    patchlib_field_get_type(field) != PATCH_INT32 ||
                    patchlib_field_get_size(field) != sizeof(value)) continue;
                patchlib_field_get_value(field, NULL, &value);
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[ITEM_ID_FIELD] name=%s value=%d type=%d size=%zu",
                               field_name, (int)value,
                               (int)patchlib_field_get_type(field),
                               patchlib_field_get_size(field));
                ++logged;
            }
        }
        runtime->item_id_static_int_count = logged;
        runtime->item_id_static_int_ready = logged > 0u;
        tefstd_vector_destroy(&fields);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_SCAN] staticIntFields=%u limit=128",
                       (unsigned)logged);
    }
    if (patchlib_type_get_properties && patchlib_property_get_name &&
        tefstd_vector_init && tefstd_vector_size && tefstd_vector_at &&
        tefstd_vector_destroy) {
        tefstd_vector_t properties = {0};
        uint32_t logged = 0u;
        if (tefstd_vector_init(&properties, sizeof(patch_handle_t)) &&
            patchlib_type_get_properties(item_id_type, true, &properties)) {
            for (i = 0u; i < tefstd_vector_size(&properties) && logged < 128u; ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&properties, i);
                patch_handle_t property = entry ? *entry : PATCH_NULL;
                const char *name = property ? patchlib_property_get_name(property) : NULL;
                if (!name) continue;
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[ITEM_ID_MEMBER] kind=property name=%s", name);
                ++logged;
            }
        }
        tefstd_vector_destroy(&properties);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_SCAN] properties=%u limit=128",
                       (unsigned)logged);
    }
    if (patchlib_type_get_methods && patchlib_method_get_name &&
        tefstd_vector_init && tefstd_vector_size && tefstd_vector_at &&
        tefstd_vector_destroy) {
        tefstd_vector_t methods = {0};
        uint32_t logged = 0u;
        if (tefstd_vector_init(&methods, sizeof(patch_handle_t)) &&
            patchlib_type_get_methods(item_id_type, true, &methods)) {
            for (i = 0u; i < tefstd_vector_size(&methods) && logged < 128u; ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
                patch_handle_t method = entry ? *entry : PATCH_NULL;
                const char *name = method ? patchlib_method_get_name(method) : NULL;
                if (!name) continue;
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[ITEM_ID_MEMBER] kind=method name=%s", name);
                if ((strcmp(name, "GenerateLegacyItemDictionary") == 0 ||
                     strcmp(name, "FromNetId") == 0 ||
                     strcmp(name, "FromLegacyName") == 0) &&
                    patchlib_method_get_signature) {
                    patch_method_signature_t signature;
                    size_t arg_count;
                    size_t j;
                    memset(&signature, 0, sizeof(signature));
                    if (patchlib_method_get_signature(method, &signature)) {
                        arg_count = tefstd_vector_size(&signature.arg_types);
                        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                                       "[ITEM_ID_METHOD] name=%s instance=%s "
                                       "returnType=%d argCount=%zu invoke=disabled",
                                       name, signature.is_instance ? "yes" : "no",
                                       (int)signature.return_type, arg_count);
                        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                                       "[ITEM_ID_RETURN] name=%s returnType=%d "
                                       "returnSize=metadata_only invoke=disabled",
                                       name, (int)signature.return_type);
                        for (j = 0u; j < arg_count; ++j) {
                            patch_type_t *arg_type = (patch_type_t *)tefstd_vector_at(
                                &signature.arg_types, j);
                            const char **arg_name = (const char **)tefstd_vector_at(
                                &signature.arg_names, j);
                            if (arg_type) {
                                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                                               "[ITEM_ID_METHOD_ARG] name=%s index=%zu "
                                               "type=%d param=%s",
                                               name, j, (int)*arg_type,
                                               arg_name && *arg_name ? *arg_name : "unavailable");
                            }
                        }
                        if (strcmp(name, "FromNetId") == 0 ||
                            strcmp(name, "FromLegacyName") == 0) {
                            runtime->item_id_lookup_signature_ready = true;
                        }
                        /* Do not invoke FromNetId from the startup probe.  The
                         * signature is useful evidence, but this is a game
                         * runtime call and is not safe until verified on the
                         * exact mobile build. */
                        if (patchlib_method_signature_free) {
                            (void)patchlib_method_signature_free(&signature);
                        }
                    }
                }
                ++logged;
            }
        }
        runtime->item_id_method_count = logged;
        tefstd_vector_destroy(&methods);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_SCAN] methods=%u limit=128",
                       (unsigned)logged);
    }
    or_release_handle(item_id_type);
}

/* ItemID.Sets exposes useful vanilla registries, but their element types and
 * layouts vary by game build. Probe only verified static array metadata. */
static void or_probe_item_id_sets(void) {
    patch_handle_t item_id_type;
    patch_handle_t sets_type;
    tefstd_vector_t fields = {0};
    size_t i;
    uint32_t logged = 0u;
    uint32_t arrays = 0u;

    if (!patchlib_type_get_type || !patchlib_type_get_inner_type ||
        !patchlib_type_get_fields || !patchlib_field_get_name ||
        !patchlib_field_is_static || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_get_value ||
        !patchlib_array_length || !tefstd_vector_init ||
        !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_SETS] api=unavailable write=disabled");
        return;
    }
    item_id_type = patchlib_type_get_type("Terraria.ID", "ItemID");
    sets_type = or_handle_is_valid(item_id_type)
        ? patchlib_type_get_inner_type(item_id_type, "Sets") : PATCH_NULL;
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_ID_SETS] type=%s write=disabled",
                   or_handle_is_valid(sets_type) ? "available" : "unavailable");
    if (!or_handle_is_valid(sets_type) ||
        !tefstd_vector_init(&fields, sizeof(patch_handle_t)) ||
        !patchlib_type_get_fields(sets_type, true, &fields)) {
        tefstd_vector_destroy(&fields);
        or_release_handle(sets_type);
        or_release_handle(item_id_type);
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&fields) && logged < 256u; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&fields, i);
        patch_handle_t field = entry ? *entry : PATCH_NULL;
        const char *name = field ? patchlib_field_get_name(field) : NULL;
        patch_type_t type;
        size_t size;
        patch_handle_t array = PATCH_NULL;
        size_t length = 0u;
        bool candidate;
        if (!name || !or_handle_is_valid(field) ||
            !patchlib_field_is_static(field)) continue;
        type = patchlib_field_get_type(field);
        size = patchlib_field_get_size(field);
        candidate = (type == PATCH_POINTER || type == PATCH_OBJECT) && size == 8u;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_ID_SETS_FIELD] name=%s static=yes type=%d size=%zu candidate=%s",
                       name, (int)type, size, candidate ? "yes" : "no");
        ++logged;
        if (!candidate) continue;
        patchlib_field_get_value(field, NULL, &array);
        if (or_handle_is_valid(array)) {
            length = patchlib_array_length(array);
            ++arrays;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_ID_SETS_SCAN] name=%s array=ok length=%zu read=metadata-only",
                           name, length);
        } else {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_ID_SETS_SCAN] name=%s array=unavailable read=metadata-only",
                           name);
        }
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_ID_SETS_SUMMARY] fields=%u arrays=%u limit=256 write=disabled",
                   (unsigned)logged, (unsigned)arrays);
    tefstd_vector_destroy(&fields);
    or_release_handle(sets_type);
    or_release_handle(item_id_type);
}

void or_runtime_probe_main_item_array(OR_Runtime *runtime) {
    patch_handle_t main_type;
    patch_handle_t field;
    patch_handle_t array = PATCH_NULL;
    patch_type_t type;
    size_t size;
    size_t length = 0u;
    if (!runtime || !patchlib_type_get_type || !patchlib_type_get_field ||
        !patchlib_field_get_type || !patchlib_field_get_size ||
        !patchlib_field_is_static || !patchlib_field_get_value) return;
    main_type = patchlib_type_get_type("Terraria", "Main");
    field = or_handle_is_valid(runtime->main_item_field)
        ? runtime->main_item_field
        : (or_handle_is_valid(main_type)
           ? patchlib_type_get_field(main_type, "item") : PATCH_NULL);
    if (!or_handle_is_valid(field) || !patchlib_field_is_static(field)) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[MAIN_ITEM_ARRAY] field=unavailable read=disabled");
        or_release_handle(field);
        or_release_handle(main_type);
        return;
    }
    type = patchlib_field_get_type(field);
    size = patchlib_field_get_size(field);
    if (!runtime->main_item_field) {
        runtime->main_item_field = field;
        field = PATCH_NULL;
    }
    patchlib_field_get_value(field, NULL, &array);
    if (or_handle_is_valid(array) && patchlib_array_length) {
        length = patchlib_array_length(array);
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[MAIN_ITEM_ARRAY] field=available static=yes type=%d size=%zu array=%s length=%zu read=metadata-only",
                   (int)type, size, or_handle_is_valid(array) ? "ok" : "unavailable", length);
    or_release_handle(field);
    or_release_handle(main_type);
}

static void or_scan_main_item_members(void) {
    patch_handle_t main_type;
    tefstd_vector_t fields = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!patchlib_type_get_type || !patchlib_type_get_fields ||
        !patchlib_field_get_name || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_is_static ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    main_type = patchlib_type_get_type("Terraria", "Main");
    if (!or_handle_is_valid(main_type) ||
        !tefstd_vector_init(&fields, sizeof(patch_handle_t)) ||
        !patchlib_type_get_fields(main_type, true, &fields)) {
        tefstd_vector_destroy(&fields);
        or_release_handle(main_type);
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&fields) && logged < 256u; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&fields, i);
        patch_handle_t field = entry ? *entry : PATCH_NULL;
        const char *name = field ? patchlib_field_get_name(field) : NULL;
        char lowered[96];
        size_t j;
        if (!name) continue;
        for (j = 0u; j + 1u < sizeof(lowered) && name[j] != '\0'; ++j) {
            lowered[j] = (char)tolower((unsigned char)name[j]);
        }
        lowered[j] = '\0';
        if (!strstr(lowered, "item") && !strstr(lowered, "newitem") &&
            !strstr(lowered, "spawn")) continue;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[MAIN_ITEM_MEMBER] name=%s static=%s type=%d size=%zu read=metadata-only",
                       name, patchlib_field_is_static(field) ? "yes" : "no",
                       (int)patchlib_field_get_type(field),
                       patchlib_field_get_size(field));
        ++logged;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[MAIN_ITEM_SCAN] members=%u limit=256 read=metadata-only",
                   (unsigned)logged);
    tefstd_vector_destroy(&fields);
    or_release_handle(main_type);
}

static void or_scan_main_item_methods(void) {
    patch_handle_t main_type;
    tefstd_vector_t methods = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!patchlib_type_get_type || !patchlib_type_get_methods ||
        !patchlib_method_get_name || !patchlib_method_get_signature ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    main_type = patchlib_type_get_type("Terraria", "Main");
    if (!or_handle_is_valid(main_type) ||
        !tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
        !patchlib_type_get_methods(main_type, true, &methods)) {
        tefstd_vector_destroy(&methods);
        or_release_handle(main_type);
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&methods) && logged < 256u; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t method = entry ? *entry : PATCH_NULL;
        const char *name = method ? patchlib_method_get_name(method) : NULL;
        char lowered[128];
        size_t j;
        patch_method_signature_t sig;
        if (!name) continue;
        for (j = 0u; j + 1u < sizeof(lowered) && name[j] != '\0'; ++j)
            lowered[j] = (char)tolower((unsigned char)name[j]);
        lowered[j] = '\0';
        if (!strstr(lowered, "item") && !strstr(lowered, "drop") &&
            !strstr(lowered, "spawn")) continue;
        memset(&sig, 0, sizeof(sig));
        if (!patchlib_method_get_signature(method, &sig)) continue;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[MAIN_ITEM_METHOD] name=%s instance=%s returnType=%d argCount=%zu invoke=disabled",
                       name, sig.is_instance ? "yes" : "no", (int)sig.return_type,
                       tefstd_vector_size(&sig.arg_types));
        {
            size_t arg_index;
            for (arg_index = 0u;
                 arg_index < tefstd_vector_size(&sig.arg_types);
                 ++arg_index) {
                patch_type_t *arg_type = (patch_type_t *)tefstd_vector_at(
                    &sig.arg_types, arg_index);
                const char **arg_name = (const char **)tefstd_vector_at(
                    &sig.arg_names, arg_index);
                if (arg_type) {
                    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                                   "[MAIN_ITEM_METHOD_ARG] name=%s index=%zu type=%d param=%s",
                                   name, arg_index, (int)*arg_type,
                                   arg_name && *arg_name ? *arg_name : "unavailable");
                }
            }
        }
        if (patchlib_method_signature_free) (void)patchlib_method_signature_free(&sig);
        ++logged;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[MAIN_ITEM_METHOD_SCAN] methods=%u limit=256 invoke=disabled",
                   (unsigned)logged);
    tefstd_vector_destroy(&methods);
    or_release_handle(main_type);
}

static bool or_item_name_candidate(const char *name) {
    static const char *const tokens[] = {
        "type", "stack", "prefix", "value", "create", "clone", "find",
        "lookup", "setdefaults", "newitem", "item"
    };
    char lowered[128];
    size_t i;
    size_t j;
    if (!name) return false;
    for (j = 0u; j + 1u < sizeof(lowered) && name[j] != '\0'; ++j) {
        lowered[j] = (char)tolower((unsigned char)name[j]);
    }
    lowered[j] = '\0';
    for (i = 0u; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        if (strstr(lowered, tokens[i]) != NULL) return true;
    }
    return false;
}

static bool or_item_method_candidate(const char *name) {
    return or_item_name_candidate(name);
}

static void or_scan_item_surface(OR_Runtime *runtime) {
    tefstd_vector_t entries = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!runtime || !runtime->item_type || !patchlib_type_get_fields ||
        !patchlib_field_get_name || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_is_static ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    if (tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_fields(runtime->item_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) && logged < 256u; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t field = entry ? *entry : PATCH_NULL;
            const char *name = field ? patchlib_field_get_name(field) : NULL;
            if (!or_item_name_candidate(name)) continue;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_MEMBER] kind=field name=%s static=%s type=%d size=%zu",
                           name, patchlib_field_is_static(field) ? "yes" : "no",
                           (int)patchlib_field_get_type(field),
                           patchlib_field_get_size(field));
            ++logged;
        }
    }
    tefstd_vector_destroy(&entries);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_MEMBER_SCAN] kind=field logged=%u limit=256",
                   (unsigned)logged);
    logged = 0u;
    if (patchlib_type_get_methods && patchlib_method_get_name &&
        tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_methods(runtime->item_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) && logged < 256u; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t method = entry ? *entry : PATCH_NULL;
            const char *name = method ? patchlib_method_get_name(method) : NULL;
            if (!or_item_name_candidate(name)) continue;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_MEMBER] kind=method name=%s", name);
            ++logged;
        }
    }
    tefstd_vector_destroy(&entries);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_MEMBER_SCAN] kind=method logged=%u limit=256",
                   (unsigned)logged);
    logged = 0u;
    if (patchlib_type_get_properties && patchlib_property_get_name &&
        tefstd_vector_init && tefstd_vector_size && tefstd_vector_at &&
        tefstd_vector_destroy &&
        tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_properties(runtime->item_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) && logged < 256u; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t property = entry ? *entry : PATCH_NULL;
            const char *name = property ? patchlib_property_get_name(property) : NULL;
            if (!or_item_name_candidate(name)) continue;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_MEMBER] kind=property name=%s", name);
            ++logged;
        }
    }
    tefstd_vector_destroy(&entries);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_MEMBER_SCAN] kind=property logged=%u limit=256",
                   (unsigned)logged);
}

static void or_probe_item_method_signatures(OR_Runtime *runtime) {
    tefstd_vector_t methods = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!runtime || !runtime->item_type || !patchlib_type_get_methods ||
        !patchlib_method_get_name || !patchlib_method_get_signature ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
        !patchlib_type_get_methods(runtime->item_type, true, &methods)) {
        tefstd_vector_destroy(&methods);
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&methods) && logged < 128u; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t method = entry ? *entry : PATCH_NULL;
        const char *name = method ? patchlib_method_get_name(method) : NULL;
        patch_method_signature_t signature;
        size_t arg_count;
        size_t arg_index;
        if (!name || !or_item_method_candidate(name)) continue;
        memset(&signature, 0, sizeof(signature));
        if (!patchlib_method_get_signature(method, &signature)) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[ITEM_METHOD_SIG] name=%s signature=unavailable invoke=disabled",
                           name);
            ++logged;
            continue;
        }
        arg_count = tefstd_vector_size(&signature.arg_types);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[ITEM_METHOD_SIG] name=%s instance=%s returnType=%d argCount=%zu invoke=disabled",
                       name, signature.is_instance ? "yes" : "no",
                       (int)signature.return_type, arg_count);
        for (arg_index = 0u; arg_index < arg_count; ++arg_index) {
            patch_type_t *arg_type = (patch_type_t *)tefstd_vector_at(
                &signature.arg_types, arg_index);
            const char **arg_name = (const char **)tefstd_vector_at(
                &signature.arg_names, arg_index);
            if (arg_type) {
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[ITEM_METHOD_SIG_ARG] name=%s index=%zu type=%d param=%s",
                               name, arg_index, (int)*arg_type,
                               arg_name && *arg_name ? *arg_name : "unavailable");
            }
        }
        if (patchlib_method_signature_free) {
            (void)patchlib_method_signature_free(&signature);
        }
        ++logged;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[ITEM_METHOD_SIG_SUMMARY] candidates=%u limit=128 invoke=disabled",
                   (unsigned)logged);
    tefstd_vector_destroy(&methods);
}

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

static bool or_world_member_name(const char *name) {
    static const char *const tokens[] = {
        "day", "night", "moon", "rain", "snow", "blood", "eclipse",
        "sandstorm", "cloud", "wind", "world", "weather", "hell",
        "underworld", "layer", "depth", "tile"
    };
    char lowered[128];
    size_t i;
    size_t j;
    if (!name) return false;
    for (i = 0u; i + 1u < sizeof(lowered) && name[i] != '\0'; ++i) {
        lowered[i] = (char)tolower((unsigned char)name[i]);
    }
    lowered[i] = '\0';
    for (j = 0u; j < sizeof(tokens) / sizeof(tokens[0]); ++j) {
        if (strstr(lowered, tokens[j]) != NULL) return true;
    }
    return false;
}

static void or_scan_world_members(patch_handle_t main_type) {
    tefstd_vector_t entries = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!main_type || !patchlib_type_get_fields || !patchlib_field_get_name ||
        !patchlib_field_get_size || !patchlib_field_get_type ||
        !patchlib_field_is_static || !patchlib_field_is_instance ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    if (tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_fields(main_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) &&
             logged < OR_WORLD_MEMBER_LIMIT; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t field = entry ? *entry : PATCH_NULL;
            const char *name = field ? patchlib_field_get_name(field) : NULL;
            if (or_world_member_name(name)) {
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[WORLD_MEMBER] kind=field name=%s instance=%s "
                               "static=%s size=%zu type=%d",
                               name,
                               patchlib_field_is_instance(field) ? "yes" : "no",
                               patchlib_field_is_static(field) ? "yes" : "no",
                               patchlib_field_get_size(field),
                               (int)patchlib_field_get_type(field));
                logged += 1u;
            }
        }
    }
    tefstd_vector_destroy(&entries);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[WORLD_SCAN] logged=%u limit=%u",
                   (unsigned)logged, (unsigned)OR_WORLD_MEMBER_LIMIT);
}

static bool or_biome_member_name(const char *name) {
    static const char *const tokens[] = {
        "biome", "zone", "forest", "jungle", "desert", "snow",
        "corrupt", "crimson", "hallow", "ocean", "dungeon", "beach"
    };
    char lowered[160];
    size_t i;
    size_t j;
    if (!name) return false;
    for (i = 0u; i + 1u < sizeof(lowered) && name[i] != '\0'; ++i) {
        lowered[i] = (char)tolower((unsigned char)name[i]);
    }
    lowered[i] = '\0';
    for (j = 0u; j < sizeof(tokens) / sizeof(tokens[0]); ++j) {
        if (strstr(lowered, tokens[j]) != NULL) return true;
    }
    return false;
}

static void or_scan_biome_type(const char *type_namespace,
                               const char *type_name) {
    tefstd_vector_t entries = {0};
    patch_handle_t type;
    size_t i;
    uint32_t logged = 0u;
    if (!type_namespace || !type_name || !patchlib_type_get_type ||
        !patchlib_type_get_fields || !patchlib_field_get_name ||
        !patchlib_field_get_size || !patchlib_field_get_type ||
        !patchlib_field_is_static || !patchlib_field_is_instance ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    type = patchlib_type_get_type(type_namespace, type_name);
    if (!or_handle_is_valid(type)) {
        or_release_handle(type);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[BIOME_SCAN] type=%s.%s status=unavailable",
                       type_namespace, type_name);
        return;
    }
    if (tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_fields(type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) &&
             logged < OR_BIOME_MEMBER_LIMIT; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t field = entry ? *entry : PATCH_NULL;
            const char *name = field ? patchlib_field_get_name(field) : NULL;
            if (!or_biome_member_name(name)) continue;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[BIOME_MEMBER] type=%s.%s kind=field name=%s "
                           "instance=%s static=%s type=%d size=%zu read=disabled write=disabled",
                           type_namespace, type_name, name,
                           patchlib_field_is_instance(field) ? "yes" : "no",
                           patchlib_field_is_static(field) ? "yes" : "no",
                           (int)patchlib_field_get_type(field),
                           patchlib_field_get_size(field));
            ++logged;
        }
    }
    tefstd_vector_destroy(&entries);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[BIOME_SCAN] type=%s.%s fields=%u limit=%u",
                   type_namespace, type_name, (unsigned)logged,
                   (unsigned)OR_BIOME_MEMBER_LIMIT);

    logged = 0u;
    if (patchlib_type_get_properties && patchlib_property_get_name &&
        tefstd_vector_init && tefstd_vector_size && tefstd_vector_at &&
        tefstd_vector_destroy) {
        if (tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
            patchlib_type_get_properties(type, true, &entries)) {
            for (i = 0u; i < tefstd_vector_size(&entries) &&
                 logged < OR_BIOME_MEMBER_LIMIT; ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
                patch_handle_t property = entry ? *entry : PATCH_NULL;
                const char *name = property ? patchlib_property_get_name(property) : NULL;
                patch_handle_t getter = property && patchlib_property_get_get_method
                    ? patchlib_property_get_get_method(property) : PATCH_NULL;
                patch_handle_t setter = property && patchlib_property_get_set_method
                    ? patchlib_property_get_set_method(property) : PATCH_NULL;
                if (!or_biome_member_name(name)) continue;
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[BIOME_PROPERTY] type=%s.%s name=%s getter=%s "
                               "setter=%s invoke=disabled",
                               type_namespace, type_name, name,
                               getter ? "available" : "unavailable",
                               setter ? "available" : "unavailable");
                ++logged;
            }
        }
        tefstd_vector_destroy(&entries);
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[BIOME_SCAN] type=%s.%s properties=%u limit=%u",
                   type_namespace, type_name, (unsigned)logged,
                   (unsigned)OR_BIOME_MEMBER_LIMIT);

    logged = 0u;
    if (patchlib_type_get_methods && patchlib_method_get_name &&
        patchlib_method_get_signature && tefstd_vector_init &&
        tefstd_vector_size && tefstd_vector_at && tefstd_vector_destroy) {
        if (tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
            patchlib_type_get_methods(type, true, &entries)) {
            for (i = 0u; i < tefstd_vector_size(&entries) &&
                 logged < OR_BIOME_MEMBER_LIMIT; ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
                patch_handle_t method = entry ? *entry : PATCH_NULL;
                const char *name = method ? patchlib_method_get_name(method) : NULL;
                patch_method_signature_t signature;
                if (!or_biome_member_name(name)) continue;
                memset(&signature, 0, sizeof(signature));
                if (!patchlib_method_get_signature(method, &signature)) continue;
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[BIOME_METHOD] type=%s.%s name=%s instance=%s "
                               "returnType=%d argCount=%zu invoke=disabled",
                               type_namespace, type_name, name,
                               signature.is_instance ? "yes" : "no",
                               (int)signature.return_type,
                               tefstd_vector_size(&signature.arg_types));
                if (patchlib_method_signature_free) {
                    (void)patchlib_method_signature_free(&signature);
                }
                ++logged;
            }
        }
        tefstd_vector_destroy(&entries);
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[BIOME_SCAN] type=%s.%s methods=%u limit=%u",
                   type_namespace, type_name, (unsigned)logged,
                   (unsigned)OR_BIOME_MEMBER_LIMIT);
    or_release_handle(type);
}

static void or_scan_biome_members(void) {
    or_scan_biome_type("Terraria", "NPC");
    or_scan_biome_type("Terraria", "Player");
}

static void or_probe_player_source(patch_handle_t main_type) {
    static const char *const field_names[] = {
        "player", "Player", "myPlayer", "LocalPlayer", "localPlayer"
    };
    static const char *const property_names[] = {
        "LocalPlayer", "Player", "MyPlayer"
    };
    size_t i;
    if (!main_type) return;
    for (i = 0u; i < sizeof(field_names) / sizeof(field_names[0]); ++i) {
        patch_handle_t field = patchlib_type_get_field
            ? patchlib_type_get_field(main_type, field_names[i]) : PATCH_NULL;
        if (!or_handle_is_valid(field)) {
            or_release_handle(field);
            continue;
        }
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[PLAYER_SOURCE_MEMBER] kind=field name=%s instance=%s "
                       "static=%s type=%d size=%zu read=disabled write=disabled",
                       patchlib_field_get_name ? patchlib_field_get_name(field)
                                                : field_names[i],
                       patchlib_field_is_instance && patchlib_field_is_instance(field)
                           ? "yes" : "no",
                       patchlib_field_is_static && patchlib_field_is_static(field)
                           ? "yes" : "no",
                       patchlib_field_get_type ? (int)patchlib_field_get_type(field) : -1,
                       patchlib_field_get_size ? patchlib_field_get_size(field) : 0u);
        or_release_handle(field);
    }
    if (!patchlib_type_get_properties || !patchlib_property_get_name) return;
    for (i = 0u; i < sizeof(property_names) / sizeof(property_names[0]); ++i) {
        patch_handle_t property = patchlib_type_get_property
            ? patchlib_type_get_property(main_type, property_names[i]) : PATCH_NULL;
        patch_handle_t getter;
        if (!or_handle_is_valid(property)) {
            or_release_handle(property);
            continue;
        }
        getter = patchlib_property_get_get_method
            ? patchlib_property_get_get_method(property) : PATCH_NULL;
        if (getter && patchlib_method_get_signature) {
            patch_method_signature_t signature;
            memset(&signature, 0, sizeof(signature));
            if (patchlib_method_get_signature(getter, &signature)) {
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[PLAYER_SOURCE_METHOD] property=%s getter="
                               "instance=%s returnType=%d argCount=%zu invoke=disabled",
                               patchlib_property_get_name(property),
                               signature.is_instance ? "yes" : "no",
                               (int)signature.return_type,
                               tefstd_vector_size(&signature.arg_types));
                if (patchlib_method_signature_free) {
                    (void)patchlib_method_signature_free(&signature);
                }
            }
        }
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[PLAYER_SOURCE_MEMBER] kind=property name=%s getter=%s "
                       "setter=%s invoke=disabled",
                       patchlib_property_get_name(property), getter ? "available" : "unavailable",
                       patchlib_property_get_set_method &&
                           patchlib_property_get_set_method(property)
                           ? "available" : "unavailable");
        or_release_handle(property);
    }
}

static patch_handle_t or_resolve_bool_property_getter(patch_handle_t type,
                                                      const char *name) {
    patch_handle_t property;
    patch_handle_t getter;
    patch_method_signature_t signature;
    if (!type || !name || !patchlib_type_get_property ||
        !patchlib_property_get_get_method || !patchlib_method_get_signature) {
        return PATCH_NULL;
    }
    property = patchlib_type_get_property(type, name);
    getter = or_handle_is_valid(property)
        ? patchlib_property_get_get_method(property) : PATCH_NULL;
    memset(&signature, 0, sizeof(signature));
    if (!or_handle_is_valid(getter) ||
        !patchlib_method_get_signature(getter, &signature) ||
        !signature.is_instance || signature.return_type != PATCH_BOOL ||
        tefstd_vector_size(&signature.arg_types) != 0u) {
        if (patchlib_method_signature_free && signature.method) {
            (void)patchlib_method_signature_free(&signature);
        }
        or_release_handle(property);
        or_release_handle(getter);
        return PATCH_NULL;
    }
    if (patchlib_method_signature_free) {
        (void)patchlib_method_signature_free(&signature);
    }
    or_release_handle(property);
    return getter;
}

static void or_resolve_biome_runtime(OR_Runtime *runtime,
                                     patch_handle_t player_type) {
    patch_handle_t main_type;
    patch_handle_t property;
    patch_handle_t getter;
    patch_method_signature_t signature;
    if (!runtime || !patchlib_type_get_type || !player_type) return;
    main_type = patchlib_type_get_type("Terraria", "Main");
    if (or_handle_is_valid(main_type) && patchlib_type_get_property &&
        patchlib_property_get_get_method && patchlib_method_get_signature) {
        patch_handle_t player_field = patchlib_type_get_field
            ? patchlib_type_get_field(main_type, "player") : PATCH_NULL;
        if (or_handle_is_valid(player_field) && patchlib_field_is_static &&
            patchlib_field_get_type && patchlib_field_get_size &&
            patchlib_field_is_static(player_field) &&
            patchlib_field_get_type(player_field) == PATCH_OBJECT &&
            patchlib_field_get_size(player_field) == sizeof(patch_handle_t)) {
            runtime->main_player_field_probe = player_field;
            player_field = PATCH_NULL;
        }
        or_release_handle(player_field);
        player_field = patchlib_type_get_field
            ? patchlib_type_get_field(main_type, "myPlayer") : PATCH_NULL;
        if (or_handle_is_valid(player_field) && patchlib_field_is_static &&
            patchlib_field_get_type && patchlib_field_get_size &&
            patchlib_field_is_static(player_field) &&
            patchlib_field_get_type(player_field) == PATCH_INT32 &&
            patchlib_field_get_size(player_field) == sizeof(int32_t)) {
            runtime->main_my_player_field_probe = player_field;
            player_field = PATCH_NULL;
        }
        or_release_handle(player_field);
        property = patchlib_type_get_property(main_type, "LocalPlayer");
        getter = or_handle_is_valid(property)
            ? patchlib_property_get_get_method(property) : PATCH_NULL;
        memset(&signature, 0, sizeof(signature));
        if (or_handle_is_valid(getter) &&
            patchlib_method_get_signature(getter, &signature) &&
            !signature.is_instance && signature.return_type == PATCH_OBJECT &&
            tefstd_vector_size(&signature.arg_types) == 0u) {
            runtime->main_local_player_get = getter;
            getter = PATCH_NULL;
        }
        if (patchlib_method_signature_free && signature.method) {
            (void)patchlib_method_signature_free(&signature);
        }
        or_release_handle(property);
        or_release_handle(getter);
    }
    or_release_handle(main_type);
    runtime->player_zone_corrupt_get = or_resolve_bool_property_getter(
        player_type, "ZoneCorrupt");
    runtime->player_zone_crimson_get = or_resolve_bool_property_getter(
        player_type, "ZoneCrimson");
    runtime->player_zone_hallow_get = or_resolve_bool_property_getter(
        player_type, "ZoneHallow");
    runtime->player_zone_jungle_get = or_resolve_bool_property_getter(
        player_type, "ZoneJungle");
    runtime->player_zone_snow_get = or_resolve_bool_property_getter(
        player_type, "ZoneSnow");
    runtime->player_zone_desert_get = or_resolve_bool_property_getter(
        player_type, "ZoneDesert");
    runtime->player_zone_beach_get = or_resolve_bool_property_getter(
        player_type, "ZoneBeach");
    runtime->player_zone_glowshroom_get = or_resolve_bool_property_getter(
        player_type, "ZoneGlowshroom");
    runtime->player_shopping_forest_get = or_resolve_bool_property_getter(
        player_type, "ShoppingZone_Forest");
    runtime->player_shopping_any_biome_get = or_resolve_bool_property_getter(
        player_type, "ShoppingZone_AnyBiome");
    runtime->player_position_field_probe = or_resolve_field(
        player_type, "position", true, PATCH_POINTER, 8u);
    if (!runtime->player_position_field_probe) {
        runtime->player_position_field_probe = or_resolve_field(
            player_type, "Position", true, PATCH_POINTER, 8u);
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[BIOME_READ_CAPABILITY] localPlayer=%s corrupt=%s crimson=%s "
                   "hallow=%s jungle=%s snow=%s desert=%s beach=%s invoke=enabled",
                   runtime->main_local_player_get ? "ready" : "unavailable",
                   runtime->player_zone_corrupt_get ? "ready" : "unavailable",
                   runtime->player_zone_crimson_get ? "ready" : "unavailable",
                   runtime->player_zone_hallow_get ? "ready" : "unavailable",
                   runtime->player_zone_jungle_get ? "ready" : "unavailable",
                   runtime->player_zone_snow_get ? "ready" : "unavailable",
                   runtime->player_zone_desert_get ? "ready" : "unavailable",
                   runtime->player_zone_beach_get ? "ready" : "unavailable");
}

void or_runtime_init(OR_Runtime *runtime) {
    size_t i;
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->spawn_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->death_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->ai_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_observer_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->strike_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->display_name_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->display_name_hook_id_alt = PATCH_HOOK_INVALID_ID;
    runtime->display_name_hook_id_third = PATCH_HOOK_INVALID_ID;
    for (i = 0; i < OR_MOUSE_TEXT_METHOD_LIMIT; ++i) {
        runtime->mouse_text_hook_ids[i] = PATCH_HOOK_INVALID_ID;
    }
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

static patch_handle_t or_probe_spawn_member(patch_handle_t main_type,
                                            const char *name) {
    patch_handle_t field;
    if (!main_type || !name || !patchlib_type_get_field) return PATCH_NULL;
    field = patchlib_type_get_field(main_type, name);
    if (!or_handle_is_valid(field) || !patchlib_field_get_name ||
        !patchlib_field_get_type || !patchlib_field_get_size ||
        !patchlib_field_is_instance || !patchlib_field_is_static) {
        or_release_handle(field);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[SPAWN_RATE_MEMBER] name=%s status=unavailable", name);
        return PATCH_NULL;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[SPAWN_RATE_MEMBER] name=%s instance=%s static=%s "
                   "type=%d size=%zu read=deferred write=disabled",
                   patchlib_field_get_name(field),
                   patchlib_field_is_instance(field) ? "yes" : "no",
                   patchlib_field_is_static(field) ? "yes" : "no",
                   (int)patchlib_field_get_type(field),
                   patchlib_field_get_size(field));
    if (!patchlib_field_is_static(field) || patchlib_field_is_instance(field) ||
        patchlib_field_get_type(field) != PATCH_INT32 ||
        patchlib_field_get_size(field) != sizeof(int32_t)) {
        or_release_handle(field);
        return PATCH_NULL;
    }
    return field;
}

static void or_probe_spawn_rate_members(OR_Runtime *runtime,
                                        patch_handle_t main_type) {
    static const char *const spawn_rate_names[] = {"spawnRate", "SpawnRate"};
    static const char *const max_spawns_names[] = {"maxSpawns", "MaxSpawns"};
    size_t i;
    if (!runtime || !main_type) return;
    for (i = 0u; i < sizeof(spawn_rate_names) / sizeof(spawn_rate_names[0]); ++i) {
        runtime->main_spawn_rate = or_probe_spawn_member(main_type,
                                                         spawn_rate_names[i]);
        if (runtime->main_spawn_rate) break;
    }
    for (i = 0u; i < sizeof(max_spawns_names) / sizeof(max_spawns_names[0]); ++i) {
        runtime->main_max_spawns = or_probe_spawn_member(main_type,
                                                         max_spawns_names[i]);
        if (runtime->main_max_spawns) break;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[SPAWN_RATE_CAPABILITY] spawnRate=%s maxSpawns=%s "
                   "read=deferred write=disabled",
                   runtime->main_spawn_rate ? "verified-int32" : "unavailable",
                   runtime->main_max_spawns ? "verified-int32" : "unavailable");
}

static void or_probe_position_vector2_type(OR_Runtime *runtime) {
    static const char *const namespaces[] = {
        "Microsoft.Xna.Framework",
        "System.Numerics"
    };
    size_t i;
    if (!runtime || !patchlib_type_get_type) return;
    for (i = 0u; i < sizeof(namespaces) / sizeof(namespaces[0]); ++i) {
        patch_handle_t type = patchlib_type_get_type(namespaces[i], "Vector2");
        patch_handle_t x;
        patch_handle_t y;
        if (!or_handle_is_valid(type)) {
            or_release_handle(type);
            continue;
        }
        x = or_resolve_field(type, "X", true, PATCH_FLOAT, sizeof(float));
        y = or_resolve_field(type, "Y", true, PATCH_FLOAT, sizeof(float));
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[TERRAIN_POSITION_TYPE] candidate=%s type=Vector2 "
                       "xField=%s yField=%s read=deferred",
                       namespaces[i], x ? "verified" : "unavailable",
                       y ? "verified" : "unavailable");
        if (x && y) {
            runtime->position_vector2_type_probe = type;
            runtime->position_vector2_x_probe = x;
            runtime->position_vector2_y_probe = y;
            runtime->position_vector2_metadata_ready = true;
            runtime->position_value_probe_ready = true;
            return;
        }
        or_release_handle(x);
        or_release_handle(y);
        or_release_handle(type);
    }
}

static bool or_position_name_candidate(const char *name) {
    if (!name) return false;
    return strstr(name, "position") != NULL || strstr(name, "Position") != NULL ||
           strstr(name, "center") != NULL || strstr(name, "Center") != NULL ||
           strstr(name, "bottom") != NULL || strstr(name, "Bottom") != NULL ||
           strstr(name, "top") != NULL || strstr(name, "Top") != NULL;
}

static void or_scan_position_accessors(OR_Runtime *runtime) {
    tefstd_vector_t entries = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!runtime || !runtime->npc_type || !patchlib_type_get_properties ||
        !patchlib_property_get_name || !patchlib_property_get_get_method ||
        !patchlib_method_get_signature || !tefstd_vector_init ||
        !tefstd_vector_size || !tefstd_vector_at || !tefstd_vector_destroy) return;
    if (tefstd_vector_init(&entries, sizeof(patch_handle_t)) &&
        patchlib_type_get_properties(runtime->npc_type, true, &entries)) {
        for (i = 0u; i < tefstd_vector_size(&entries) && logged < 32u; ++i) {
            patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&entries, i);
            patch_handle_t property = entry ? *entry : PATCH_NULL;
            const char *name = property ? patchlib_property_get_name(property) : NULL;
            patch_handle_t getter;
            patch_method_signature_t signature;
            if (!or_position_name_candidate(name)) continue;
            getter = patchlib_property_get_get_method(property);
            memset(&signature, 0, sizeof(signature));
            if (!getter || !patchlib_method_get_signature(getter, &signature)) continue;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[POSITION_ACCESSOR] kind=property name=%s getter=available "
                           "instance=%s returnType=%d argCount=%zu read=deferred",
                           name, signature.is_instance ? "yes" : "no",
                           (int)signature.return_type,
                           tefstd_vector_size(&signature.arg_types));
            if (patchlib_method_signature_free) (void)patchlib_method_signature_free(&signature);
            ++logged;
        }
    }
    tefstd_vector_destroy(&entries);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[POSITION_ACCESSOR_SCAN] properties=%u limit=32 read=deferred",
                   (unsigned)logged);
}

static void or_probe_velocity_member(patch_handle_t npc_type) {
    static const char *const names[] = {"velocity", "Velocity"};
    size_t i;
    if (!npc_type || !patchlib_type_get_field || !patchlib_field_get_name ||
        !patchlib_field_get_type || !patchlib_field_get_size ||
        !patchlib_field_is_instance || !patchlib_field_is_static) return;
    for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        patch_handle_t field = patchlib_type_get_field(npc_type, names[i]);
        if (!or_handle_is_valid(field)) {
            or_release_handle(field);
            continue;
        }
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[AI_DASH_MEMBER] name=%s instance=%s static=%s "
                       "type=%d size=%zu representation=%s read=deferred",
                       patchlib_field_get_name(field),
                       patchlib_field_is_instance(field) ? "yes" : "no",
                       patchlib_field_is_static(field) ? "yes" : "no",
                       (int)patchlib_field_get_type(field),
                       patchlib_field_get_size(field),
                       patchlib_field_get_type(field) == PATCH_POINTER &&
                       patchlib_field_get_size(field) == 8u
                           ? "pointer8-unverified" : "unclassified");
        or_release_handle(field);
        return;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[AI_DASH_MEMBER] name=velocity status=unavailable");
}

static patch_handle_t or_probe_field(patch_handle_t type, const char *name) {
    patch_handle_t field;
    if (!type || !name || !patchlib_type_get_field) return PATCH_NULL;
    field = patchlib_type_get_field(type, name);
    if (!or_handle_is_valid(field) || !patchlib_field_get_name ||
        !patchlib_field_get_size || !patchlib_field_get_type ||
        !patchlib_field_is_static || !patchlib_field_is_instance) {
        or_release_handle(field);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[TERRAIN_MEMBER] name=%s status=unavailable", name);
        return PATCH_NULL;
    }
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[TERRAIN_MEMBER] kind=field name=%s instance=%s static=%s "
                   "size=%zu type=%d read=deferred",
                   patchlib_field_get_name(field),
                   patchlib_field_is_instance(field) ? "yes" : "no",
                   patchlib_field_is_static(field) ? "yes" : "no",
                   patchlib_field_get_size(field),
                   (int)patchlib_field_get_type(field));
    return field;
}

static patch_handle_t or_resolve_marker_field(patch_handle_t type,
                                              const char *name) {
    patch_handle_t field;
    if (!type || !patchlib_type_get_field) return PATCH_NULL;
    field = patchlib_type_get_field(type, name);
    if (!or_handle_is_valid(field) || !patchlib_field_is_instance ||
        !patchlib_field_is_static || !patchlib_field_get_type ||
        !patchlib_field_get_size || !patchlib_field_get_pointer) {
        or_release_handle(field);
        return PATCH_NULL;
    }
    /* Terraria 1.4.5.6.4 reports NPC.color as an 8-byte PATCH_POINTER. Keep
     * the handle for a read-only diagnostic probe, but do not mark it as a
     * writable color capability until the pointed representation is proven. */
    if (!patchlib_field_is_instance(field) || patchlib_field_is_static(field) ||
        patchlib_field_get_type(field) != PATCH_POINTER ||
        patchlib_field_get_size(field) != 8u) {
        or_release_handle(field);
        return PATCH_NULL;
    }
    return field;
}

static void or_probe_terrain_boundary_candidates(patch_handle_t main_type) {
    static const struct {
        const char *name;
        patch_type_t type;
        size_t size;
    } candidates[] = {
        {"UnderworldLayer", PATCH_INT32, sizeof(int32_t)},
        {"underworldLayer", PATCH_INT32, sizeof(int32_t)},
        {"underWorldLayer", PATCH_INT32, sizeof(int32_t)},
        {"maxTilesY", PATCH_INT32, sizeof(int32_t)},
        {"MaxTilesY", PATCH_INT32, sizeof(int32_t)},
        {"worldHeight", PATCH_INT32, sizeof(int32_t)},
        {"WorldHeight", PATCH_INT32, sizeof(int32_t)},
        {"maxWorldTilesY", PATCH_INT32, sizeof(int32_t)}
    };
    size_t i;
    if (!main_type) return;
    for (i = 0u; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        patch_handle_t field = or_resolve_field(main_type, candidates[i].name,
                                                false, candidates[i].type,
                                                candidates[i].size);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[TERRAIN_BOUNDARY_MEMBER] name=%s status=%s type=%d size=%zu",
                       candidates[i].name, field ? "verified" : "unavailable",
                       (int)candidates[i].type, candidates[i].size);
        or_release_handle(field);
    }
}

static bool or_terrain_method_name(const char *name) {
    static const char *const tokens[] = {
        "hell", "underworld", "layer", "depth", "worldheight", "tile"
    };
    char lowered[160];
    size_t i;
    size_t j;
    if (!name) return false;
    for (i = 0u; i + 1u < sizeof(lowered) && name[i] != '\0'; ++i) {
        lowered[i] = (char)tolower((unsigned char)name[i]);
    }
    lowered[i] = '\0';
    for (j = 0u; j < sizeof(tokens) / sizeof(tokens[0]); ++j) {
        if (strstr(lowered, tokens[j]) != NULL) return true;
    }
    return false;
}

static void or_scan_terrain_methods(patch_handle_t main_type) {
    tefstd_vector_t methods = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!main_type || !patchlib_type_get_methods || !patchlib_method_get_name ||
        !patchlib_method_get_signature || !tefstd_vector_init ||
        !tefstd_vector_size || !tefstd_vector_at || !tefstd_vector_destroy) return;
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
        !patchlib_type_get_methods(main_type, true, &methods)) {
        tefstd_vector_destroy(&methods);
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&methods) && logged < 64u; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t method = entry ? *entry : PATCH_NULL;
        const char *name = method ? patchlib_method_get_name(method) : NULL;
        patch_method_signature_t signature;
        size_t arg_count;
        if (!or_terrain_method_name(name)) continue;
        memset(&signature, 0, sizeof(signature));
        if (!patchlib_method_get_signature(method, &signature)) continue;
        arg_count = tefstd_vector_size(&signature.arg_types);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[TERRAIN_METHOD] name=%s instance=%s returnType=%d argCount=%zu",
                       name, signature.is_instance ? "yes" : "no",
                       (int)signature.return_type, arg_count);
        if (patchlib_method_signature_free) {
            (void)patchlib_method_signature_free(&signature);
        }
        ++logged;
    }
    tefstd_vector_destroy(&methods);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[TERRAIN_METHOD_SCAN] logged=%u limit=64",
                   (unsigned)logged);
}

static bool or_boundary_method_name(const char *name) {
    static const char *const names[] = {
        "CheckDead", "PreKill", "StrikeNPC", "HitEffect", "OnKill",
        "Death", "Kill", "NPCLoot", "DropItems", "DropCoins"
    };
    size_t i;
    if (!name) return false;
    for (i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0) return true;
    }
    return false;
}

static patch_type_t or_signature_arg_type(
    const patch_method_signature_t *signature, size_t index);

static void or_scan_boundary_methods(OR_Runtime *runtime) {
    tefstd_vector_t methods = {0};
    size_t i;
    uint32_t logged = 0u;
    if (!runtime || !runtime->npc_type || !patchlib_type_get_methods ||
        !patchlib_method_get_name || !patchlib_method_get_signature ||
        !tefstd_vector_init || !tefstd_vector_size || !tefstd_vector_at ||
        !tefstd_vector_destroy) return;
    if (!tefstd_vector_init(&methods, sizeof(patch_handle_t)) ||
        !patchlib_type_get_methods(runtime->npc_type, true, &methods)) {
        tefstd_vector_destroy(&methods);
        return;
    }
    for (i = 0u; i < tefstd_vector_size(&methods) && logged < 32u; ++i) {
        patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
        patch_handle_t method = entry ? *entry : PATCH_NULL;
        const char *name = method ? patchlib_method_get_name(method) : NULL;
        patch_method_signature_t signature;
        size_t arg_count;
        size_t arg_index;
        if (!or_boundary_method_name(name)) continue;
        memset(&signature, 0, sizeof(signature));
        if (!patchlib_method_get_signature(method, &signature)) continue;
        arg_count = tefstd_vector_size(&signature.arg_types);
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[BOUNDARY_METHOD] name=%s instance=%s returnType=%d argCount=%zu",
                       name, signature.is_instance ? "yes" : "no",
                       (int)signature.return_type, arg_count);
        for (arg_index = 0u; arg_index < arg_count && arg_index < 8u; ++arg_index) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[BOUNDARY_ARG] name=%s index=%zu type=%d",
                           name, arg_index,
                           (int)or_signature_arg_type(&signature, arg_index));
        }
        if (patchlib_method_signature_free) {
            (void)patchlib_method_signature_free(&signature);
        }
        ++logged;
    }
    tefstd_vector_destroy(&methods);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[BOUNDARY_SCAN] logged=%u limit=32 deathHook=off lootHook=observer rewards=enabled",
                   (unsigned)logged);
}

static patch_type_t or_signature_arg_type(
    const patch_method_signature_t *signature, size_t index) {
    patch_type_t *entry;
    if (!signature || !tefstd_vector_size || !tefstd_vector_at ||
        index >= tefstd_vector_size(&signature->arg_types)) return PATCH_VOID;
    entry = (patch_type_t *)tefstd_vector_at(&signature->arg_types, index);
    return entry ? *entry : PATCH_VOID;
}

bool or_runtime_query_unity_version(char *out, size_t out_size) {
    patch_handle_t application_type = PATCH_NULL;
    patch_handle_t getter = PATCH_NULL;
    patch_handle_t managed_string = PATCH_NULL;
    patch_method_signature_t signature;
    char *version = NULL;
    bool ok = false;

    if (!out || out_size == 0u) return false;
    out[0] = '\0';
    if (!patchlib_type_get_type || !patchlib_type_get_method_by_param_count ||
        !patchlib_method_get_signature || !patchlib_method_invoke_args ||
        !patchlib_string_cstr) return false;

    application_type = patchlib_type_get_type("UnityEngine", "Application");
    if (!or_handle_is_valid(application_type)) goto cleanup;
    getter = patchlib_type_get_method_by_param_count(
        application_type, "get_unityVersion", 0);
    if (!or_handle_is_valid(getter)) goto cleanup;

    memset(&signature, 0, sizeof(signature));
    if (!patchlib_method_get_signature(getter, &signature)) goto cleanup;
    ok = !signature.is_instance &&
         tefstd_vector_size(&signature.arg_types) == 0u &&
         (signature.return_type == PATCH_OBJECT ||
          signature.return_type == PATCH_POINTER);
    if (patchlib_method_signature_free) {
        (void)patchlib_method_signature_free(&signature);
    }
    if (!ok) goto cleanup;
    ok = patchlib_method_invoke_args(getter, PATCH_NULL, &managed_string, NULL) &&
         or_handle_is_valid(managed_string);
    if (!ok) goto cleanup;
    version = patchlib_string_cstr(managed_string);
    if (!version || !*version) {
        ok = false;
        goto cleanup;
    }
    (void)snprintf(out, out_size, "%s", version);
    ok = out[0] != '\0';

cleanup:
    free(version);
    or_release_handle(getter);
    or_release_handle(application_type);
    return ok;
}

static void or_resolve_mouse_text_method(OR_Runtime *runtime,
                                         patch_handle_t main_type) {
    int parameter_count;
    size_t method_count = 0u;
    if (!runtime || !main_type || !patchlib_type_get_method_by_param_count ||
        !patchlib_method_get_signature || !tefstd_vector_size) return;
    runtime->main_mouse_text_signature_ready = false;
    runtime->main_mouse_text_method_count = 0u;
    for (parameter_count = 0; parameter_count <= 10; ++parameter_count) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            main_type, "MouseText", parameter_count);
        patch_method_signature_t signature;
        size_t count;
        bool exact = false;
        if (!or_handle_is_valid(method)) {
            or_release_handle(method);
            continue;
        }
        memset(&signature, 0, sizeof(signature));
        if (patchlib_method_get_signature(method, &signature)) {
            count = tefstd_vector_size(&signature.arg_types);
            /* The target mobile build exposes MouseText as an instance method
             * with 8/10 explicit parameters. The first parameter is the
             * managed display string, followed by the vanilla rarity index
             * and diff byte. Keep the remaining parameters opaque: they are
             * only forwarded by the original method and are never touched by
             * this prefix hook. */
            exact = signature.is_instance && signature.return_type == PATCH_VOID &&
                    (count == 8u || count == 10u) &&
                    (or_signature_arg_type(&signature, 0u) == PATCH_OBJECT ||
                     or_signature_arg_type(&signature, 0u) == PATCH_POINTER) &&
                    or_signature_arg_type(&signature, 1u) == PATCH_INT32 &&
                    or_signature_arg_type(&signature, 2u) == PATCH_UINT8;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[NAME_COLOR_PROBE] method=MouseText instance=%s "
                           "returnType=%d argCount=%zu status=%s",
                           signature.is_instance ? "yes" : "no",
                           (int)signature.return_type, count,
                           exact ? "verified" : "rejected");
            if (exact) {
                size_t arg_index;
                for (arg_index = 0u; arg_index < count; ++arg_index) {
                    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                                   "[NAME_COLOR_ARG] method=MouseText index=%zu type=%d",
                                   arg_index,
                                   (int)or_signature_arg_type(&signature, arg_index));
                }
            }
            if (patchlib_method_signature_free) {
                (void)patchlib_method_signature_free(&signature);
            }
        }
        if (exact && method_count < OR_MOUSE_TEXT_METHOD_LIMIT) {
            runtime->method_main_mouse_text[method_count++] = method;
            runtime->main_mouse_text_method_count = method_count;
            runtime->main_mouse_text_signature_ready = true;
            runtime->capabilities.name_color_hook_ready = true;
            method = PATCH_NULL;
        } else if (exact) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[NAME_COLOR_PROBE] method=MouseText status=extra_overload_skipped");
        }
        or_release_handle(method);
    }
    if (!runtime->main_mouse_text_signature_ready) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[NAME_COLOR_PROBE] method=MouseText status=safe_off "
                       "reason=exact_signature_not_found");
    }
}

static void or_resolve_visual_members(OR_Runtime *runtime,
                                      patch_handle_t main_type) {
    patch_handle_t property;
    patch_handle_t getter;
    patch_handle_t setter;
    patch_method_signature_t getter_sig;
    patch_method_signature_t setter_sig;
    bool getter_ok = false;
    bool setter_ok = false;
    static const int text_counts[] = {1, 2, 3, 4};
    size_t i;

    if (!runtime) return;
    or_resolve_mouse_text_method(runtime, main_type);
    runtime->field_color = or_resolve_marker_field(runtime->npc_type, "color");
    runtime->capabilities.color_marker_probe_ready = runtime->field_color != PATCH_NULL;
    runtime->capabilities.color_marker_ready = false;
    if (patchlib_type_get_type) {
        static const char *const color_namespaces[] = {
            "Microsoft.Xna.Framework",
            "Microsoft.Xna.Framework.Graphics",
            "Terraria"
        };
        size_t color_namespace_index;
        for (color_namespace_index = 0u;
             color_namespace_index < sizeof(color_namespaces) / sizeof(color_namespaces[0]);
             ++color_namespace_index) {
            patch_handle_t candidate = patchlib_type_get_type(
                color_namespaces[color_namespace_index], "Color");
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[COLOR_TYPE_RESOLVE] namespace=%s status=%s",
                           color_namespaces[color_namespace_index],
                           or_handle_is_valid(candidate) ? "available" : "unavailable");
            if (or_handle_is_valid(candidate)) {
                runtime->color_type = candidate;
                break;
            }
            or_release_handle(candidate);
        }
    }

    if (runtime->field_color && patchlib_field_get_type && patchlib_field_get_size) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[COLOR_ABI_GATE] fieldType=%d fieldSize=%zu colorType=%s "
                       "write=pending reason=verify_color_layout_and_value_api",
                       (int)patchlib_field_get_type(runtime->field_color),
                       patchlib_field_get_size(runtime->field_color),
                       runtime->color_type ? "resolved" : "unresolved");
    }

    /* PatchLib reports the mobile Color value field as PATCH_POINTER/8 bytes.
     * Do not treat that metadata alone as a native pointer.  Enable the value
     * API only after the resolved Color type exposes the four byte components
     * with their exact instance-field ABI. This remains a value-API write. */
    if (runtime->field_color && runtime->color_type &&
        patchlib_type_get_field && patchlib_field_is_instance &&
        patchlib_field_is_static && patchlib_field_get_type &&
        patchlib_field_get_size && patchlib_field_set_value &&
        patchlib_field_get_value) {
        static const char *const components[] = {"R", "G", "B", "A"};
        bool layout_ok = true;
        for (i = 0u; i < sizeof(components) / sizeof(components[0]); ++i) {
            patch_handle_t component = patchlib_type_get_field(
                runtime->color_type, components[i]);
            bool component_ok = or_handle_is_valid(component) &&
                                patchlib_field_is_instance(component) &&
                                !patchlib_field_is_static(component) &&
                                patchlib_field_get_type(component) == PATCH_UINT8 &&
                                patchlib_field_get_size(component) == 1u;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[COLOR_LAYOUT] component=%s type=%d size=%zu instance=%s "
                           "status=%s",
                           components[i],
                           component_ok ? (int)patchlib_field_get_type(component)
                                         : (int)PATCH_VOID,
                           component_ok ? patchlib_field_get_size(component) : 0u,
                           component_ok ? "yes" : "no",
                           component_ok ? "verified" : "unavailable");
            if (!component_ok) layout_ok = false;
            or_release_handle(component);
        }
        runtime->capabilities.color_value_layout_ready = layout_ok &&
            patchlib_field_get_size(runtime->field_color) == 8u;
        runtime->capabilities.color_marker_ready = runtime->capabilities.color_value_layout_ready;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[COLOR_ABI_GATE] layout=%s valueApi=%s write=%s",
                       layout_ok ? "verified" : "unverified",
                       runtime->capabilities.color_value_layout_ready
                           ? "available" : "unavailable",
                       runtime->capabilities.color_marker_ready ? "enabled" : "disabled");
    }

    /* NewDust is the smallest widely available vanilla particle entry point.
     * Keep it completely gated: the Vector2 position must be represented by
     * the observed eight-byte PatchLib slot and the kernel must expose the
     * struct-by-value FFI bridge. NPC body-color writes remain disabled. */
    {
        static const patch_type_t dust_args[] = {
            PATCH_POINTER, PATCH_INT32, PATCH_INT32, PATCH_INT32,
            PATCH_FLOAT, PATCH_FLOAT, PATCH_INT32, PATCH_POINTER, PATCH_FLOAT
        };
        patch_handle_t dust_type = PATCH_NULL;
        patch_handle_t method = PATCH_NULL;
        runtime->capabilities.dust_new_dust_ready = false;
#if defined(__ANDROID__)
        runtime->capabilities.dust_value_invoke_ready =
            patchlib_method_invoke_value_args != NULL;
#else
        runtime->capabilities.dust_value_invoke_ready = false;
#endif
        if (patchlib_type_get_type && patchlib_type_get_method_by_param_count &&
            patchlib_method_get_signature) {
            dust_type = patchlib_type_get_type("Terraria", "Dust");
            if (or_handle_is_valid(dust_type)) {
                method = patchlib_type_get_method_by_param_count(
                    dust_type, "NewDust", 9u);
                if (or_runtime_signature_matches(method, false, PATCH_INT32,
                                                 dust_args, 9u)) {
                    runtime->method_dust_new_dust = method;
                    runtime->capabilities.dust_new_dust_ready =
                        runtime->field_position_probe != PATCH_NULL &&
                        runtime->position_vector2_metadata_ready &&
                        runtime->capabilities.dust_value_invoke_ready;
                    method = PATCH_NULL;
                }
            }
        }
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[DUST_ABI] type=%s method=NewDust signature=%s "
                       "gate=%s invoke=%s structValueApi=%s color=tier_particle_value",
                       or_handle_is_valid(dust_type) ? "resolved" : "unavailable",
                       runtime->method_dust_new_dust
                           ? "static return=int32 args=pointer,int32,int32,int32,float,float,int32,pointer,float"
                           : "unavailable",
                       runtime->capabilities.dust_new_dust_ready ? "verified" : "safe_off",
                       runtime->capabilities.dust_new_dust_ready ? "enabled_once" : "disabled",
                       runtime->capabilities.dust_value_invoke_ready ? "available" : "missing");
        or_release_handle(method);
        or_release_handle(dust_type);
    }

    /* HitEffect remains ABI-probed for diagnostics, but is not exposed as a
     * particle emitter because the target build produced no visible effect. */
    {
        static const patch_type_t hit_effect_args[] = {
            PATCH_INT32, PATCH_DOUBLE
        };
        patch_handle_t method = PATCH_NULL;
        runtime->capabilities.visual_hit_effect_ready = false;
        if (patchlib_type_get_method_by_param_count &&
            patchlib_method_get_signature) {
            method = patchlib_type_get_method_by_param_count(
                runtime->npc_type, "HitEffect", 2);
            if (or_runtime_signature_matches(method, true, PATCH_VOID,
                                              hit_effect_args, 2u)) {
                runtime->method_visual_hit_effect = method;
                runtime->capabilities.visual_hit_effect_ready = false;
                method = PATCH_NULL;
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[VISUAL_EFFECT_ABI] method=HitEffect "
                               "instance=yes returnType=void args=int32,double "
                               "status=verified invoke=disabled reason=not_particle_emitter");
            } else {
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[VISUAL_EFFECT_ABI] method=HitEffect "
                               "status=unavailable invoke=disabled");
            }
        }
        or_release_handle(method);
    }

    if (patchlib_type_get_property && patchlib_property_get_get_method &&
        patchlib_property_get_set_method && patchlib_method_get_signature &&
        tefstd_vector_size && tefstd_vector_at) {
        property = patchlib_type_get_property(runtime->npc_type, "GivenName");
        getter = or_handle_is_valid(property)
            ? patchlib_property_get_get_method(property) : PATCH_NULL;
        setter = or_handle_is_valid(property)
            ? patchlib_property_get_set_method(property) : PATCH_NULL;
        memset(&getter_sig, 0, sizeof(getter_sig));
        memset(&setter_sig, 0, sizeof(setter_sig));
        getter_ok = or_handle_is_valid(getter) &&
                    patchlib_method_get_signature(getter, &getter_sig) &&
                    getter_sig.is_instance && getter_sig.return_type == PATCH_OBJECT &&
                    tefstd_vector_size(&getter_sig.arg_types) == 0u;
        setter_ok = or_handle_is_valid(setter) &&
                    patchlib_method_get_signature(setter, &setter_sig) &&
                    setter_sig.is_instance && setter_sig.return_type == PATCH_VOID &&
                    tefstd_vector_size(&setter_sig.arg_types) == 1u &&
                    (or_signature_arg_type(&setter_sig, 0u) == PATCH_OBJECT ||
                     or_signature_arg_type(&setter_sig, 0u) == PATCH_POINTER);
        if (patchlib_method_signature_free) {
            if (getter_sig.method) (void)patchlib_method_signature_free(&getter_sig);
            if (setter_sig.method) (void)patchlib_method_signature_free(&setter_sig);
        }
        if (getter_ok && setter_ok) {
            runtime->property_given_name = property;
            runtime->method_given_name_get = getter;
            runtime->method_given_name_set = setter;
            runtime->capabilities.given_name_property_ready = true;
        } else {
            or_release_handle(property);
            or_release_handle(getter);
            or_release_handle(setter);
        }
    }

    /* Keep a verified field fallback.  Some mobile builds expose a working
     * GivenName property setter but the renderer reads the backing field
     * directly for hostile NPCs. */
    if (patchlib_type_get_field && patchlib_field_is_instance &&
        patchlib_field_is_static && patchlib_field_get_type &&
        patchlib_field_get_size) {
        patch_handle_t field = patchlib_type_get_field(runtime->npc_type, "GivenName");
        bool field_ok = or_handle_is_valid(field) &&
                        patchlib_field_is_instance(field) &&
                        !patchlib_field_is_static(field) &&
                        (patchlib_field_get_type(field) == PATCH_OBJECT ||
                         patchlib_field_get_type(field) == PATCH_POINTER) &&
                        patchlib_field_get_size(field) == 8u;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[NAME_BACKING_FIELD] field=GivenName type=%d size=%zu status=%s",
                       field_ok ? (int)patchlib_field_get_type(field) : (int)PATCH_VOID,
                       field_ok ? patchlib_field_get_size(field) : 0u,
                       field_ok ? "verified" : "unavailable");
        if (field_ok) runtime->field_given_name = field;
        else or_release_handle(field);
    }

    /* Hostile NPCs normally leave GivenName empty. Resolve a read-only
     * localized display-name property as the source for the original name;
     * each candidate is accepted only with the exact instance/object/zero-arg
     * getter ABI. FullName is preferred, TypeName is an automatic fallback. */
    if (patchlib_type_get_property && patchlib_property_get_get_method &&
        patchlib_method_get_signature && tefstd_vector_size && tefstd_vector_at) {
        /* Install the same three getter hooks as EliteMonsters. The renderer
         * may use any one of these paths depending on the Android build. */
        static const char *const display_names[] = {
            "FullName", "TypeName", "GivenOrTypeName"
        };
        for (i = 0; i < sizeof(display_names) / sizeof(display_names[0]); ++i) {
            property = patchlib_type_get_property(runtime->npc_type,
                                                  display_names[i]);
            getter = or_handle_is_valid(property)
                ? patchlib_property_get_get_method(property) : PATCH_NULL;
            memset(&getter_sig, 0, sizeof(getter_sig));
            getter_ok = or_handle_is_valid(getter) &&
                        patchlib_method_get_signature(getter, &getter_sig) &&
                        getter_sig.is_instance && getter_sig.return_type == PATCH_OBJECT &&
                        tefstd_vector_size(&getter_sig.arg_types) == 0u;
            if (patchlib_method_signature_free && getter_sig.method) {
                (void)patchlib_method_signature_free(&getter_sig);
            }
            if (getter_ok) {
                if (!runtime->method_display_name_get) {
                    runtime->property_display_name = property;
                    runtime->method_display_name_get = getter;
                } else if (!runtime->method_display_name_get_alt) {
                    runtime->property_display_name_alt = property;
                    runtime->method_display_name_get_alt = getter;
                } else if (!runtime->method_display_name_get_third) {
                    runtime->property_display_name_third = property;
                    runtime->method_display_name_get_third = getter;
                } else {
                    or_release_handle(property);
                    or_release_handle(getter);
                }
                if (runtime->method_display_name_get == getter ||
                    runtime->method_display_name_get_alt == getter ||
                    runtime->method_display_name_get_third == getter) {
                    property = PATCH_NULL;
                    getter = PATCH_NULL;
                }
            }
            or_release_handle(property);
            or_release_handle(getter);
        }
    }

    /* Some Android metadata exports expose the property getter only as a
     * method and do not return a Property object. Resolve those exact method
     * names as a second path, matching EliteMonsters 1.3.2. */
    if (patchlib_type_get_method_by_param_count &&
        patchlib_method_get_signature && tefstd_vector_size) {
        static const char *const display_getters[] = {
            "get_FullName", "get_TypeName", "get_GivenOrTypeName"
        };
        for (i = 0; i < sizeof(display_getters) / sizeof(display_getters[0]); ++i) {
            patch_handle_t method = patchlib_type_get_method_by_param_count(
                runtime->npc_type, display_getters[i], 0);
            patch_method_signature_t signature;
            bool exact = false;
            if (!or_handle_is_valid(method)) {
                or_release_handle(method);
                continue;
            }
            memset(&signature, 0, sizeof(signature));
            if (patchlib_method_get_signature(method, &signature)) {
                exact = signature.is_instance && signature.return_type == PATCH_OBJECT &&
                        tefstd_vector_size(&signature.arg_types) == 0u;
                if (patchlib_method_signature_free) {
                    (void)patchlib_method_signature_free(&signature);
                }
            }
            if (!exact || method == runtime->method_display_name_get ||
                method == runtime->method_display_name_get_alt ||
                method == runtime->method_display_name_get_third) {
                or_release_handle(method);
                continue;
            }
            if (!runtime->method_display_name_get) {
                runtime->method_display_name_get = method;
                method = PATCH_NULL;
            } else if (!runtime->method_display_name_get_alt) {
                runtime->method_display_name_get_alt = method;
                method = PATCH_NULL;
            } else if (!runtime->method_display_name_get_third) {
                runtime->method_display_name_get_third = method;
                method = PATCH_NULL;
            }
            or_release_handle(method);
        }
    }

    if (!main_type || !patchlib_type_get_method_by_param_count ||
        !patchlib_method_get_signature) return;
    for (i = 0; i < sizeof(text_counts) / sizeof(text_counts[0]); ++i) {
        patch_handle_t method = patchlib_type_get_method_by_param_count(
            main_type, "NewText", text_counts[i]);
        patch_method_signature_t signature;
        bool supported = false;
        if (!or_handle_is_valid(method)) {
            or_release_handle(method);
            continue;
        }
        memset(&signature, 0, sizeof(signature));
        if (patchlib_method_get_signature(method, &signature)) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[NOTICE_API_SIG] name=NewText instance=%s returnType=%d argCount=%zu",
                           signature.is_instance ? "yes" : "no",
                           (int)signature.return_type,
                           tefstd_vector_size(&signature.arg_types));
            for (size_t arg_index = 0u;
                 arg_index < tefstd_vector_size(&signature.arg_types) && arg_index < 8u;
                 ++arg_index) {
                OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                               "[NOTICE_API_ARG] name=NewText index=%zu type=%d",
                               arg_index, (int)or_signature_arg_type(&signature, arg_index));
            }
            bool first_is_text = or_signature_arg_type(&signature, 0u) == PATCH_OBJECT ||
                                 or_signature_arg_type(&signature, 0u) == PATCH_POINTER;
            if (!signature.is_instance && signature.return_type == PATCH_VOID &&
                tefstd_vector_size(&signature.arg_types) == (size_t)text_counts[i] &&
                first_is_text) {
                if (text_counts[i] == 1) {
                    supported = true;
                } else if (text_counts[i] == 3 &&
                           or_signature_arg_type(&signature, 1u) == PATCH_POINTER &&
                           or_signature_arg_type(&signature, 2u) == PATCH_BOOL) {
                    /* The 1.4.5.8.5 mobile build exposes
                     * NewText(object, Color*, bool). */
                    supported = true;
                    runtime->main_new_text_color_type = PATCH_POINTER;
                } else {
                    bool byte_color = or_signature_arg_type(&signature, 1u) == PATCH_UINT8 &&
                                       or_signature_arg_type(&signature, 2u) == PATCH_UINT8 &&
                                       or_signature_arg_type(&signature, 3u) == PATCH_UINT8;
                    bool int_color = or_signature_arg_type(&signature, 1u) == PATCH_INT32 &&
                                     or_signature_arg_type(&signature, 2u) == PATCH_INT32 &&
                                     or_signature_arg_type(&signature, 3u) == PATCH_INT32;
                    supported = byte_color || int_color;
                    runtime->main_new_text_color_type = byte_color ? PATCH_UINT8 : PATCH_INT32;
                }
            }
            if (supported) {
                runtime->main_new_text_second_type =
                    or_signature_arg_type(&signature, 1u);
            }
            if (patchlib_method_signature_free) {
                (void)patchlib_method_signature_free(&signature);
            }
        }
        if (supported) {
            runtime->method_main_new_text = method;
            runtime->main_new_text_arg_count = text_counts[i];
            runtime->capabilities.new_text_ready = true;
            return;
        }
        or_release_handle(method);
    }
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
    /* The target mobile build exposes a hidden runtime pointer as the second
     * SetDefaults parameter. This is taken from the runtime signature probe,
     * not inferred from the managed source declaration. */
    static const patch_type_t expected_args[] = {PATCH_INT32, PATCH_POINTER};
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
        if (!runtime->setdefaults_probe_seen) {
            size_t observed_count = tefstd_vector_size
                ? tefstd_vector_size(&signature.arg_types) : 0u;
            size_t observed_limit = observed_count < 4u ? observed_count : 4u;
            size_t observed_index;
            runtime->setdefaults_probe_seen = true;
            runtime->setdefaults_probe_param_count = (int)observed_count;
            for (observed_index = 0; observed_index < observed_limit;
                 ++observed_index) {
                runtime->setdefaults_probe_arg_types[observed_index] =
                    or_signature_arg_type(&signature, observed_index);
            }
        }
        /* Parameter count is only a prefilter; accept the callback only after
         * the exact instance/void/int32/pointer signature is verified. */
        if (args_count == 2 &&
            or_runtime_signature_matches(method, true, PATCH_VOID,
                                         expected_args, 2u) &&
            patchlib_method_get_name &&
            patchlib_method_get_name(method) &&
            strcmp(patchlib_method_get_name(method), "SetDefaults") == 0 &&
            runtime->method_setdefaults_count < OR_SETDEFAULTS_METHOD_LIMIT) {
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
    if (!runtime) return;
    method = patchlib_type_get_method_by_param_count(runtime->npc_type, "NPCLoot", 0);
    if (or_method_is_instance_void_zero(method)) {
        runtime->method_npcloot = method;
    } else {
        or_release_handle(method);
    }
}

static void or_resolve_strike_method(OR_Runtime *runtime) {
    static const patch_type_t expected_args[] = {
        PATCH_INT32, PATCH_FLOAT, PATCH_INT32, PATCH_BOOL, PATCH_BOOL, PATCH_INT32
    };
    patch_handle_t method;
    if (!runtime || !patchlib_type_get_method_by_param_count) return;
    method = patchlib_type_get_method_by_param_count(runtime->npc_type,
                                                     "StrikeNPC", 6);
    if (or_runtime_signature_matches(method, true, PATCH_INT32,
                                     expected_args, 6u)) {
        runtime->method_strike_npc = method;
    } else {
        or_release_handle(method);
    }
}

static void or_resolve_ai_factories(OR_Runtime *runtime) {
    static const patch_type_t npc_args4[] = {
        PATCH_INT32, PATCH_INT32, PATCH_INT32, PATCH_INT32
    };
    static const patch_type_t npc_args10[] = {
        PATCH_INT32, PATCH_INT32, PATCH_INT32, PATCH_INT32,
        PATCH_FLOAT, PATCH_FLOAT, PATCH_FLOAT, PATCH_FLOAT,
        PATCH_FLOAT, PATCH_FLOAT
    };
    static const patch_type_t projectile_args[] = {
        PATCH_FLOAT, PATCH_FLOAT, PATCH_FLOAT, PATCH_FLOAT,
        PATCH_INT32, PATCH_INT32, PATCH_FLOAT, PATCH_INT32,
        PATCH_FLOAT, PATCH_FLOAT
    };
    patch_handle_t method = PATCH_NULL;
    patch_handle_t projectile_type = PATCH_NULL;
    size_t i;

    if (!runtime || !patchlib_type_get_method_by_param_count) return;

    /* Terraria exposes both the compact and extended NPC.NewNPC overloads.
     * Resolve only exact static signatures; the returned handle is later
     * used by the host-authoritative summon action. */
    for (i = 0u; i < 2u && !runtime->npc_new_npc_signature_ready; ++i) {
        int arg_count = i == 0u ? 4 : 10;
        const patch_type_t *expected = i == 0u ? npc_args4 : npc_args10;
        method = patchlib_type_get_method_by_param_count
            ? patchlib_type_get_method_by_param_count(
                runtime->npc_type, "NewNPC", (size_t)arg_count)
            : PATCH_NULL;
        if (or_runtime_signature_matches(method, false, PATCH_INT32,
                                         expected, (size_t)arg_count)) {
            runtime->method_npc_new_npc = method;
            runtime->npc_new_npc_arg_count = arg_count;
            runtime->npc_new_npc_signature_ready = true;
            runtime->capabilities.npc_spawn_factory_ready = true;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[AI_FACTORY] NPC.NewNPC ready=yes args=%d "
                           "authority=host-or-singleplayer",
                           arg_count);
            break;
        }
        or_release_handle(method);
        method = PATCH_NULL;
    }

    /* Some mobile metadata builds do not expose overloads through
     * get_method_by_param_count even though the methods are present. Scan the
     * complete method table before declaring the action unavailable. */
    if (!runtime->npc_new_npc_signature_ready && patchlib_type_get_methods &&
        patchlib_method_get_name && patchlib_method_get_signature &&
        tefstd_vector_init && tefstd_vector_size && tefstd_vector_at &&
        tefstd_vector_destroy) {
        tefstd_vector_t methods = {0};
        if (tefstd_vector_init(&methods, sizeof(patch_handle_t)) &&
            patchlib_type_get_methods(runtime->npc_type, true, &methods)) {
            for (i = 0u; i < tefstd_vector_size(&methods); ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
                patch_handle_t candidate = entry ? *entry : PATCH_NULL;
                const char *name = candidate && patchlib_method_get_name
                    ? patchlib_method_get_name(candidate) : NULL;
                if (!name || strcmp(name, "NewNPC") != 0) continue;
                if (or_runtime_signature_matches(candidate, false, PATCH_INT32,
                                                 npc_args4, 4u)) {
                    runtime->method_npc_new_npc = candidate;
                    runtime->npc_new_npc_arg_count = 4;
                    runtime->npc_new_npc_signature_ready = true;
                    runtime->capabilities.npc_spawn_factory_ready = true;
                    break;
                }
                if (or_runtime_signature_matches(candidate, false, PATCH_INT32,
                                                 npc_args10, 10u)) {
                    runtime->method_npc_new_npc = candidate;
                    runtime->npc_new_npc_arg_count = 10;
                    runtime->npc_new_npc_signature_ready = true;
                    runtime->capabilities.npc_spawn_factory_ready = true;
                    break;
                }
            }
        }
        tefstd_vector_destroy(&methods);
        if (runtime->npc_new_npc_signature_ready) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[AI_FACTORY] NPC.NewNPC ready=yes source=method_scan args=%d "
                           "authority=host-or-singleplayer",
                           runtime->npc_new_npc_arg_count);
        }
    }

    projectile_type = patchlib_type_get_type
        ? patchlib_type_get_type("Terraria", "Projectile") : PATCH_NULL;
    if (projectile_type && patchlib_type_get_method_by_param_count) {
        method = patchlib_type_get_method_by_param_count(
            projectile_type, "NewProjectile", 10u);
        if (or_runtime_signature_matches(method, false, PATCH_INT32,
                                         projectile_args, 10u)) {
            runtime->method_projectile_new_projectile = method;
            runtime->projectile_new_projectile_signature_ready = true;
            runtime->capabilities.projectile_factory_ready = true;
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[AI_FACTORY] Projectile.NewProjectile ready=yes "
                           "args=10 authority=host-or-singleplayer");
        } else {
            or_release_handle(method);
        }
    }
    if (projectile_type && !runtime->projectile_new_projectile_signature_ready &&
        patchlib_type_get_methods && patchlib_method_get_name &&
        patchlib_method_get_signature && tefstd_vector_init &&
        tefstd_vector_size && tefstd_vector_at && tefstd_vector_destroy) {
        tefstd_vector_t methods = {0};
        if (tefstd_vector_init(&methods, sizeof(patch_handle_t)) &&
            patchlib_type_get_methods(projectile_type, true, &methods)) {
            for (i = 0u; i < tefstd_vector_size(&methods); ++i) {
                patch_handle_t *entry = (patch_handle_t *)tefstd_vector_at(&methods, i);
                patch_handle_t candidate = entry ? *entry : PATCH_NULL;
                const char *name = candidate && patchlib_method_get_name
                    ? patchlib_method_get_name(candidate) : NULL;
                if (!name || strcmp(name, "NewProjectile") != 0) continue;
                if (or_runtime_signature_matches(candidate, false, PATCH_INT32,
                                                 projectile_args, 10u)) {
                    runtime->method_projectile_new_projectile = candidate;
                    runtime->projectile_new_projectile_signature_ready = true;
                    runtime->capabilities.projectile_factory_ready = true;
                    break;
                }
            }
        }
        tefstd_vector_destroy(&methods);
        if (runtime->projectile_new_projectile_signature_ready) {
            OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                           "[AI_FACTORY] Projectile.NewProjectile ready=yes "
                           "source=method_scan args=10 authority=host-or-singleplayer");
        }
    }
    if (!runtime->npc_new_npc_signature_ready) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[AI_FACTORY] NPC.NewNPC ready=no reason=exact_signature_not_found");
    }
    if (!runtime->projectile_new_projectile_signature_ready) {
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[AI_FACTORY] Projectile.NewProjectile ready=no reason=exact_signature_not_found");
    }
    or_release_handle(projectile_type);
}

bool or_runtime_probe(OR_Runtime *runtime) {
    static const char *const game_mode_names[] = {"GameMode", "gameMode"};
    static const char *const hard_mode_names[] = {"hardMode", "HardMode"};
    static const char *const update_count_names[] = {"GameUpdateCount", "gameUpdateCount"};
    static const char *const day_time_names[] = {"dayTime"};
    static const char *const time_names[] = {"time"};
    static const char *const blood_moon_names[] = {"bloodMoon"};
    static const char *const raining_names[] = {"raining"};
    static const char *const sandstorm_names[] = {"sandStorm", "sandstorm", "sandStormActive"};
    static const char *const eclipse_names[] = {"eclipse"};
    static const char *const pumpkin_moon_names[] = {"pumpkinMoon"};
    static const char *const snow_moon_names[] = {"snowMoon"};
    static const char *const slime_rain_names[] = {"slimeRain"};
    static const char *const wind_strength_names[] = {"windSpeedCurrent", "windSpeed", "wind"};
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
    or_probe_item_new_item(runtime);
    or_probe_item_ids(runtime);
    or_probe_item_id_sets();
    or_runtime_probe_main_item_array(runtime);
    or_scan_main_item_members();
    or_scan_main_item_methods();
    or_scan_item_surface(runtime);
    or_probe_item_method_signatures(runtime);

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
    runtime->field_dont_take_damage = or_resolve_field(
        runtime->npc_type, "dontTakeDamage", true, PATCH_BOOL, sizeof(bool));
    /* Position is intentionally metadata-only in this version. Its native
     * representation must be confirmed before any byte or pointer read. */
    runtime->field_position_probe = or_probe_field(runtime->npc_type, "position");
    or_scan_position_accessors(runtime);
    runtime->field_velocity_probe = or_resolve_field(runtime->npc_type, "velocity",
                                                     true, PATCH_POINTER, 8u);
    or_probe_position_vector2_type(runtime);
    or_probe_velocity_member(runtime->npc_type);
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[AI_DASH_GATE] metadataOnly=yes velocityWrite=disabled "
                   "reason=pointer8_representation_unverified");

    /* active and value are optional compatibility/reward fields. The core
     * elite stat overlay only needs the vanilla combat fields below. */
    fields_ok = runtime->field_type && runtime->field_life_max && runtime->field_life;
    runtime->capabilities.stats_fields_resolved = fields_ok;

    main_type = patchlib_type_get_type("Terraria", "Main");
    if (or_handle_is_valid(main_type)) {
        /* Do not pass an unresolved type into optional capability probing. */
        or_resolve_visual_members(runtime, main_type);
        or_scan_world_members(main_type);
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
        runtime->main_day_time = or_resolve_field_any(
            main_type, day_time_names, sizeof(day_time_names) / sizeof(day_time_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_time = or_resolve_field_any(
            main_type, time_names, sizeof(time_names) / sizeof(time_names[0]),
            false, PATCH_DOUBLE, sizeof(double));
        runtime->main_blood_moon = or_resolve_field_any(
            main_type, blood_moon_names, sizeof(blood_moon_names) / sizeof(blood_moon_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_raining = or_resolve_field_any(
            main_type, raining_names, sizeof(raining_names) / sizeof(raining_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_sandstorm = or_resolve_field_any(
            main_type, sandstorm_names, sizeof(sandstorm_names) / sizeof(sandstorm_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_eclipse = or_resolve_field_any(
            main_type, eclipse_names, sizeof(eclipse_names) / sizeof(eclipse_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_pumpkin_moon = or_resolve_field_any(
            main_type, pumpkin_moon_names, sizeof(pumpkin_moon_names) / sizeof(pumpkin_moon_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_snow_moon = or_resolve_field_any(
            main_type, snow_moon_names, sizeof(snow_moon_names) / sizeof(snow_moon_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_slime_rain = or_resolve_field_any(
            main_type, slime_rain_names, sizeof(slime_rain_names) / sizeof(slime_rain_names[0]),
            false, PATCH_BOOL, sizeof(bool));
        runtime->main_wind_strength = or_resolve_field_any(
            main_type, wind_strength_names,
            sizeof(wind_strength_names) / sizeof(wind_strength_names[0]),
            false, PATCH_FLOAT, sizeof(float));
        runtime->main_world_surface = or_resolve_field(
            main_type, "worldSurface", false, PATCH_DOUBLE, sizeof(double));
        runtime->main_rock_layer = or_resolve_field(
            main_type, "rockLayer", false, PATCH_DOUBLE, sizeof(double));
        {
            static const char *const underworld_names[] = {
                "UnderworldLayer", "underworldLayer", "underWorldLayer",
                "underworldlayer"
            };
            runtime->main_underworld_layer = or_resolve_field_any(
                main_type, underworld_names,
                sizeof(underworld_names) / sizeof(underworld_names[0]),
                false, PATCH_INT32, sizeof(int32_t));
        }
        runtime->main_top_world = or_resolve_field(
            main_type, "topWorld", false, PATCH_FLOAT, sizeof(float));
        runtime->main_bottom_world = or_resolve_field(
            main_type, "bottomWorld", false, PATCH_FLOAT, sizeof(float));
        runtime->main_max_tiles_y = or_resolve_field(
            main_type, "maxTilesY", false, PATCH_INT32, sizeof(int32_t));
        or_probe_spawn_rate_members(runtime, main_type);
        if (patchlib_type_get_method_by_param_count) {
            patch_handle_t method = patchlib_type_get_method_by_param_count(
                main_type, "get_UnderworldLayer", 0u);
            if (or_runtime_signature_matches(method, false, PATCH_INT32, NULL, 0u)) {
                runtime->method_underworld_layer_get = method;
            } else {
                or_release_handle(method);
            }
        }
        or_probe_terrain_boundary_candidates(main_type);
        or_scan_terrain_methods(main_type);
        or_scan_biome_members();
        or_probe_player_source(main_type);
        {
            patch_handle_t player_type = patchlib_type_get_type("Terraria", "Player");
            if (or_handle_is_valid(player_type)) {
                or_resolve_biome_runtime(runtime, player_type);
            }
            or_release_handle(player_type);
        }
        runtime->capabilities.world_context_ready = runtime->main_day_time != PATCH_NULL &&
                                                     runtime->main_raining != PATCH_NULL &&
                                                     runtime->main_blood_moon != PATCH_NULL &&
                                                     runtime->main_eclipse != PATCH_NULL;
        /* Vector2's representation and both component fields are verified by
         * PatchLib. The adapter still validates every live read and falls
         * back to surface/forest if a particular instance cannot be read. */
        runtime->capabilities.terrain_probe_ready =
            runtime->field_position_probe != PATCH_NULL &&
            runtime->position_vector2_metadata_ready &&
            runtime->position_vector2_x_probe != PATCH_NULL &&
            runtime->position_vector2_y_probe != PATCH_NULL &&
            runtime->main_world_surface != PATCH_NULL &&
            runtime->main_top_world != PATCH_NULL &&
            runtime->main_bottom_world != PATCH_NULL;
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[TERRAIN_BOUNDARY] rockLayer=%s underworldLayer=%s "
                       "read=deferred",
                       runtime->main_rock_layer ? "verified" : "unavailable",
                       runtime->main_underworld_layer ? "verified" : "unavailable");
        OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                       "[TERRAIN_CAPABILITY] metadata=%s terrainRead=%s "
                       "reason=%s",
                       runtime->field_position_probe != PATCH_NULL &&
                       runtime->main_world_surface != PATCH_NULL &&
                       runtime->main_top_world != PATCH_NULL &&
                       runtime->main_bottom_world != PATCH_NULL ? "ready" : "partial",
                       runtime->capabilities.terrain_probe_ready ? "yes" : "no",
                       runtime->capabilities.terrain_probe_ready ?
                       "position_vector2_verified" : "position_representation_deferred");
        or_release_handle(main_type);
    } else {
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
    or_resolve_strike_method(runtime);
    or_resolve_ai_factories(runtime);
    runtime->capabilities.phase_immunity_ready = runtime->field_dont_take_damage != PATCH_NULL;
    OR_RUNTIME_LOG(MOD_LOG_LEVEL_INFO,
                   "[AI_CAPABILITY] velocity=%s projectile=%s summon=%s "
                   "phaseImmunity=%s",
                   runtime->field_velocity_probe ? "ready" : "unavailable",
                   runtime->capabilities.projectile_factory_ready ? "ready" : "unavailable",
                   runtime->capabilities.npc_spawn_factory_ready ? "ready" : "unavailable",
                   runtime->capabilities.phase_immunity_ready ? "ready" : "unavailable");
    or_scan_boundary_methods(runtime);
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
    or_uninstall_hook(&runtime->loot_observer_hook_id);
    or_uninstall_hook(&runtime->strike_hook_id);
    or_uninstall_hook(&runtime->display_name_hook_id);
    or_uninstall_hook(&runtime->display_name_hook_id_alt);
    or_uninstall_hook(&runtime->display_name_hook_id_third);
    for (i = 0; i < OR_MOUSE_TEXT_METHOD_LIMIT; ++i) {
        or_uninstall_hook(&runtime->mouse_text_hook_ids[i]);
    }

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
    or_release_handle(runtime->field_dont_take_damage);
    or_release_handle(runtime->field_position_probe);
    or_release_handle(runtime->field_velocity_probe);
    or_release_handle(runtime->position_vector2_type_probe);
    or_release_handle(runtime->position_vector2_x_probe);
    or_release_handle(runtime->position_vector2_y_probe);
    or_release_handle(runtime->field_color);
    or_release_handle(runtime->color_type);
    or_release_handle(runtime->method_visual_hit_effect);
    or_release_handle(runtime->method_dust_new_dust);
    or_release_handle(runtime->property_given_name);
    or_release_handle(runtime->field_given_name);
    or_release_handle(runtime->method_given_name_get);
    or_release_handle(runtime->method_given_name_set);
    or_release_handle(runtime->property_display_name);
    or_release_handle(runtime->method_display_name_get);
    or_release_handle(runtime->property_display_name_alt);
    or_release_handle(runtime->method_display_name_get_alt);
    or_release_handle(runtime->property_display_name_third);
    or_release_handle(runtime->method_display_name_get_third);
    or_release_handle(runtime->main_game_mode);
    or_release_handle(runtime->main_zenith_world);
    or_release_handle(runtime->main_hard_mode);
    or_release_handle(runtime->main_net_mode);
    or_release_handle(runtime->main_world_id);
    or_release_handle(runtime->main_update_count);
    or_release_handle(runtime->main_day_time);
    or_release_handle(runtime->main_time);
    or_release_handle(runtime->main_blood_moon);
    or_release_handle(runtime->main_raining);
    or_release_handle(runtime->main_sandstorm);
    or_release_handle(runtime->main_eclipse);
    or_release_handle(runtime->main_pumpkin_moon);
    or_release_handle(runtime->main_snow_moon);
    or_release_handle(runtime->main_slime_rain);
    or_release_handle(runtime->main_wind_strength);
    or_release_handle(runtime->main_world_surface);
    or_release_handle(runtime->main_rock_layer);
    or_release_handle(runtime->main_underworld_layer);
    or_release_handle(runtime->main_top_world);
    or_release_handle(runtime->main_bottom_world);
    or_release_handle(runtime->main_spawn_rate);
    or_release_handle(runtime->main_max_spawns);
    or_release_handle(runtime->main_player_field_probe);
    or_release_handle(runtime->main_my_player_field_probe);
    or_release_handle(runtime->player_position_field_probe);
    or_release_handle(runtime->main_local_player_get);
    or_release_handle(runtime->player_zone_corrupt_get);
    or_release_handle(runtime->player_zone_crimson_get);
    or_release_handle(runtime->player_zone_hallow_get);
    or_release_handle(runtime->player_zone_jungle_get);
    or_release_handle(runtime->player_zone_snow_get);
    or_release_handle(runtime->player_zone_desert_get);
    or_release_handle(runtime->player_zone_beach_get);
    or_release_handle(runtime->player_zone_glowshroom_get);
    or_release_handle(runtime->player_shopping_forest_get);
    or_release_handle(runtime->player_shopping_any_biome_get);
    or_release_handle(runtime->method_underworld_layer_get);
    or_release_handle(runtime->npc_downed_mech);
    or_release_handle(runtime->npc_downed_plant);
    or_release_handle(runtime->npc_downed_golem);
    or_release_handle(runtime->npc_downed_moonlord);
    for (i = 0; i < runtime->method_setdefaults_count; ++i) {
        or_release_handle(runtime->method_setdefaults[i]);
    }
    or_release_handle(runtime->method_ai);
    or_release_handle(runtime->method_npcloot);
    or_release_handle(runtime->method_strike_npc);
    or_release_handle(runtime->method_npc_new_npc);
    or_release_handle(runtime->method_projectile_new_projectile);
    or_release_handle(runtime->method_main_new_text);
    for (i = 0; i < OR_MOUSE_TEXT_METHOD_LIMIT; ++i) {
        or_release_handle(runtime->method_main_mouse_text[i]);
    }
    or_release_handle(runtime->method_item_new_item);
    or_release_handle(runtime->method_item_new_item_extended);
    or_release_handle(runtime->method_item_id_from_net_id);
    or_release_handle(runtime->item_type);
    or_release_handle(runtime->item_field_type);
    or_release_handle(runtime->item_field_stack);
    or_release_handle(runtime->main_item_field);
    or_release_handle(runtime->npc_type);
    memset(runtime, 0, sizeof(*runtime));
    runtime->spawn_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->death_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->ai_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->loot_observer_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->strike_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->display_name_hook_id = PATCH_HOOK_INVALID_ID;
    runtime->display_name_hook_id_alt = PATCH_HOOK_INVALID_ID;
    runtime->display_name_hook_id_third = PATCH_HOOK_INVALID_ID;
    for (i = 0; i < OR_MOUSE_TEXT_METHOD_LIMIT; ++i) {
        runtime->mouse_text_hook_ids[i] = PATCH_HOOK_INVALID_ID;
    }
    for (i = 0; i < OR_SETDEFAULTS_METHOD_LIMIT; ++i) {
        runtime->setdefaults_hook_ids[i] = PATCH_HOOK_INVALID_ID;
    }
}
