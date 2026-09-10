#include "or_visual_effects.h"

#include "or_config.h"
#include "or_log.h"
#include "tefkernel/patchlib/field.h"
#include "tefkernel/patchlib/method.h"

#include <math.h>
#include <string.h>
#include <stdint.h>

#define OR_LOG(level, ...) do { or_log_write((level), __VA_ARGS__); } while (0)

/* Terraria's vanilla dust array is commonly indexed [0, 5999], with 6000
 * used as the no-slot sentinel by NewDust. Treat it as a failed allocation,
 * not as a committed particle. */
#define OR_DUST_MAX_INDEX 6000

/* Terraria's explicitly tintable dust. The lighted tintable variant washed
 * purple/blue colors toward white on the mobile renderer, so use the plain
 * tintable material for faithful tier colors. Torch dusts were
 * rejected after device testing: their built-in palette overrode the Color
 * argument (the green tier appeared yellow). This material is chosen so the
 * already verified Color-by-value argument remains the source of truth. */
#define OR_DUST_TINTABLE 4

static bool emit_spawn_offset(const OR_Runtime *runtime,
                              patch_handle_t instance,
                              OR_EliteTier tier,
                              uint32_t npc_type,
                              float offset_x,
                              float offset_y,
                              float speed_x,
                              float speed_y) {
#if !defined(__ANDROID__)
    (void)runtime;
    (void)instance;
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[VISUAL_EFFECT_SKIP] type=%u tier=%s effect=NewDust "
           "reason=android_value_invoke_only",
           (unsigned)npc_type, or_elite_tier_name(tier));
    return false;
#else
    unsigned char position_raw[8] = {0};
    float position[2];
    int32_t width = 1;
    int32_t height = 1;
    int32_t dust_type = OR_DUST_TINTABLE;
    int32_t alpha = 0;
    float scale = 0.72f;
    int32_t npc_width = 16;
    int32_t npc_height = 16;
    /* NewDust takes Color by value. PatchLib represents this managed value
     * through a verified native storage slot; a null pointer is not a valid
     * substitute for the value argument. This is particle-only color: it does
     * not write NPC.color or any other body-tint field. */
    unsigned char color_raw[4] = {255u, 255u, 255u, 255u};
    const char *color_name = "unknown";
    int32_t dust_index = -1;
    static const patch_type_t vector2_fields[] = {PATCH_FLOAT, PATCH_FLOAT};
    static const patch_type_t color_fields[] = {
        PATCH_UINT8, PATCH_UINT8, PATCH_UINT8, PATCH_UINT8
    };
    patchlib_value_arg_t value_args[9] = {
        {position, sizeof(position), vector2_fields, 2u},
        {NULL, 0u, NULL, 0u},
        {NULL, 0u, NULL, 0u},
        {NULL, 0u, NULL, 0u},
        {NULL, 0u, NULL, 0u},
        {NULL, 0u, NULL, 0u},
        {NULL, 0u, NULL, 0u},
        {color_raw, sizeof(color_raw), color_fields, 4u},
        {NULL, 0u, NULL, 0u}
    };
    void *args[9] = {position, &width, &height, &dust_type,
                     &speed_x, &speed_y, &alpha, color_raw, &scale};

    if (!runtime || !instance || !runtime->capabilities.dust_new_dust_ready ||
        !runtime->capabilities.dust_value_invoke_ready ||
        !runtime->method_dust_new_dust || !runtime->field_position_probe ||
        !patchlib_field_get_value) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[VISUAL_EFFECT_SKIP] type=%u tier=%s effect=NewDust "
               "reason=abi_not_verified",
               (unsigned)npc_type, or_elite_tier_name(tier));
        return false;
    }

    if (!patchlib_method_invoke_value_args) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[VISUAL_EFFECT_SKIP] type=%u tier=%s effect=NewDust "
               "reason=struct_value_api_missing",
               (unsigned)npc_type, or_elite_tier_name(tier));
        return false;
    }

    patchlib_field_get_value(runtime->field_position_probe, instance,
                             position_raw);
    memcpy(position, position_raw, sizeof(position));
    if (!isfinite(position[0]) || !isfinite(position[1])) {
        OR_LOG(MOD_LOG_LEVEL_INFO,
               "[VISUAL_EFFECT_SKIP] type=%u tier=%s effect=NewDust "
               "reason=position_invalid",
               (unsigned)npc_type, or_elite_tier_name(tier));
        return false;
    }
    if (runtime->field_width && runtime->field_height) {
        patchlib_field_get_value(runtime->field_width, instance, &npc_width);
        patchlib_field_get_value(runtime->field_height, instance, &npc_height);
        if (npc_width < 1 || npc_width > 256) npc_width = 16;
        if (npc_height < 1 || npc_height > 256) npc_height = 16;
    }
    position[0] += (float)npc_width * 0.5f;
    position[1] += (float)npc_height * 0.5f;
    position[0] += offset_x;
    position[1] += offset_y;

    if (tier < OR_TIER_ALTERED || tier > OR_TIER_APOCALYPSE) return false;
    switch (tier) {
        case OR_TIER_ALTERED:
            color_raw[0] = 73u;   /* #49C96D */
            color_raw[1] = 201u;
            color_raw[2] = 109u;
            color_name = "altered_green";
            break;
        case OR_TIER_CALAMITY:
            color_raw[0] = 66u;   /* #4287F5 */
            color_raw[1] = 135u;
            color_raw[2] = 245u;
            color_name = "calamity_blue";
            break;
        case OR_TIER_APOCALYPSE:
            color_raw[0] = 192u;  /* #C04CFF */
            color_raw[1] = 76u;
            color_raw[2] = 255u;
            color_name = "apocalypse_purple";
            break;
        default:
            return false;
    }
    if (!patchlib_method_invoke_value_args(runtime->method_dust_new_dust,
                                           PATCH_NULL, &dust_index, args,
                                           value_args)) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[VISUAL_EFFECT_SKIP] type=%u tier=%s effect=NewDust "
               "reason=invoke_failed",
               (unsigned)npc_type, or_elite_tier_name(tier));
        return false;
    }
    if (dust_index < 0 || dust_index >= OR_DUST_MAX_INDEX) {
        OR_LOG(MOD_LOG_LEVEL_WARNING,
               "[VISUAL_EFFECT_SKIP] type=%u tier=%s effect=NewDust "
               "reason=no_dust_slot returnIndex=%d",
               (unsigned)npc_type, or_elite_tier_name(tier), (int)dust_index);
        return false;
    }

    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[VISUAL_EFFECT_COMMIT] type=%u tier=%s effect=NewDust "
           "mode=tintable_magic dustType=%d material=tintable color=%s rgba=%u,%u,%u,%u "
           "particle_only=yes struct_abi=yes returnIndex=%d position=%.2f,%.2f",
           (unsigned)npc_type, or_elite_tier_name(tier),
           (int)dust_type, color_name,
           (unsigned)color_raw[0], (unsigned)color_raw[1],
           (unsigned)color_raw[2], (unsigned)color_raw[3],
           (int)dust_index,
           (double)position[0], (double)position[1]);
    return true;
