/*
 * title.c - the Dragon Quest IV logo in place of the Dragon Warrior IV plaque (title.h).
 */
#include "title.h"
#include "nes.h"

#if defined(__has_include)
#  if __has_include("../generated/title_dq.h")
#    include "../generated/title_dq.h"
#    define HAVE_LOGO 1
#  endif
#endif

/* The plaque: 24 x 10 tiles at column 4, row 4 of the first name table, in a frame of twelve
   tiles ($80-$8B) that stays as it is while the letters are engraved (those are drawn into the
   tiles' patterns). */
#define PLAQUE_COL 4
#define PLAQUE_ROW 4
#define PLAQUE_COLS 24
#define PLAQUE_ROWS 10
#define FRAME_HASH 0xDAB537DF45BF5CDFULL    /* FNV-1a of the twelve frame tiles' patterns */

int title_available(void) {
#ifdef HAVE_LOGO
    return 1;
#else
    return 0;
#endif
}

int title_plaque(void) {
    if (!(ppu.mask & 0x08) || ppu.frame_t || ppu.frame_x) return 0;     /* background on, nothing scrolled */
    const uint8_t *nt = ppu.vram + ppu.nt_offset[0];
    const uint8_t *top = nt + PLAQUE_ROW * 32 + PLAQUE_COL, *bottom = top + (PLAQUE_ROWS - 1) * 32;
    if (top[0] != 0x80 || top[PLAQUE_COLS - 1] != 0x83 || bottom[0] != 0x88 || bottom[PLAQUE_COLS - 1] != 0x8B) return 0;
    for (int c = 1; c < PLAQUE_COLS - 1; c++)
        if (top[c] != (c & 1 ? 0x81 : 0x82) || bottom[c] != (c & 1 ? 0x89 : 0x8A)) return 0;
    for (int r = 1; r < PLAQUE_ROWS - 1; r++)
        if (top[r * 32] != (r & 1 ? 0x84 : 0x85) || top[r * 32 + PLAQUE_COLS - 1] != (r & 1 ? 0x86 : 0x87)) return 0;
    uint16_t base = (ppu.frame_ctrl & 0x10) ? 0x1000 : 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint16_t a = (uint16_t)(base + 0x80 * 16); a < base + 0x8C * 16; a++) {
        h ^= mapper.chr_ptr[(a >> 10) & 7][a & 0x3FF];
        h *= 0x100000001b3ULL;
    }
    return h == FRAME_HASH;
}

void title_draw(void) {
#ifdef HAVE_LOGO
    uint8_t sky = ppu.palette[0] & 0x3F;
    /* the plaque away: sky, and no source tiles for the enhanced graphics to redraw */
    for (int y = PLAQUE_ROW * 8; y < (PLAQUE_ROW + PLAQUE_ROWS) * 8; y++)
        for (int x = PLAQUE_COL * 8; x < (PLAQUE_COL + PLAQUE_COLS) * 8; x++) {
            ppu.fb[y * 256 + x] = sky;
            ppu.hd_bg[y * 256 + x] = ppu.hd_spr[y * 256 + x] = 0;
        }
    int x = 0, y = 0;
    for (unsigned i = 0; i + 1 < sizeof title_dq_runs && y < TITLE_DQ_H; i += 2) {
        uint8_t colour = title_dq_runs[i + 1];
        for (int n = title_dq_runs[i]; n > 0 && y < TITLE_DQ_H; n--) {
            int px = TITLE_DQ_X + x, py = TITLE_DQ_Y + y;
            if (px < 256 && py < 240) {
                if (colour != 0xFF) ppu.fb[py * 256 + px] = colour;
                ppu.hd_bg[py * 256 + px] = ppu.hd_spr[py * 256 + px] = 0;
            }
            if (++x == TITLE_DQ_W) { x = 0; y++; }
        }
    }
#endif
}
