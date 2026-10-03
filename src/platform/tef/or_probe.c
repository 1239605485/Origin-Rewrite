#include "tef_private.h"
#include "or_log.h"
#include <string.h>
bool or_tef_signature(patch_handle_t m,bool instance,patch_type_t result,const patch_type_t *args,size_t n) {
    if (!m || !patchlib_is_valid(m)) return false;
    patch_method_signature_t sig={0};
    if (!patchlib_method_get_signature(m,&sig)) return false;
    bool ok=sig.is_instance==instance && sig.return_type==result && tefstd_vector_size(&sig.arg_types)==n;
    for (size_t i=0;ok && i<n;++i) {
        const patch_type_t *p=tefstd_vector_at(&sig.arg_types,i); ok=p && *p==args[i];
    }
    or_log("METHOD","name=%s instance=%d return=%d args=%zu exact=%d",patchlib_method_get_name(m),sig.is_instance,sig.return_type,tefstd_vector_size(&sig.arg_types),ok);
    patchlib_method_signature_free(&sig); return ok;
}
static patch_handle_t method(patch_handle_t type,const char *name,bool instance,patch_type_t result,const patch_type_t *args,size_t n) {
    patch_handle_t m=patchlib_type_get_method_by_param_count(type,name,(int)n);
    return or_tef_signature(m,instance,result,args,n) ? m : NULL;
}
static patch_handle_t field(patch_handle_t type,const char *name,patch_type_t kind,size_t size) {
    patch_handle_t f=patchlib_type_get_field(type,name);
    bool ok=f && patchlib_is_valid(f) && patchlib_field_is_instance(f) && patchlib_field_get_type(f)==kind && patchlib_field_get_size(f)==size;
    or_log("FIELD","name=%s kind=%d bytes=%zu available=%d",name,(int)kind,size,ok);
    return ok?f:NULL;
}
static patch_handle_t static_field(patch_handle_t type,const char *name,patch_type_t kind,size_t size) {
    patch_handle_t f=patchlib_type_get_field(type,name);
    bool ok=f && patchlib_is_valid(f) && patchlib_field_is_static(f) &&
            patchlib_field_get_type(f)==kind && patchlib_field_get_size(f)==size;
    or_log("FIELD","name=%s static=yes available=%d",name,ok);
    return ok?f:NULL;
}
bool or_tef_probe(OR_Tef *t) {
    if (!patchlib_type_get_type || !patchlib_type_get_field || !patchlib_type_get_method_by_param_count ||
        !patchlib_is_valid || !patchlib_field_is_instance || !patchlib_field_is_static || !patchlib_field_get_type || !patchlib_field_get_size ||
        !patchlib_field_get_pointer || !patchlib_method_get_signature || !patchlib_method_signature_free ||
        !patchlib_method_get_name || !patchlib_method_invoke_args || !tefstd_vector_size || !tefstd_vector_at ||
        !patchlib_install_prepost_hook || !patchlib_uninstall_hook) {
        or_log("CAPABILITY","required SDK symbols missing; hooks=off"); return false;
    }
    t->npc_type=patchlib_type_get_type("Terraria","NPC");
    t->main_type=patchlib_type_get_type("Terraria","Main");
    t->player_type=patchlib_type_get_type("Terraria","Player");
    t->entity_type=patchlib_type_get_type("Terraria","Entity");
    if (!t->npc_type || !t->main_type || !t->entity_type) return false;
    const char *names[]={"whoAmI","type","life","lifeMax","active","boss"};
    for (unsigned i=0;i<6;++i) {
        t->npc_fields[i]=field(i==0?t->entity_type:t->npc_type,names[i],i<4?PATCH_INT32:PATCH_BOOL,i<4?sizeof(int32_t):sizeof(bool));
        if (!t->npc_fields[i]) return false;
    }
    t->world_id=method(t->main_type,"get_worldID",false,PATCH_INT32,NULL,0);
    t->menu=method(t->main_type,"get_gameMenu",false,PATCH_BOOL,NULL,0);
    t->tick=method(t->main_type,"get_GameUpdateCount",false,PATCH_UINT32,NULL,0);
    t->netmode=static_field(t->main_type,"netMode",PATCH_INT32,sizeof(int32_t));
    t->npc_array=static_field(t->main_type,"npc",PATCH_OBJECT,sizeof(patch_handle_t));
    t->local_player=method(t->main_type,"get_LocalPlayer",false,PATCH_OBJECT,NULL,0);
    if (t->player_type) t->player_dead=field(t->player_type,"dead",PATCH_BOOL,sizeof(bool));
    t->methods[0]=method(t->main_type,"UpdateAudio",true,PATCH_VOID,NULL,0);
    t->methods[1]=method(t->npc_type,"AI",true,PATCH_VOID,NULL,0);
    const patch_type_t source[]={PATCH_OBJECT};
    t->methods[2]=method(t->npc_type,"OnSpawn",true,PATCH_VOID,source,1);
    t->methods[3]=method(t->npc_type,"NPCLoot",true,PATCH_VOID,NULL,0);
    const patch_type_t text[]={PATCH_OBJECT,PATCH_UINT8,PATCH_UINT8,PATCH_UINT8,PATCH_BOOL};
    t->newtext=method(t->main_type,"NewText",false,PATCH_VOID,text,5);
    t->text_ready=t->newtext && patchlib_string_create;
    bool base=t->world_id && t->menu && t->tick && t->netmode && t->methods[0] && t->methods[1] && t->methods[2];
    or_log("CAPABILITY","readOnly=%d worldLoop=%d identitySpawn=%d text=%d deathBoundary=%d gameplayWrites=off",base,
           t->methods[0]!=NULL,t->methods[2]!=NULL,t->text_ready,t->methods[3]!=NULL);
    return base;
}
