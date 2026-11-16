/*
 * title.c - the Dragon Quest IV logo and the Dragon Warrior IV plaque, one in place of the other (title.h).
 */
#include "title.h"
#include "nes.h"

#if defined(__has_include)
#  if __has_include("../generated/title_dq.h") && __has_include("../generated/title_dw.h")
#    include "../generated/title_dq.h"
#    include "../generated/title_dw.h"
#    define HAVE_PICTURES 1
#  endif
#endif

/* The plaque: 24 x 10 tiles at column 4, row 4 of the first name table, in a frame of twelve
   tiles ($80-$8B) that stays as it is while the letters are engraved (those are drawn into the
   tiles' patterns). The logo: the tiles of rows 4 to 11, columns 4 to 28, told by their patterns
   (a hack that brings the logo along numbers its tiles differently). */
#define PLAQUE_COL 4
#define PLAQUE_ROW 4
#define PLAQUE_COLS 24
#define PLAQUE_ROWS 10
#define FRAME_HASH 0xDAB537DF45BF5CDFULL    /* FNV-1a of the twelve frame tiles' patterns */
#define LOGO_HASH  0x09D04A23574077B1ULL    /* FNV-1a of the patterns of the logo's 25 x 8 tiles */

/* the area either of them covers: saved before it is drawn over */
#define AREA_X 32
#define AREA_Y 32
#define AREA_W 200
#define AREA_H 80
static uint8_t saved_fb[AREA_W * AREA_H];
static uint32_t saved_bg[AREA_W * AREA_H], saved_spr[AREA_W * AREA_H];
static int saved;
static uint64_t saved_frame;    /* the frame the saved pixels are of */

int title_available(void) {
#ifdef HAVE_PICTURES
    return 1;
#else
    return 0;
#endif
}

static inline uint64_t fnv_tile(uint64_t h, uint16_t base, uint8_t tile) {
    for (uint16_t a = (uint16_t)(base + tile * 16), end = (uint16_t)(a + 16); a < end; a++) {
        h ^= mapper.chr_ptr[(a >> 10) & 7][a & 0x3FF];
        h *= 0x100000001b3ULL;
    }
    return h;
}

int title_scene(void) {
    if (!(ppu.mask & 0x08) || ppu.frame_t || ppu.frame_x) return TITLE_NONE;     /* background on, nothing scrolled */
    const uint8_t *nt = ppu.vram + ppu.nt_offset[0];
    uint16_t base = (ppu.frame_ctrl & 0x10) ? 0x1000 : 0;
    const uint8_t *top = nt + PLAQUE_ROW * 32 + PLAQUE_COL, *bottom = top + (PLAQUE_ROWS - 1) * 32;
    uint64_t h = 0xcbf29ce484222325ULL;
    if (top[0] == 0x80 && top[PLAQUE_COLS - 1] == 0x83 && bottom[0] == 0x88 && bottom[PLAQUE_COLS - 1] == 0x8B) {
        int frame = 1;
        for (int c = 1; c < PLAQUE_COLS - 1 && frame; c++)
            if (top[c] != (c & 1 ? 0x81 : 0x82) || bottom[c] != (c & 1 ? 0x89 : 0x8A)) frame = 0;
        for (int r = 1; r < PLAQUE_ROWS - 1 && frame; r++)
            if (top[r * 32] != (r & 1 ? 0x84 : 0x85) || top[r * 32 + PLAQUE_COLS - 1] != (r & 1 ? 0x86 : 0x87)) frame = 0;
        if (frame) {
            for (int t = 0x80; t < 0x8C; t++) h = fnv_tile(h, base, (uint8_t)t);
            if (h == FRAME_HASH) return TITLE_PLAQUE;
        }
    }
    if ((ppu.palette[0] & 0x3F) == 0x0F) return TITLE_NONE;      /* still dark: the logo has not appeared */
    h = 0xcbf29ce484222325ULL;
    for (int r = 4; r < 12; r++)
        for (int c = 4; c < 29; c++) h = fnv_tile(h, base, nt[r * 32 + c]);
    return h == LOGO_HASH ? TITLE_LOGO : TITLE_NONE;
}

#ifdef HAVE_PICTURES
static void runs(const unsigned char *data, unsigned len, int x0, int y0, int w, int h) {
    int x = 0, y = 0;
    for (unsigned i = 0; i + 1 < len && y < h; i += 2) {
        uint8_t colour = data[i + 1];
        for (int n = data[i]; n > 0 && y < h; n--) {
            int px = x0 + x, py = y0 + y;
            if (colour != 0xFF && px < 256 && py < 240) ppu.fb[py * 256 + px] = colour;
            if (++x == w) { x = 0; y++; }
        }
    }
}
#endif

void title_draw(int want) {
#ifdef HAVE_PICTURES
    uint8_t sky = ppu.palette[0] & 0x3F;
    title_undo();           /* (drawn twice over the same frame: from the PPU's pixels again) */
    /* what is there away: sky, and no source tiles for the enhanced graphics to redraw */
    for (int y = 0; y < AREA_H; y++)
        for (int x = 0; x < AREA_W; x++) {
            int i = (AREA_Y + y) * 256 + AREA_X + x, k = y * AREA_W + x;
            saved_fb[k] = ppu.fb[i]; saved_bg[k] = ppu.hd_bg[i]; saved_spr[k] = ppu.hd_spr[i];
            ppu.fb[i] = sky;
            ppu.hd_bg[i] = ppu.hd_spr[i] = 0;
        }
    saved = 1; saved_frame = ppu.frame;
    if (want == TITLE_LOGO) runs(title_dq_runs, sizeof title_dq_runs, TITLE_DQ_X, TITLE_DQ_Y, TITLE_DQ_W, TITLE_DQ_H);
    else runs(title_dw_runs, sizeof title_dw_runs, TITLE_DW_X, TITLE_DW_Y, TITLE_DW_W, TITLE_DW_H);
#else
    (void)want;
#endif
}

void title_undo(void) {
    if (!saved) return;
    if (saved_frame != ppu.frame) { saved = 0; return; }       /* the PPU has drawn a new picture since */
    for (int y = 0; y < AREA_H; y++)
        for (int x = 0; x < AREA_W; x++) {
            int i = (AREA_Y + y) * 256 + AREA_X + x, k = y * AREA_W + x;
            ppu.fb[i] = saved_fb[k]; ppu.hd_bg[i] = saved_bg[k]; ppu.hd_spr[i] = saved_spr[k];
        }
    saved = 0;
}
