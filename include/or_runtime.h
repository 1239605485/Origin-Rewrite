#ifndef ORIGINREWRITE_RUNTIME_H
#define ORIGINREWRITE_RUNTIME_H

#include "or_types.h"

#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/property.h"

#define OR_SETDEFAULTS_METHOD_LIMIT 8u

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
    bool ai_dispatcher_known_target;
    bool color_marker_ready;
    bool color_marker_probe_ready;
    bool given_name_property_ready;
    bool new_text_ready;
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
    patch_handle_t field_npc_slots;
    patch_handle_t field_width;
    patch_handle_t field_height;
    patch_handle_t field_type;
    patch_handle_t field_friendly;
    patch_handle_t field_town_npc;
    patch_handle_t field_boss;
    patch_handle_t field_ai_style;
    patch_handle_t field_color;
    patch_handle_t property_given_name;
    patch_handle_t method_given_name_get;
    patch_handle_t method_given_name_set;
    patch_handle_t property_display_name;
    patch_handle_t method_display_name_get;

    patch_handle_t main_game_mode;
    patch_handle_t main_zenith_world;
    patch_handle_t main_hard_mode;
    patch_handle_t main_net_mode;
    patch_handle_t main_world_id;
    patch_handle_t main_update_count;
    patch_handle_t npc_downed_mech;
    patch_handle_t npc_downed_plant;
    patch_handle_t npc_downed_golem;
    patch_handle_t npc_downed_moonlord;

    patch_handle_t method_setdefaults[OR_SETDEFAULTS_METHOD_LIMIT];
    size_t method_setdefaults_count;
    bool setdefaults_probe_seen;
    int setdefaults_probe_param_count;
    patch_type_t setdefaults_probe_arg_types[4];
    patch_handle_t method_ai;
    patch_handle_t method_npcloot;
    bool ai_known_dispatcher;
    patch_handle_t method_main_new_text;
    int main_new_text_arg_count;
    patch_type_t main_new_text_color_type;

    patch_hook_id_t setdefaults_hook_ids[OR_SETDEFAULTS_METHOD_LIMIT];
    size_t setdefaults_hook_count;
    patch_hook_id_t spawn_hook_id;
    patch_hook_id_t death_hook_id;
    patch_hook_id_t ai_hook_id;
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
