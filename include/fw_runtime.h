#ifndef ORIGINREWRITE_FW_RUNTIME_H
#define ORIGINREWRITE_FW_RUNTIME_H

#include "fw.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/type.h"

typedef struct fw_runtime {
    bool patchlib_available;
    bool npc_type_resolved;
    bool stats_fields_resolved;
    bool main_fields_resolved;
    bool ai_known_dispatcher;

    patch_handle_t npc_type;

    /* Terraria.NPC instance fields */
    patch_handle_t field_active;
    patch_handle_t field_type;
    patch_handle_t field_life_max;
    patch_handle_t field_life;
    patch_handle_t field_damage;
    patch_handle_t field_defense;
    patch_handle_t field_knockback_resist;
    patch_handle_t field_scale;
    patch_handle_t field_width;
    patch_handle_t field_height;
    patch_handle_t field_value;
    patch_handle_t field_npc_slots;
    patch_handle_t field_friendly;
    patch_handle_t field_town_npc;
    patch_handle_t field_boss;

    /* Terraria.Main static fields */
    patch_handle_t main_game_mode;
    patch_handle_t main_zenith_world;
    patch_handle_t main_hard_mode;
    patch_handle_t main_net_mode;
    patch_handle_t main_world_id;
    patch_handle_t main_update_count;

    /* Terraria.NPC static progress fields */
    patch_handle_t npc_downed_mech;
    patch_handle_t npc_downed_plant;
    patch_handle_t npc_downed_golem;
    patch_handle_t npc_downed_moonlord;

    /* Method handles */
    patch_handle_t method_setdefaults[FW_SETDEFAULTS_LIMIT];
    size_t method_setdefaults_count;
    patch_handle_t method_ai;
    patch_handle_t method_npcloot;

    patch_hook_id_t setdefaults_hooks[FW_SETDEFAULTS_LIMIT];
    size_t setdefaults_hook_count;
    patch_hook_id_t ai_hook;
    patch_hook_id_t loot_hook;
} fw_runtime;

bool fw_runtime_probe(fw_runtime *runtime);
void fw_runtime_cleanup(fw_runtime *runtime);
bool fw_signature_matches(patch_handle_t method,
                          bool expected_instance,
                          patch_type_t expected_return,
                          const patch_type_t *expected_args,
                          size_t expected_arg_count);

#endif
