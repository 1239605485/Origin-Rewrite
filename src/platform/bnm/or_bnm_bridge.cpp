#include "or_bnm_bridge.h"

#include "or_compat.h"
#include "or_log.h"

#include <BNM/Class.hpp>
#include <BNM/Field.hpp>
#include <BNM/Loading.hpp>
#include <BNM/MethodBase.hpp>
#include <BNM/Method.hpp>
#include <BNM/PropertyBase.hpp>
#include <BNM/Defaults.hpp>
#include <BNM/Utils.hpp>
#include <xdl.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

struct BnmState {
    OR_BnmCapabilities capabilities{};
    const char *status{"not-initialized"};
    void *il2cpp_handle{};

    BNM::Class npc_class{};
    BNM::Class main_class{};
    BNM::Class item_class{};

    BNM::Field<bool> active{};
    BNM::Field<int32_t> type{};
    BNM::Field<int32_t> life_max{};
    BNM::Field<int32_t> life{};
    BNM::Field<int32_t> damage{};
    BNM::Field<int32_t> defense{};
    BNM::Field<float> knockback_resist{};
    BNM::Field<float> scale{};
    BNM::Field<float> value{};
    BNM::Field<float> npc_slots{};
    BNM::Field<int32_t> width{};
    BNM::Field<int32_t> height{};
    BNM::Field<int32_t> ai_style{};
    BNM::Field<bool> friendly{};
    BNM::Field<bool> town_npc{};
    BNM::Field<bool> boss{};

    BNM::MethodBase set_defaults{};
    BNM::MethodBase ai{};
    BNM::MethodBase npc_loot{};
    BNM::Method<int32_t> new_item_basic9{};
    bool new_item_basic9_ready{};
};

BnmState g_bnm{};

void *find_il2cpp_method(const char *name, void *user_data) {
    return user_data && name ? xdl_sym(user_data, name, nullptr) : nullptr;
}

bool preflight_symbols(void *handle) {
    /* BNM 2.5.2 reads these APIs during SetupBNM/LoadDefaults. Requiring the
     * direct image API avoids its version-sensitive Image::GetTypes fallback. */
    static const char *const required[] = {
        "il2cpp_array_new_specific",
        "il2cpp_image_get_class",
        "il2cpp_get_corlib",
        "il2cpp_class_from_name",
        "il2cpp_assembly_get_image",
        "il2cpp_method_get_param_name",
        "il2cpp_class_from_il2cpp_type",
        "il2cpp_array_class_get",
        "il2cpp_type_get_object",
        "il2cpp_object_new",
        "il2cpp_value_box",
        "il2cpp_array_new",
        "il2cpp_field_static_get_value",
        "il2cpp_field_static_set_value",
        "il2cpp_string_new",
        "il2cpp_resolve_icall",
        "il2cpp_runtime_invoke",
        "il2cpp_domain_get",
        "il2cpp_thread_current",
        "il2cpp_thread_attach",
        "il2cpp_thread_detach",
        "il2cpp_domain_get_assemblies"
    };
    for (const char *name : required) {
        if (!xdl_sym(handle, name, nullptr)) {
            or_log_write(MOD_LOG_LEVEL_WARNING,
                         "[BNM_PREFLIGHT] missing=%s result=safe-off", name);
            return false;
        }
    }
    return true;
}

template <typename T>
BNM::Field<T> field(BNM::Class &owner, const char *name) {
    return BNM::Field<T>(owner.GetField(name));
}

template <typename T>
void log_field_preflight(const char *name, const BNM::Field<T> &value) {
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_FIELD_PREFLIGHT] field=%s valid=%s expectedSize=%zu "
                 "readOnly=yes",
                 name, value.IsValid() ? "yes" : "no", sizeof(T));
}

void log_method_preflight(const char *name, const BNM::MethodBase &value) {
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_METHOD_PREFLIGHT] method=%s valid=%s readOnly=yes",
                 name, value.IsValid() ? "yes" : "no");
}