#endif
}

bool or_visual_effects_emit_spawn(const OR_Runtime *runtime,
                                  patch_handle_t instance,
                                  OR_EliteTier tier,
                                  uint32_t npc_type) {
    return emit_spawn_offset(runtime, instance, tier, npc_type,
                             0.0f, 0.0f, 0.0f, -0.10f);
}

bool or_visual_effects_emit_aura(const OR_Runtime *runtime,
                                 patch_handle_t instance,
                                 OR_EliteTier tier,
                                 uint32_t npc_type,
                                 uint32_t ai_tick) {
    unsigned emitted = 0u;
    unsigned requested = 0u;
    unsigned i;
    unsigned phase;
    float motes[5][4];
    float direction;

    /* Goblin-sorcerer-inspired magic arc: a few restrained, colored motes
     * hover above the head and drift upward slightly. It uses only the
     * already-gated vanilla particle call; no renderer ABI is guessed. */
    if ((ai_tick % 30u) != 0u || tier < OR_TIER_ALTERED || tier > OR_TIER_APOCALYPSE)
        return false;
    phase = ((ai_tick / 30u) + npc_type) & 1u;
    direction = phase ? -1.0f : 1.0f;
    memset(motes, 0, sizeof(motes));
    if (tier == OR_TIER_ALTERED) {
        /* A small, two-mote green casting curl. */
        requested = 2u;
        motes[0][0] = -5.0f * direction; motes[0][1] = -25.0f;
        motes[0][2] = 0.14f * direction; motes[0][3] = -0.16f;
        motes[1][0] = 4.0f * direction;  motes[1][1] = -32.0f;
        motes[1][2] = -0.09f * direction; motes[1][3] = -0.11f;
    } else if (tier == OR_TIER_CALAMITY) {
        /* A three-mote blue casting curl with a clear central focus. */
        requested = 3u;
        motes[0][0] = -8.0f * direction; motes[0][1] = -25.0f;
        motes[0][2] = 0.17f * direction; motes[0][3] = -0.17f;
        motes[1][0] = 0.0f;                motes[1][1] = -34.0f;
        motes[1][2] = 0.04f * direction; motes[1][3] = -0.13f;
        motes[2][0] = 8.0f * direction;  motes[2][1] = -27.0f;
        motes[2][2] = -0.15f * direction; motes[2][3] = -0.15f;
    } else {
        /* Four violet motes make a restrained crown-like spell curl. */
        requested = 4u;
        motes[0][0] = -10.0f * direction; motes[0][1] = -25.0f;
        motes[0][2] = 0.18f * direction; motes[0][3] = -0.18f;
        motes[1][0] = -3.5f * direction;  motes[1][1] = -35.0f;
        motes[1][2] = 0.08f * direction; motes[1][3] = -0.14f;
        motes[2][0] = 3.5f * direction;   motes[2][1] = -35.0f;
        motes[2][2] = -0.08f * direction; motes[2][3] = -0.14f;
        motes[3][0] = 10.0f * direction;  motes[3][1] = -27.0f;
        motes[3][2] = -0.18f * direction; motes[3][3] = -0.18f;
    }
    for (i = 0u; i < requested; ++i) {
        if (emit_spawn_offset(runtime, instance, tier, npc_type,
                              motes[i][0], motes[i][1],
                              motes[i][2], motes[i][3])) {
            ++emitted;
        }
    }
    OR_LOG(MOD_LOG_LEVEL_INFO,
           "[VISUAL_AURA] type=%u tier=%s tick=%u requested=%u emitted=%u "
           "particle_only=yes style=%s tintable=yes magic_arc=yes period_ticks=30 low_density=yes",
           (unsigned)npc_type, or_elite_tier_name(tier), (unsigned)ai_tick,
           requested, emitted,
           tier == OR_TIER_ALTERED ? "single_arc" :
               (tier == OR_TIER_CALAMITY ? "double_arc" : "crown_arc"));
    return emitted != 0u;
}
