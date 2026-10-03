#include "tef_private.h"
#include <string.h>
/* FFI scalar results use a native-word return cell, not a bool/int stack cell. */
typedef union { uint64_t word; int32_t i32; uint32_t u32; bool boolean; void *object; } ReturnCell;
static bool get(patch_handle_t method,ReturnCell *out) {
    memset(out,0,sizeof(*out));
    return method && patchlib_is_valid(method) && patchlib_method_invoke_args(method,NULL,out,NULL);
}
bool or_tef_read_world(OR_Tef *t) {
    ReturnCell id,menu,tick;
    if (!get(t->world_id,&id)||!get(t->menu,&menu)||!get(t->tick,&tick)) return false;
    int32_t net=0; void *net_pointer=patchlib_field_get_pointer(t->netmode,NULL);
    if (!net_pointer) return false;
    memcpy(&net,net_pointer,sizeof(net));
    if (tick.u32<t->low_tick && t->low_tick-tick.u32>UINT32_MAX/2) t->tick_epoch+=UINT64_C(1)<<32;
    else if (tick.u32<t->low_tick) t->tick_epoch=0;
    t->low_tick=tick.u32;
    t->world=(OR_WorldView){true,menu.boolean,net!=0,false,false,id.i32,t->tick_epoch+tick.u32};
    if (!menu.boolean && t->local_player && t->player_dead) {
        ReturnCell player;
        if (get(t->local_player,&player) && player.object) {
            void *p=patchlib_field_get_pointer(t->player_dead,player.object);
            if (p) { memcpy(&t->world.player_dead,p,sizeof(bool)); t->world.player_known=true; }
        }
    }
    return true;
}
bool or_tef_read_npc(OR_Tef *t,patch_handle_t instance,OR_NpcView *v) {
    if (!instance || !t->world.valid) return false;
    int32_t slot=0; memset(v,0,sizeof(*v));
    void *dst[]={&slot,&v->type,&v->life,&v->life_max,&v->active,&v->boss};
    for (unsigned i=0;i<6;++i) {
        void *p=patchlib_field_get_pointer(t->npc_fields[i],instance);
        if (!p) return false;
        memcpy(dst[i],p,i<4?sizeof(int32_t):sizeof(bool));
    }
    if (slot<0 || slot>=OR_SLOT_COUNT || v->type<=0) return false;
    v->slot=(uint16_t)slot; v->tick=t->world.tick; return true;
}
void or_tef_scan(OR_Tef *t) {
    if (!t->world.valid || t->world.menu || t->world.multiplayer || !t->npc_array ||
        !patchlib_array_length || !patchlib_array_at || !t->observer.npc) return;
    if (t->world.tick>=t->last_scan && t->world.tick-t->last_scan<120) return;
    t->last_scan=t->world.tick;
    void *p=patchlib_field_get_pointer(t->npc_array,NULL); patch_handle_t array=NULL;
    if (!p) return;
    memcpy(&array,p,sizeof(array)); if (!array) return;
    size_t count=patchlib_array_length(array); if (count>OR_SLOT_COUNT) count=OR_SLOT_COUNT;
    for (size_t i=0;i<count;++i) {
        patch_handle_t instance=NULL; OR_NpcView v;
        if (patchlib_array_at(array,i,&instance) && instance && or_tef_read_npc(t,instance,&v))
            t->observer.npc(t->observer.ctx,&v,OR_SIGNAL_SCAN);
    }
}