void enumerate_npc_fields() {
    if (!g_bnm.npc_class.IsValid()) return;
    const auto fields = g_bnm.npc_class.GetFields(false);
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_FIELD_ENUM] class=Terraria.NPC count=%zu includeParent=no readOnly=yes",
                 fields.size());
    size_t index = 0u;
    for (const auto &entry : fields) {
        if (index >= 256u) {
            or_log_write(MOD_LOG_LEVEL_INFO, "[BNM_FIELD_ENUM] truncated=yes limit=256");
            break;
        }
        auto *info = entry.GetInfo();
        const char *name = info && info->name ? info->name : "<unnamed>";
        const int type_code = info && info->type
            ? static_cast<int>(info->type->type) : -1;
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_FIELD_ENUM_ITEM] index=%zu name=%s typeCode=%d offset=%zu readOnly=yes",
                     index, name, type_code,
                     info ? static_cast<size_t>(info->offset) : 0u);
        ++index;
    }
}

void enumerate_class_members(const char *label, BNM::Class &klass) {
    if (!klass.IsValid()) return;
    const auto fields = klass.GetFields(true);
    const auto methods = klass.GetMethods(true);
    const auto properties = klass.GetProperties(true);
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_MEMBER_ENUM] class=%s fields=%zu methods=%zu properties=%zu "
                 "includeParent=yes readOnly=yes",
                 label, fields.size(), methods.size(), properties.size());
    size_t index = 0u;
    for (const auto &entry : fields) {
        if (index++ >= 512u) break;
        auto *info = entry.GetInfo();
        const char *name = info && info->name ? info->name : "<unnamed>";
        const int type_code = info && info->type
            ? static_cast<int>(info->type->type) : -1;
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_MEMBER_FIELD] class=%s name=%s typeCode=%d offset=%zu "
                     "readOnly=yes",
                     label, name, type_code,
                     info ? static_cast<size_t>(info->offset) : 0u);
    }
    index = 0u;
    for (const auto &entry : methods) {
        if (index++ >= 512u) break;
        auto *info = entry.GetInfo();
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_MEMBER_METHOD] class=%s name=%s params=%u valid=%s "
                     "readOnly=yes",
                     label,
                     info && info->name ? info->name : "<unnamed>",
                     info ? static_cast<unsigned>(info->parameters_count) : 0u,
                     entry.IsValid() ? "yes" : "no");
        if (info && info->name &&
            (std::strcmp(info->name, "NewItem") == 0 ||
             std::strcmp(info->name, "RequestNewItem") == 0 ||
             std::strcmp(info->name, "NPCLoot") == 0)) {
            or_log_write(MOD_LOG_LEVEL_INFO,
                         "[BNM_REWARD_METHOD] class=%s name=%s params=%u "
                         "valid=%s invoke=disabled readOnly=yes",
                         label, info->name,
                         static_cast<unsigned>(info->parameters_count),
                         entry.IsValid() ? "yes" : "no");
            if (info->return_type) {
                or_log_write(MOD_LOG_LEVEL_INFO,
                             "[BNM_REWARD_RETURN] class=%s name=%s typeCode=%d "
                             "byref=%u valuetype=%u data=%p readOnly=yes",
                             label, info->name,
                             static_cast<int>(info->return_type->type),
                             static_cast<unsigned>(info->return_type->byref),
                             static_cast<unsigned>(info->return_type->valuetype),
                             info->return_type->data.dummy);
            }
            for (unsigned parameter_index = 0u;
                 parameter_index < static_cast<unsigned>(info->parameters_count) &&
                 parameter_index < 16u; ++parameter_index) {
                const auto *parameter_type = info->parameters
                    ? info->parameters[parameter_index] : nullptr;
                if (parameter_type) {
                    BNM::Class parameter_class(parameter_type);
                    const auto *parameter_class_raw = parameter_class.GetClass();
                    const std::string parameter_name = parameter_class.IsValid() &&
                        parameter_class_raw && parameter_class_raw->name
                        ? std::string(parameter_class_raw->namespaze
                                          ? parameter_class_raw->namespaze : "") +
                          "." + parameter_class_raw->name
                        : "<unresolved>";
                    or_log_write(MOD_LOG_LEVEL_INFO,
                                 "[BNM_REWARD_PARAM] class=%s name=%s index=%u "
                                 "typeCode=%d byref=%u valuetype=%u data=%p "
                                 "readOnly=yes",
                                 label, info->name, parameter_index,
                                 static_cast<int>(parameter_type->type),
                                 static_cast<unsigned>(parameter_type->byref),
                                 static_cast<unsigned>(parameter_type->valuetype),
                                 parameter_type->data.dummy);

                    or_log_write(MOD_LOG_LEVEL_INFO,
                                 "[BNM_REWARD_PARAM_LAYOUT] class=%s name=%s index=%u "
                                 "typeName=%s classValid=%s instanceSize=%u actualSize=%u "
                                 "readOnly=yes",
                                 label, info->name, parameter_index,
                                 parameter_name.c_str(), parameter_class.IsValid() ? "yes" : "no",
                                 parameter_class_raw ? static_cast<unsigned>(parameter_class_raw->instance_size) : 0u,
                                 parameter_class_raw ? static_cast<unsigned>(parameter_class_raw->actualSize) : 0u);
                } else {
                    or_log_write(MOD_LOG_LEVEL_INFO,
                                 "[BNM_REWARD_PARAM] class=%s name=%s index=%u "
                                 "type=unavailable readOnly=yes",
                                 label, info->name, parameter_index);
                }
            }
        }
    }
    index = 0u;
    for (const auto &entry : properties) {
        if (index++ >= 512u) break;
        auto *info = entry._data;
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_MEMBER_PROPERTY] class=%s name=%s valid=%s "
                     "readOnly=yes",
                     label,
                     info && info->name ? info->name : "<unnamed>",
                     entry.IsValid() ? "yes" : "no");
    }
}

