#include "tef_private.h"
#include "app/or_app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
void or_log(const char *tag,const char *format,...) { (void)tag;(void)format; }
typedef struct { const char *name; bool instance; patch_type_t ret; unsigned n; patch_type_t args[5]; } Method;
static Method methods[]={
    {"get_worldID",false,PATCH_INT32,0,{0}}, {"get_gameMenu",false,PATCH_BOOL,0,{0}},
    {"get_GameUpdateCount",false,PATCH_UINT32,0,{0}}, {"get_LocalPlayer",false,PATCH_OBJECT,0,{0}},
    {"UpdateAudio",true,PATCH_VOID,0,{0}}, {"AI",true,PATCH_VOID,0,{0}},
    {"OnSpawn",true,PATCH_VOID,1,{PATCH_OBJECT}}, {"NPCLoot",true,PATCH_VOID,0,{0}},
    {"NewText",false,PATCH_VOID,5,{PATCH_OBJECT,PATCH_UINT8,PATCH_UINT8,PATCH_UINT8,PATCH_BOOL}}};
typedef struct { const char *name; patch_type_t kind; size_t size,offset; bool instance; } Field;
typedef struct { int32_t slot,type,life,max; bool active,boss; } NativeNpc;
static Field fields[]={
    {"whoAmI",PATCH_INT32,4,offsetof(NativeNpc,slot),true}, {"type",PATCH_INT32,4,offsetof(NativeNpc,type),true},
    {"life",PATCH_INT32,4,offsetof(NativeNpc,life),true}, {"lifeMax",PATCH_INT32,4,offsetof(NativeNpc,max),true},
    {"active",PATCH_BOOL,sizeof(bool),offsetof(NativeNpc,active),true},
    {"boss",PATCH_BOOL,sizeof(bool),offsetof(NativeNpc,boss),true},
    {"dead",PATCH_BOOL,sizeof(bool),0,true}, {"netMode",PATCH_INT32,4,0,false},
    {"npc",PATCH_OBJECT,sizeof(void *),0,false}};
