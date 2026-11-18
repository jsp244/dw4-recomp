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

/* The canvas: the PPU's picture with bands of map around it. Canvas pixel (WS_BAND, WS_VBAND) is
   the picture's first pixel. */
#define WS_BAND 64                    /* at each side: four map squares */
#define WS_VBAND 16                   /* above and below: one */
#define WS_W (256 + 2 * WS_BAND)
#define WS_H (240 + 2 * WS_VBAND)

/* The part (x, y, w, h) of the canvas as 6-bit palette indices into out (w per line) and each
   line's $2001 bits into emph. Returns 1 when the bands hold the map, 0 when they are black. */
int widescreen_render(uint8_t *out, uint8_t *emph, int x, int y, int w, int h);
/* The same, with every pixel's source for the remastered textures (remaster.h); needs ppu_hd_capture. */
int widescreen_render_hd(uint8_t *out, uint8_t *emph, uint32_t *hbg, uint32_t *hspr, int x, int y, int w, int h);

/* The same again, with each background pixel's place for the 16-bit textures (sfc.h): HD_CELL
   and a name table cell where the PPU drew it, HD_CELL_BAND and one of the canvas's own map
   squares where the map did. */
int widescreen_render_cells(uint8_t *out, uint8_t *emph, uint32_t *hbg, uint32_t *hspr, uint16_t *hcell, int x, int y, int w, int h);
/* Square n of the last canvas's map (as HD_CELL_BAND numbers them): its four tiles and its palette.
   Returns 0 when there is none. */
int widescreen_square(int n, uint8_t tile[4], uint8_t *pal);
/* The people the last canvas drew beside the PPU's picture, each as a whole: where their 16x16
   pixels start, counted from the PPU picture's first pixel, and the tile and sprite attributes of
   each quarter (top left, top right, bottom left, bottom right). Returns how many. */
typedef struct { int x, y; uint8_t tile[4], attr[4]; } ws_person_t;
int widescreen_people_list(const ws_person_t **list);
/* The map square one row below the square that holds pixel (sx, line) of the PPU's picture (the
   bands' pixels have sx < 0 or >= 256, line < 0 or >= 240): its number as widescreen_square takes
   it when the canvas draws that square from the map, -2 when it is one the PPU shows (look in the
   name tables), -1 when there is none. For art that stands taller than its square. */
int widescreen_below(int sx, int line);
/* Where on the map the PPU picture's first pixel lies, in pixels from the map's first square (sixteen to a
   square; negative above and left of it): for art that is laid over the map by place. Known from the last
   frame whose map was decoded;
   while a window hides too much of the map for that, the scroll carries it on. Returns 0 until a map has
   been decoded. */
int widescreen_origin(int *wx, int *wy);
/* Does the canvas take the PPU picture's first eight pixels from the map (the game blanks them)? */
int widescreen_strip(void);

/* The width of the canvas that makes a picture of target_aspect from src_h lines of pixels that
   are pixel_aspect wide: an even number, at most WS_W. */
int widescreen_visible_width(int src_h, double pixel_aspect, double target_aspect);

/* For tests: how the map code's answer for the squares on the screen compares with what the PPU
   was given to draw. Returns the number of squares that differ (-1 when no map is shown) and the
   number compared in *cells. */
int widescreen_check(int *cells);
/* Is this pixel of the PPU's picture shown as the PPU drew it? Not while the map is decoded and it
   lies in the outer ring: the row and the column of map squares the game rewrites in its name
   tables while the screen scrolls (they show half-written for a few frames of every step), and
   the eight pixels the PPU blanks at the left. Those come from the map like the bands. */
int widescreen_inner(int sx, int line);
extern long widescreen_still_edge_bad; /* checks with the screen at rest and a difference in its first column or row */
extern long widescreen_better;        /* checks in which a neighbouring camera square fits the screen clearly better */
extern long widescreen_jumps;         /* frames in which the decoded map jumped while the PPU's scroll did not */
extern int widescreen_bad_box[4];     /* of that check: the first and last column, the first and last row that differ */
/* For tools: the whole map the player is on, asked of the game's map code square by square: out
   takes 256 x 256 squares of five bytes (the four tiles, the palette), *px and *py the player's
   square. Returns 0 when the routine did not come back. */
int widescreen_map_dump(uint8_t *out, int *px, int *py);
/* and how many people the last picture has in its bands */
int widescreen_people(void);
/* and how many times the game's map routine has been run on the side so far */
unsigned long widescreen_calls(void);

#endif
