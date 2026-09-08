#include "or_death_probe.h"

#include "or_adapter.h"
#include "or_log.h"
#include "tefkernel/patchlib/field.h"

#include <stdint.h>

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

static uint32_t g_observation_count;
static patch_handle_t g_field_type;
static patch_handle_t g_field_life;
static patch_handle_t g_field_active;

static bool field_ready(patch_handle_t field, patch_type_t type, size_t size) {
    return field && patchlib_field_get_type && patchlib_field_get_size &&
           patchlib_field_get_value && patchlib_field_get_type(field) == type &&
           patchlib_field_get_size(field) == size;
}

void or_death_probe_configure(patch_handle_t field_type,
                              patch_handle_t field_life,
                              patch_handle_t field_active) {
    g_field_type = field_type;
    g_field_life = field_life;
    g_field_active = field_active;
}

void or_death_probe_postfix(patch_handle_t instance, void **args,
                            void *result,
                            const patch_method_signature_t *sig_info) {
    (void)instance;
    int32_t type = 0;
    int32_t life = 0;
    bool active = false;
    int32_t strike_result = 0;
    bool state_ok;
    (void)sig_info;

    if (!instance || args == NULL) return;
    state_ok = field_ready(g_field_type, PATCH_INT32, sizeof(type)) &&
               field_ready(g_field_life, PATCH_INT32, sizeof(life)) &&
               field_ready(g_field_active, PATCH_BOOL, sizeof(active));
    if (!state_ok) return;

    patchlib_field_get_value(g_field_type, instance, &type);
    patchlib_field_get_value(g_field_life, instance, &life);
    patchlib_field_get_value(g_field_active, instance, &active);
    if (result) strike_result = *(const int32_t *)result;

    if ((life <= 0 || !active) && g_observation_count < 64u) {
        ++g_observation_count;
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[DEATH_STATE_OBSERVE] type=%d life=%d active=%s result=%d sample=%u observationOnly=yes",
               (int)type, (int)life, active ? "true" : "false",
               (int)strike_result, (unsigned)g_observation_count);
        or_adapter_observe_death_state(instance, type, life, active,
                                       strike_result);
    }
}

void or_death_probe_loot_postfix(patch_handle_t instance, void **args,
                                 void *result,
                                 const patch_method_signature_t *sig_info) {
    (void)args;
    (void)result;
    (void)sig_info;
    or_adapter_observe_loot_boundary(instance);
}