static NativeNpc *native_slots[OR_SLOT_COUNT];
static void *native_array=native_slots;
static int classes[4],world_id=10,netmode,installs,removes,texts;
static bool menu=false,dead=false,fail_hook=false;
static uint32_t tick=1;
static postfix_callback_t hooks[4];
static bool valid(patch_handle_t h) { return h!=NULL; }
static patch_handle_t type(const char *ns,const char *name) {
    assert(!strcmp(ns,"Terraria"));
    if (!strcmp(name,"NPC")) return &classes[0];
    if (!strcmp(name,"Main")) return &classes[1];
    if (!strcmp(name,"Entity")) return &classes[3];
    return &classes[2];
}
static patch_handle_t find_method(patch_handle_t c,const char *name,int n) {
    (void)c;
    for (unsigned i=0;i<sizeof(methods)/sizeof(methods[0]);++i)
        if (!strcmp(name,methods[i].name) && n==(int)methods[i].n) return &methods[i];
    return NULL;
}
static patch_handle_t find_field(patch_handle_t c,const char *name) {
    if (!strcmp(name,"whoAmI") && c!=&classes[3]) return NULL;
    for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i) if (!strcmp(name,fields[i].name)) return &fields[i];
    return NULL;
}
static bool instance_field(patch_handle_t f) { return ((Field *)f)->instance; }
static bool static_field(patch_handle_t f) { return !((Field *)f)->instance; }
static patch_type_t field_kind(patch_handle_t f) { return ((Field *)f)->kind; }
static size_t field_size(patch_handle_t f) { return ((Field *)f)->size; }
static void *field_pointer(patch_handle_t f,void *instance) {
    Field *v=f;
    if (!v->instance) return !strcmp(v->name,"npc") ? &native_array : (void *)&netmode;
    return instance ? (char *)instance+v->offset : NULL;
}
static bool signature(patch_handle_t method,patch_method_signature_t *sig) {
    Method *m=method; memset(sig,0,sizeof(*sig));
    sig->method=m; sig->is_instance=m->instance; sig->return_type=m->ret;
    sig->arg_types=(tefstd_vector_t){m->args,m->n,m->n,sizeof(patch_type_t)}; return true;
}
static bool free_signature(patch_method_signature_t *s) { memset(s,0,sizeof(*s)); return true; }
static size_t vector_size(const tefstd_vector_t *v) { return v->size; }
static void *vector_at(const tefstd_vector_t *v,size_t i) { return i<v->size ? (char *)v->data+i*v->elem_size : NULL; }
static const char *method_name(patch_handle_t m) { return ((Method *)m)->name; }
static patch_handle_t create_text(const char *s) { return (void *)s; }
static size_t array_length(patch_handle_t array) { assert(array==native_slots); return OR_SLOT_COUNT; }
static bool array_at(patch_handle_t array,size_t index,void *out) {
    assert(array==native_slots); if (index>=OR_SLOT_COUNT) return false;
    memcpy(out,&native_slots[index],sizeof(void *)); return true;
}
static bool invoke(patch_handle_t method,patch_handle_t instance,void *ret,void **args) {
    (void)instance; Method *m=method;
    if (!strcmp(m->name,"NewText")) {
        assert(args && *(void **)args[0]); assert(*(bool *)args[4]); ++texts; return true;
    }
    /* Simulate libffi's native-word scalar write, including bool results. */
    uint64_t value=0;
    if (!strcmp(m->name,"get_worldID")) value=(uint32_t)world_id;
    if (!strcmp(m->name,"get_gameMenu")) value=menu;
    if (!strcmp(m->name,"get_GameUpdateCount")) value=tick;
    if (!strcmp(m->name,"get_LocalPlayer")) value=(uintptr_t)&dead;
    assert(ret); memcpy(ret,&value,sizeof(value)); return true;
}
static patch_hook_id_t install(patch_handle_t m,prefix_callback_t pre,postfix_callback_t post) {
    (void)m; assert(!pre && post);
    if (fail_hook && installs==1) return PATCH_HOOK_INVALID_ID;
    hooks[installs]=post; return 100+installs++;
}
static bool remove_hook(patch_hook_id_t id) { assert(id>=100 && id<104); ++removes; return true; }
static void bind(void) {
    patchlib_is_valid=valid; patchlib_type_get_type=type;
    patchlib_type_get_method_by_param_count=find_method; patchlib_type_get_field=find_field;
    patchlib_field_is_instance=instance_field; patchlib_field_is_static=static_field;
    patchlib_field_get_type=field_kind; patchlib_field_get_size=field_size; patchlib_field_get_pointer=field_pointer;
    patchlib_method_get_signature=signature; patchlib_method_signature_free=free_signature;
    patchlib_method_get_name=method_name; patchlib_method_invoke_args=invoke;
    patchlib_string_create=create_text; tefstd_vector_size=vector_size; tefstd_vector_at=vector_at;
    patchlib_install_prepost_hook=install; patchlib_uninstall_hook=remove_hook;
    patchlib_array_length=array_length; patchlib_array_at=array_at;
}
int main(void) {
    OR_App app; or_app_init(&app,NULL,or_config_default(),(OR_TextPort){NULL,or_tef_text});
    OR_ObserverPort port={&app,or_app_world,or_app_npc};
    assert(!or_tef_start(port)); assert(installs==0 && removes==0);
    bind(); methods[5].ret=PATCH_INT32; assert(!or_tef_start(port)); assert(installs==0);
    methods[5].ret=PATCH_VOID; fail_hook=true;
    assert(!or_tef_start(port)); assert(installs==1 && removes==1);
    fail_hook=false; installs=removes=0;
    assert(or_tef_start(port)); assert(installs==4);
    hooks[0](NULL,NULL,NULL,NULL); assert(app.lifecycle.session==1 && texts==1);
    NativeNpc npc={5,50,100,100,true,true},before=npc;
    hooks[2](&npc,NULL,NULL,NULL); hooks[1](&npc,NULL,NULL,NULL);
    assert(app.lifecycle.live==1 && !memcmp(&npc,&before,sizeof(npc)));
    OR_Key old=app.lifecycle.entities[5].key;
    hooks[1](&npc,NULL,NULL,NULL); assert(or_key_equal(old,app.lifecycle.entities[5].key));
    hooks[2](&npc,NULL,NULL,NULL); hooks[1](&npc,NULL,NULL,NULL);
    assert(!or_key_equal(old,app.lifecycle.entities[5].key));
    native_slots[5]=&npc; npc.active=false; tick=121; hooks[0](NULL,NULL,NULL,NULL);
    assert(app.lifecycle.entities[5].dead);
    tick=242; hooks[0](NULL,NULL,NULL,NULL); assert(app.lifecycle.live==0);
    menu=true; hooks[0](NULL,NULL,NULL,NULL); assert(app.lifecycle.live==0);
    menu=false; world_id=11; ++tick; hooks[0](NULL,NULL,NULL,NULL); assert(app.lifecycle.session==2);
    assert(or_tef_stop()); assert(removes==4); assert(or_tef_stop()); assert(removes==4);
    puts("PASS: missing SDK/signature gates, partial hook rollback, padded FFI reads, spawn identity, read-only callbacks, idempotent stop");
}
