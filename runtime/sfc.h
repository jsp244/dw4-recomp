/*
 * sfc.h - the 16-bit textures: the game's map squares and people drawn with the art of the Super
 * Famicom Dragon Quest games (I.II and III), taken from the player's own cartridges by tools/sfc
 * into a pack (sfc.pak) next to the game.
 *
 * Those games draw on the same grid the NES does, 8x8 tiles in 16x16 map squares at 256 pixels
 * across, so the picture keeps its size and only its pixels change: where the screen shows a map
 * square the pack knows, the square's 16x16 pixels are drawn in its place, and the same for the
 * people walking on the map. Whatever the pack does not know (and every window, letter and battle
 * until they are covered) is drawn as the Pixel Remaster textures draw it (remaster.h), so a
 * frame never has a hole.
 */
#ifndef SFC_H
#define SFC_H
#include <stdint.h>

/* Loads the pack at pack_path. Returns the number of squares and people it holds (0: none, and
   sfc_ready() stays 0). remaster_init() must have been called as well: it draws the rest. */
int  sfc_init(const char *pack_path);
int  sfc_ready(void);
void sfc_forget(void);

/* Colors w x h pixels as ARGB into out (stride pixels per line). idx, bg, spr and emph are what
   remaster_frame() takes; cell is each background pixel's place (HD_CELL / HD_CELL_BAND, nes.h),
   as widescreen_render_cells() gives it, and (org_x, org_y) is where out's first pixel lies in the
   PPU's picture (negative in the canvas's bands). */
void sfc_frame(uint32_t *out, int stride, const uint8_t *idx, const uint32_t *bg, const uint32_t *spr, const uint16_t *cell,
               int src_stride, const uint8_t *emph, int w, int h, int org_x, int org_y);

/* For tools: how many of the last frame's map squares the pack had, and how many it did not. */
void sfc_stats(int *known, int *unknown, int *people, int *people_unknown);
/* and the keys of what it did not have: up to max of the squares' into keys (those of the map
   the canvas draws around the picture: a square the PPU drew may be part of a window), up to max of
   the figures' into people (the same keys tools/sfc/nesmap.py lists). Returns the numbers through n. */
void sfc_unknown(uint64_t *keys, uint64_t *people, int max, int n[2]);

#endif
