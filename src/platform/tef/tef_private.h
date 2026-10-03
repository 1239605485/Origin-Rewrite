#ifndef OR_TEF_PRIVATE_H
#define OR_TEF_PRIVATE_H
#include "ports/or_tef.h"
#include "tefkernel/patchlib/type.h"
#include "tefkernel/patchlib/method.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/struct/string.h"
#include "tefkernel/patchlib/struct/array.h"
#if !defined(__ANDROID__)
extern void *(*patchlib_field_get_pointer)(patch_handle_t,void *);
#endif
typedef struct {
    OR_ObserverPort observer;
    patch_handle_t npc_type,main_type,player_type,entity_type;
    patch_handle_t npc_fields[6],player_dead,npc_array;
    patch_handle_t world_id,menu,tick,netmode,local_player,newtext;
    patch_handle_t methods[4]; patch_hook_id_t hooks[4];
    OR_WorldView world; uint32_t low_tick; uint64_t tick_epoch,last_scan;
    bool running,text_ready;
    unsigned read_failures;
} OR_Tef;
extern OR_Tef or_tef;
bool or_tef_probe(OR_Tef *t);
bool or_tef_signature(patch_handle_t m,bool instance,patch_type_t result,const patch_type_t *args,size_t n);
bool or_tef_read_world(OR_Tef *t);
bool or_tef_read_npc(OR_Tef *t,patch_handle_t instance,OR_NpcView *v);
void or_tef_scan(OR_Tef *t);
#endif