void log_raw_class_layout(const char *label, BNM::Class &klass) {
    auto *raw = klass.GetClass();
    if (!raw) return;
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_RAW_CLASS] class=%s self=%p parent=%p fields=%p methods=%p "
                 "properties=%p fieldCount=%u methodCount=%u propertyCount=%u "
                 "instanceSize=%u actualSize=%u readOnly=yes",
                 label, (void *)raw, (void *)raw->parent, (void *)raw->fields,
                 (void *)raw->methods, (void *)raw->properties,
                 (unsigned)raw->field_count, (unsigned)raw->method_count,
                 (unsigned)raw->property_count, (unsigned)raw->instance_size,
                 (unsigned)raw->actualSize);
    for (unsigned depth = 0u; raw && depth < 16u; ++depth) {
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_RAW_HIERARCHY] class=%s depth=%u node=%p name=%s "
                     "namespace=%s parent=%p fields=%p fieldCount=%u "
                     "readOnly=yes",
                     label, depth, (void *)raw,
                     raw->name ? raw->name : "<null>",
                     raw->namespaze ? raw->namespaze : "<null>",
                     (void *)raw->parent, (void *)raw->fields,
                     (unsigned)raw->field_count);
        raw = raw->parent;
    }
    if (raw) {
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_RAW_HIERARCHY] class=%s truncated=yes limit=16 "
                     "readOnly=yes", label);
    }
}

void scan_raw_class_window(const char *label, BNM::Class &klass) {
    auto *raw = reinterpret_cast<const uint8_t *>(klass.GetClass());
    if (!raw) return;
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_RAW_SCAN] class=%s base=%p start=0x%zx end=0x%zx "
                 "stride=8 readOnly=yes",
                 label, (const void *)raw, sizeof(void *) * 20u,
                 sizeof(void *) * 20u + 0x160u);
    for (size_t offset = sizeof(void *) * 20u;
         offset <= sizeof(void *) * 20u + 0x160u; offset += 8u) {
        uintptr_t pointer_value = 0u;
        uint32_t value32 = 0u;
        uint16_t value16 = 0u;
        memcpy(&pointer_value, raw + offset, sizeof(pointer_value));
        memcpy(&value32, raw + offset, sizeof(value32));
        memcpy(&value16, raw + offset, sizeof(value16));
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_RAW_SCAN_ITEM] class=%s offset=0x%zx ptr=%p "
                     "u32=%u u16=%u readOnly=yes",
                     label, offset, (void *)pointer_value,
                     (unsigned)value32, (unsigned)value16);
    }
}

