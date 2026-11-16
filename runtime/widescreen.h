/*
 * widescreen.h - the picture beyond the NES's 256 pixels: the map squares left and right of the
 * screen, asked of the game's own map code, and the people standing on them.
 *
 * The canvas is WS_W x 240: the PPU's picture in the middle (at x = WS_BAND) and a band of
 * WS_BAND pixels on each side. A band is filled only while the screen shows the map (walking
 * about in a town, a castle, a cave or the overworld); in battles, on the title and on other
 * screens the bands stay black and the frontend shows the normal picture between bars.
 */
#ifndef WIDESCREEN_H
#define WIDESCREEN_H
#include <stdint.h>

#define WS_BAND 48                    /* three map squares */
#define WS_W (256 + 2 * WS_BAND)
#define WS_H 240

/* The part (x, y, w, h) of the canvas as 6-bit palette indices into out (w per line) and each
   line's $2001 bits into emph. Returns 1 when the bands hold the map, 0 when they are black. */
int widescreen_render(uint8_t *out, uint8_t *emph, int x, int y, int w, int h);
/* The same, with every pixel's source for the remastered textures (remaster.h); needs ppu_hd_capture. */
int widescreen_render_hd(uint8_t *out, uint8_t *emph, uint32_t *hbg, uint32_t *hspr, int x, int y, int w, int h);

/* The width of the canvas that makes a picture of target_aspect from src_h lines of pixels that
   are pixel_aspect wide: an even number, at most WS_W. */
int widescreen_visible_width(int src_h, double pixel_aspect, double target_aspect);

/* For tests: how the map code's answer for the squares on the screen compares with what the PPU
   was given to draw. Returns the number of squares that differ (-1 when no map is shown) and the
   number compared in *cells. */
int widescreen_check(int *cells);
/* and how many people the last picture has in its bands */
int widescreen_people(void);
/* and how many times the game's map routine has been run on the side so far */
unsigned long widescreen_calls(void);

#endif
