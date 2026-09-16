#ifndef ORIGINREWRITE_HEAD_MARKER_H
#define ORIGINREWRITE_HEAD_MARKER_H

#include "or_runtime.h"
#include "or_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves the exact Terraria drawing signatures used by the lightweight
 * world-space marker.  A failed probe is intentionally non-fatal: gameplay
 * keeps running with the marker safely disabled. */
bool or_head_marker_probe(const OR_Runtime *runtime);

/* The renderer installs this method as a postfix hook. */
patch_handle_t or_head_marker_draw_method(void);
patch_handle_t or_head_marker_draw_method_lit(void);
patch_handle_t or_head_marker_frame_method(void);
bool or_head_marker_get_screen_position(float out[2]);
bool or_head_marker_read_player_dead(patch_handle_t player, bool *dead);

/* Draws one cached icon above an already-confirmed elite NPC. screen_pos is
 * the camera position supplied by Main.DrawNPCDirect. */
void or_head_marker_draw(const OR_Runtime *runtime,
                         patch_handle_t npc,
                         OR_EliteTier tier,
                         const float screen_pos[2]);

/* Drops local handles/state on module unload. The native Texture2D remains
 * owned by the running game process and is never recreated per NPC/world. */
void or_head_marker_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ORIGINREWRITE_HEAD_MARKER_H */