template <typename T>
bool read(const BNM::Field<T> &source, void *instance, T &out) {
    if (!instance || !source.IsValid()) return false;
    auto bound = source;
    bound.SetInstance(static_cast<BNM::IL2CPP::Il2CppObject *>(instance));
    auto *pointer = bound.GetPointer();
    if (!pointer) return false;
    out = *pointer;
    return true;
}

template <typename T>
bool write(const BNM::Field<T> &target, void *instance, T value) {
    if (!instance || !target.IsValid()) return false;
    auto bound = target;
    bound.SetInstance(static_cast<BNM::IL2CPP::Il2CppObject *>(instance));
    auto *pointer = bound.GetPointer();
    if (!pointer) return false;
    *pointer = value;
    return true;
}

int32_t clamp_i32(int64_t value) {
    if (value > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value < 0) return 0;
    return static_cast<int32_t>(value);
}

float clamp_f32(int64_t value) {
    constexpr auto max = static_cast<int64_t>(std::numeric_limits<int32_t>::max());
    if (value > max) value = max;
    if (value < 0) value = 0;
    return static_cast<float>(value);
}

void resolve_metadata() {
    g_bnm.npc_class = BNM::Class("Terraria", "NPC");
    g_bnm.main_class = BNM::Class("Terraria", "Main");
    g_bnm.item_class = BNM::Class("Terraria", "Item");
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_REWARD_CLASSES] npc=%s main=%s item=%s "
                 "scan=methods-fields-layout invoke=disabled readOnly=yes",
                 g_bnm.npc_class.IsValid() ? "yes" : "no",
                 g_bnm.main_class.IsValid() ? "yes" : "no",
                 g_bnm.item_class.IsValid() ? "yes" : "no");
    if (g_bnm.item_class.IsValid()) {
        const BNM::Class ownership_class("Terraria", "NewItemOwnership");
        const auto basic_new_item = g_bnm.item_class.GetMethod(
            "NewItem", {BNM::Defaults::Get<int>(), BNM::Defaults::Get<int>(),
                         BNM::Defaults::Get<int>(), BNM::Defaults::Get<int>(),
                         BNM::Defaults::Get<int>(), BNM::Defaults::Get<int>(),
                         BNM::Defaults::Get<bool>(), BNM::Defaults::Get<int>(),
                         ownership_class});
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_REWARD_TYPED_MATCH] overload=basic9 valid=%s "
                     "offset=0x%zx ownershipClass=%s invoke=disabled readOnly=yes",
                     basic_new_item.IsValid() ? "yes" : "no",
                     static_cast<size_t>(basic_new_item.GetOffset()),
                     ownership_class.IsValid() ? "yes" : "no");
        g_bnm.new_item_basic9 = basic_new_item;
        g_bnm.new_item_basic9_ready = basic_new_item.IsValid() &&
                                      ownership_class.IsValid();
        or_log_write(MOD_LOG_LEVEL_INFO,
                     "[BNM_REWARD_CALL_PLAN] overload=basic9 cached=%s "
                     "return=int32 static=%s args=9 abi=typed-call "
                     "invoke=disabled readOnly=yes",
                     g_bnm.new_item_basic9_ready ? "yes" : "no",
                     basic_new_item._isStatic ? "yes" : "no");
    }
    enumerate_class_members("Terraria.NPC", g_bnm.npc_class);
    enumerate_class_members("Terraria.Main", g_bnm.main_class);
    enumerate_class_members("Terraria.Item", g_bnm.item_class);
    log_raw_class_layout("Terraria.NPC", g_bnm.npc_class);
    log_raw_class_layout("Terraria.Main", g_bnm.main_class);
    log_raw_class_layout("Terraria.Item", g_bnm.item_class);
    scan_raw_class_window("Terraria.NPC", g_bnm.npc_class);
    scan_raw_class_window("Terraria.Main", g_bnm.main_class);
    scan_raw_class_window("Terraria.Item", g_bnm.item_class);
    g_bnm.capabilities.npc_metadata_ready = g_bnm.npc_class.IsValid();
    if (!g_bnm.capabilities.npc_metadata_ready) {
        g_bnm.status = "npc-class-unavailable";
        return;
    }

    g_bnm.active = field<bool>(g_bnm.npc_class, "active");
    g_bnm.type = field<int32_t>(g_bnm.npc_class, "type");
    g_bnm.life_max = field<int32_t>(g_bnm.npc_class, "lifeMax");
    g_bnm.life = field<int32_t>(g_bnm.npc_class, "life");
    g_bnm.damage = field<int32_t>(g_bnm.npc_class, "damage");
    g_bnm.defense = field<int32_t>(g_bnm.npc_class, "defense");
    g_bnm.knockback_resist = field<float>(g_bnm.npc_class, "knockBackResist");
    g_bnm.scale = field<float>(g_bnm.npc_class, "scale");
    g_bnm.value = field<float>(g_bnm.npc_class, "value");
    g_bnm.npc_slots = field<float>(g_bnm.npc_class, "npcSlots");
    g_bnm.width = field<int32_t>(g_bnm.npc_class, "width");
    g_bnm.height = field<int32_t>(g_bnm.npc_class, "height");
    g_bnm.ai_style = field<int32_t>(g_bnm.npc_class, "aiStyle");
    g_bnm.friendly = field<bool>(g_bnm.npc_class, "friendly");
    g_bnm.town_npc = field<bool>(g_bnm.npc_class, "townNPC");
    g_bnm.boss = field<bool>(g_bnm.npc_class, "boss");
    enumerate_npc_fields();

    log_field_preflight("active", g_bnm.active);
    log_field_preflight("type", g_bnm.type);
    log_field_preflight("lifeMax", g_bnm.life_max);
    log_field_preflight("life", g_bnm.life);
    log_field_preflight("damage", g_bnm.damage);
    log_field_preflight("defense", g_bnm.defense);
    log_field_preflight("knockBackResist", g_bnm.knockback_resist);
    log_field_preflight("scale", g_bnm.scale);
    log_field_preflight("value", g_bnm.value);
    log_field_preflight("npcSlots", g_bnm.npc_slots);
    log_field_preflight("width", g_bnm.width);
    log_field_preflight("height", g_bnm.height);
    log_field_preflight("aiStyle", g_bnm.ai_style);
    log_field_preflight("friendly", g_bnm.friendly);
    log_field_preflight("townNPC", g_bnm.town_npc);
    log_field_preflight("boss", g_bnm.boss);

    g_bnm.set_defaults = g_bnm.npc_class.GetMethod("SetDefaults", 2);
    g_bnm.ai = g_bnm.npc_class.GetMethod("AI", 0);
    g_bnm.npc_loot = g_bnm.npc_class.GetMethod("NPCLoot", 0);

    log_method_preflight("SetDefaults/2", g_bnm.set_defaults);
    log_method_preflight("AI/0", g_bnm.ai);
    log_method_preflight("NPCLoot/0", g_bnm.npc_loot);

    g_bnm.capabilities.stats_read_ready =
        g_bnm.type.IsValid() && g_bnm.life_max.IsValid() && g_bnm.life.IsValid();
    /* First boot with the Unity-2021.3 layout is read-only. PatchLib retains
     * the verified write path until BNM reads agree with it on-device. */
    g_bnm.capabilities.stats_write_ready = false;
    g_bnm.capabilities.lifecycle_methods_ready =
        g_bnm.set_defaults.IsValid() && g_bnm.ai.IsValid();
    g_bnm.capabilities.loot_method_ready = g_bnm.npc_loot.IsValid();
    g_bnm.status = g_bnm.capabilities.stats_read_ready
        ? "ready" : "required-npc-fields-unavailable";

    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_METADATA] npc=%s main=%s item=%s statsRead=%s statsWrite=disabled "
                 "setDefaults=%s ai=%s npcLoot=%s backend=BNM-2.5.2 unityAbi=2021.3",
                 g_bnm.npc_class.IsValid() ? "yes" : "no",
                 g_bnm.main_class.IsValid() ? "yes" : "no",
                 g_bnm.item_class.IsValid() ? "yes" : "no",
                 g_bnm.capabilities.stats_read_ready ? "ready" : "safe-off",
                 g_bnm.set_defaults.IsValid() ? "yes" : "no",
                 g_bnm.ai.IsValid() ? "yes" : "no",
                 g_bnm.npc_loot.IsValid() ? "yes" : "no");
}

