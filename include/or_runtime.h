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
    bool world_context_ready;
    bool terrain_probe_ready;
    bool color_marker_ready;
    bool color_marker_probe_ready;
    bool color_value_layout_ready;
    bool visual_hit_effect_ready;
    bool dust_new_dust_ready;
    bool dust_value_invoke_ready;
    bool given_name_property_ready;
    bool new_text_ready;
    bool name_color_hook_ready;
    bool gameplay_enabled;
} OR_RuntimeCapabilities;

typedef struct OR_Runtime {
    patch_handle_t npc_type;
    patch_handle_t item_type;
    patch_handle_t item_field_type;
    patch_handle_t item_field_stack;
    patch_handle_t method_item_new_item;
    patch_handle_t method_item_new_item_extended;
    patch_handle_t method_item_id_from_net_id;
    patch_handle_t main_item_field;
    bool item_new_item_signature_ready;
    bool item_id_type_resolved;
    bool item_id_static_int_ready;
    bool item_id_lookup_signature_ready;
    uint32_t item_id_static_int_count;
    uint32_t item_id_method_count;
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
    patch_handle_t field_position_probe;
    patch_handle_t field_velocity_probe;
    patch_handle_t position_vector2_type_probe;
    patch_handle_t position_vector2_x_probe;
    patch_handle_t position_vector2_y_probe;
    bool position_vector2_metadata_ready;
    bool position_value_probe_ready;
    patch_handle_t field_color;
    patch_handle_t color_type;
    patch_handle_t method_visual_hit_effect;
    patch_handle_t method_dust_new_dust;
    patch_handle_t property_given_name;
    patch_handle_t field_given_name;
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
    patch_handle_t main_day_time;
    patch_handle_t main_blood_moon;
    patch_handle_t main_raining;
    patch_handle_t main_eclipse;
    patch_handle_t main_pumpkin_moon;
    patch_handle_t main_snow_moon;
    patch_handle_t main_slime_rain;
    patch_handle_t main_world_surface;
    patch_handle_t main_rock_layer;
    patch_handle_t main_underworld_layer;
    patch_handle_t main_top_world;
    patch_handle_t main_bottom_world;
    patch_handle_t main_max_tiles_y;
    patch_handle_t main_spawn_rate;
    patch_handle_t main_max_spawns;
    patch_handle_t main_player_field_probe;
    patch_handle_t main_my_player_field_probe;
    patch_handle_t player_position_field_probe;
    patch_handle_t main_local_player_get;
    patch_handle_t player_zone_corrupt_get;
    patch_handle_t player_zone_crimson_get;
    patch_handle_t player_zone_hallow_get;
    patch_handle_t player_zone_jungle_get;
    patch_handle_t player_zone_snow_get;
    patch_handle_t player_zone_desert_get;
    patch_handle_t player_zone_beach_get;
    patch_handle_t player_zone_glowshroom_get;
    patch_handle_t player_shopping_forest_get;
    patch_handle_t player_shopping_any_biome_get;
    patch_handle_t method_underworld_layer_get;
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
    patch_handle_t method_strike_npc;
    bool ai_known_dispatcher;
    patch_handle_t method_main_new_text;
    int main_new_text_arg_count;
    patch_type_t main_new_text_color_type;
    patch_type_t main_new_text_second_type;
    patch_handle_t method_main_mouse_text;
    bool main_mouse_text_signature_ready;

    patch_hook_id_t setdefaults_hook_ids[OR_SETDEFAULTS_METHOD_LIMIT];
    size_t setdefaults_hook_count;
    patch_hook_id_t spawn_hook_id;
    patch_hook_id_t death_hook_id;
    patch_hook_id_t ai_hook_id;
    patch_hook_id_t loot_hook_id;
    patch_hook_id_t loot_observer_hook_id;
    patch_hook_id_t strike_hook_id;
    patch_hook_id_t mouse_text_hook_id;
    OR_RuntimeCapabilities capabilities;
} OR_Runtime;

void or_runtime_init(OR_Runtime *runtime);
/* Caller must initialize a fresh runtime, or cleanup it before probing again. */
bool or_runtime_probe(OR_Runtime *runtime);
void or_runtime_probe_main_item_array(OR_Runtime *runtime);
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
/* Queries UnityEngine.Application.unityVersion through PatchLib. */
bool or_runtime_query_unity_version(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_RUNTIME_H */