void on_bnm_loaded() {
    g_bnm.capabilities.loaded = BNM::IsLoaded();
    if (!g_bnm.capabilities.loaded) {
        g_bnm.status = "bnm-load-failed";
        return;
    }
    resolve_metadata();
}

}  // namespace

extern "C" bool or_bnm_bridge_init(const char *unity_version) {
    g_bnm.capabilities.compiled = true;
    g_bnm.capabilities.unity_version_supported =
        or_compat_bnm_unity_supported(unity_version);
    if (!g_bnm.capabilities.unity_version_supported) {
        g_bnm.status = unity_version && *unity_version
            ? "unsupported-unity-family" : "unity-version-unavailable";
        or_log_write(MOD_LOG_LEVEL_WARNING,
                     "[BNM_GATE] unity=%s supportedUnity=2021.3/2022.2/2022.3 "
                     "result=safe-off",
                     unity_version && *unity_version ? unity_version : "unavailable");
        return false;
    }

    g_bnm.il2cpp_handle = xdl_open("libil2cpp.so", XDL_DEFAULT);
    if (!g_bnm.il2cpp_handle) {
        g_bnm.status = "libil2cpp-handle-unavailable";
        or_log_write(MOD_LOG_LEVEL_WARNING,
                     "[BNM_GATE] libil2cpp=xdl-open-failed result=safe-off");
        return false;
    }
    g_bnm.capabilities.il2cpp_symbols_ready = preflight_symbols(g_bnm.il2cpp_handle);
    if (!g_bnm.capabilities.il2cpp_symbols_ready) {
        g_bnm.status = "required-il2cpp-symbol-unavailable";
        return false;
    }

    BNM::Loading::SetMethodFinder(find_il2cpp_method, g_bnm.il2cpp_handle);
    BNM::Loading::AddOnLoadedEvent(on_bnm_loaded);
    BNM::Loading::TrySetupByUsersFinder();

    if (!g_bnm.capabilities.loaded || !g_bnm.capabilities.stats_read_ready) {
        or_log_write(MOD_LOG_LEVEL_WARNING,
                     "[BNM_GATE] unity=%s loaded=%s metadata=%s result=safe-off",
                     unity_version,
                     g_bnm.capabilities.loaded ? "yes" : "no",
                     g_bnm.capabilities.stats_read_ready ? "yes" : "no");
        return false;
    }
    or_log_write(MOD_LOG_LEVEL_INFO,
                 "[BNM_GATE] unity=%s loaded=yes metadata=yes "
                 "access=primary hooks=PatchLib result=enabled",
                 unity_version);
    return true;
}

extern "C" void or_bnm_bridge_shutdown(void) {
    /* BNM owns process-lifetime metadata pointers. The enclosing shared object
     * is unloaded as a unit by KernelLoader, so only the xDL bookkeeping
     * handle needs closing here. */
    BNM::Loading::ClearOnLoadedEvents();
    if (g_bnm.il2cpp_handle) {
        xdl_close(g_bnm.il2cpp_handle);
        g_bnm.il2cpp_handle = nullptr;
    }
}

extern "C" const OR_BnmCapabilities *or_bnm_bridge_capabilities(void) {
    return &g_bnm.capabilities;
}

extern "C" const char *or_bnm_bridge_status(void) {
    return g_bnm.status;
}

extern "C" bool or_bnm_read_npc(void *instance,
                                 uint32_t *npc_type,
                                 OR_VanillaStats *stats,
                                 bool *is_boss,
                                 bool *is_town,
                                 bool *is_friendly,
                                 bool *active) {
    if (!g_bnm.capabilities.stats_read_ready || !instance || !npc_type || !stats) {
        return false;
    }

    int32_t type = 0;
    int32_t life_max = 0;
    int32_t life = 0;
    int32_t damage = 0;
    int32_t defense = 0;
    int32_t ai_style = -1;
    float knockback = 0.0f;
    float scale = 1.0f;
    float slots = 1.0f;
    float value = 0.0f;
    bool local_boss = false;
    bool local_town = false;
    bool local_friendly = false;
    bool local_active = false;

    if (!read(g_bnm.type, instance, type) ||
        !read(g_bnm.life_max, instance, life_max) || type <= 0 || life_max <= 0) {
        return false;
    }
    if (!read(g_bnm.life, instance, life)) life = life_max;
    (void)read(g_bnm.damage, instance, damage);
    (void)read(g_bnm.defense, instance, defense);
    (void)read(g_bnm.knockback_resist, instance, knockback);
    (void)read(g_bnm.scale, instance, scale);
    (void)read(g_bnm.npc_slots, instance, slots);
    (void)read(g_bnm.value, instance, value);
    (void)read(g_bnm.ai_style, instance, ai_style);
    (void)read(g_bnm.boss, instance, local_boss);
    (void)read(g_bnm.town_npc, instance, local_town);
    (void)read(g_bnm.friendly, instance, local_friendly);
    (void)read(g_bnm.active, instance, local_active);

    *npc_type = static_cast<uint32_t>(type);
    stats->life_max = life_max;
    stats->life_current = life > 0 ? life : 0;
    stats->damage = damage > 0 ? damage : 0;
    stats->defense = defense > 0 ? defense : 0;
    stats->knockback_resist = std::isfinite(knockback) && knockback >= 0.0f
        ? knockback : 0.0f;
    stats->scale = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
    stats->npc_slots = std::isfinite(slots) && slots > 0.0f ? slots : 1.0f;
    stats->money = std::isfinite(value) && value > 0.0f
        ? static_cast<int64_t>(std::llround(value)) : 0;
    stats->ai_style = ai_style;
    if (is_boss) *is_boss = local_boss;
    if (is_town) *is_town = local_town;
    if (is_friendly) *is_friendly = local_friendly;
    if (active) *active = local_active;
    return true;
}

extern "C" bool or_bnm_write_npc_stats(void *instance,
                                        const OR_FinalStats *stats) {
    if (!g_bnm.capabilities.stats_write_ready || !instance || !stats) return false;

    int32_t width = 0;
    int32_t height = 0;
    float vanilla_scale = 1.0f;
    const bool have_body = read(g_bnm.width, instance, width) &&
                           read(g_bnm.height, instance, height) &&
                           read(g_bnm.scale, instance, vanilla_scale) &&
                           std::isfinite(vanilla_scale) && vanilla_scale > 0.0f;

    bool ok = write(g_bnm.life_max, instance, clamp_i32(stats->life_max));
    ok = write(g_bnm.life, instance, clamp_i32(stats->life_current)) && ok;
    if (g_bnm.damage.IsValid()) {
        ok = write(g_bnm.damage, instance, stats->damage < 0 ? 0 : stats->damage) && ok;
    }
    if (g_bnm.defense.IsValid()) {
        ok = write(g_bnm.defense, instance, stats->defense < 0 ? 0 : stats->defense) && ok;
    }
    if (g_bnm.knockback_resist.IsValid()) {
        ok = write(g_bnm.knockback_resist, instance, stats->knockback_resist) && ok;
    }
    if (g_bnm.scale.IsValid()) {
        ok = write(g_bnm.scale, instance, stats->scale) && ok;
    }
    if (g_bnm.value.IsValid()) {
        ok = write(g_bnm.value, instance, clamp_f32(stats->money)) && ok;
    }
    if (g_bnm.npc_slots.IsValid()) {
        ok = write(g_bnm.npc_slots, instance, stats->npc_slots) && ok;
    }

    if (have_body && stats->scale > 0.0f) {
        const double ratio = static_cast<double>(stats->scale) /
                             static_cast<double>(vanilla_scale);
        const auto scaled_width = clamp_i32(static_cast<int64_t>(std::llround(width * ratio)));
        const auto scaled_height = clamp_i32(static_cast<int64_t>(std::llround(height * ratio)));
        ok = write(g_bnm.width, instance, scaled_width) && ok;
        ok = write(g_bnm.height, instance, scaled_height) && ok;
    }
    return ok;
}
