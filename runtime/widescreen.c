/*
 * widescreen.c - the map and the people beyond the screen's edges (widescreen.h).
 *
 * The map. The game draws its maps from 16x16 squares, sixteen across the screen, the camera
 * following the player. It has a routine (bank 22, $8DD4) that answers "which four tiles and
 * which palette does the map square at this column and row of the screen have": it uses it to
 * paint the map back where a window closes. Asked for columns left of 0 and right of 15 it
 * answers just the same, from the same map data, with whatever the game has changed in it
 * (opened doors, taken chests). So the bands are not decoded here: the routine is run, on the
 * side, for the squares they show (nes_sandbox_*: the machine is saved, the routine runs in the
 * interpreter with the hardware frozen, and everything is put back, so the game never notices).
 * A call costs about as much as a hundredth of a frame, so the answers are kept by map square:
 * a square not yet known is asked at once, the known ones are asked again an eighth at a time,
 * so that a change in the map reaches the bands within eight frames.
 *
 * Where the squares are. The camera is at $EC (x) and $ED (y), in squares, with the part of a
 * square in $EA and $EB; the pixels within the square are taken from the PPU's scroll instead,
 * since the map sits in the nametables aligned to its squares and the scroll is what the frame
 * was really drawn with. The routine counts from the player ($42/$43 on the overworld, $44/$45
 * in towns and caves; $41 bit 7 says which): column $0D and row $03E1 give the map square
 * (player x - 8 + column, player y - 6 + row).
 *
 * Whether the screen shows the map at all is decided by looking: the squares the routine names
 * for four columns of the screen are compared with the tiles the PPU was given there. In a
 * battle, on the title or in a full-screen menu they do not agree, and the bands stay black.
 * Windows over the map cover some of those squares, so a share of them is enough; but a few of
 * the squares that agree must have something drawn in them, because before any map is loaded
 * the routine answers with empty squares, and an empty stretch of the title's sky or of a black
 * menu screen agrees with those. The game's memory runs a frame ahead of the picture, so on the
 * frame a step ends the camera's square in memory is already the next one: when the squares do
 * not agree as they stand, the four neighbouring camera squares are tried.
 *
 * The people. Everyone on a map is in a table of up to 30 objects (0 is the player): position
 * in sixteenths of a pixel ($6FA0/$7100 x, $6FC0/$7120 y), kind ($6FE0), facing ($7000), flags
 * ($7020: $FF ends the list, $90 hidden), the floor they are on ($7140, against $46). The game
 * gives sprites only to those on the screen; here the ones in the bands are drawn from the same
 * table, with the tiles the game's own sprite definitions give (RAM $7200: 64 bytes per kind,
 * 16 per facing, 8 per walking frame: a tile and its attributes for each quarter).
 *
 * The leftmost eight pixels: the game has the PPU blank them (as most scrolling games do, to
 * hide the column being redrawn). On the canvas that would be a black bar between the left band
 * and the picture, so that strip is drawn from the map as well, people included.
 */
#include "widescreen.h"
#include "nes.h"
#include <string.h>

#define COL0 (-4)                 /* squares, counted from the camera's: four left of the screen */
#define COLS 25                   /* ... to four beyond its right edge while it scrolls */
#define ROW0 (-1)                 /* one above it */
#define ROWS 18                   /* ... to one below while it scrolls */
#define GET_BLOCK_BANK 22
#define GET_BLOCK 0x8DD4          /* in: $0D column, $03E1 row; out: $00-$03 the tiles, $04 the palette */
#define OBJECTS 30
#define PROBED 80                 /* squares compared to find the camera's square: see probe() */

typedef struct { uint8_t tile[4], pal, ok; } square_t;
typedef struct { square_t sq; uint8_t mx, my; uint32_t frame; } known_t;

static square_t squares[ROWS][COLS];      /* this frame's, by column and row of the camera's grid */
static known_t known[32][32];             /* the answers kept, by map square (its low five bits) */
static int in_sandbox;
static int decoded;                       /* the bands hold the map */
static int cam_dx, cam_dy;                /* the picture's camera square, from the one in memory */
static int scroll_x, scroll_y;            /* the screen's top left corner in the nametables, pixels */
static int fine_x, fine_y;                /* and within the camera's square */
static uint64_t stamp_frame, stamp_cycles;
static int stamped;
static unsigned long asked_total;         /* calls of the game's routine (statistics) */

/* the people in the bands, drawn once per frame: per canvas pixel 0xFF or the palette index,
   and the pixel's source for the remaster (with HD_BEHIND for those behind the map) */
static uint8_t people[WS_H][WS_W];
static uint32_t people_src[WS_H][WS_W];
static int people_drawn;

static inline int player_x(void) { return (ram[0x41] & 0x80) ? ram[0x44] : ram[0x42]; }
static inline int player_y(void) { return (ram[0x41] & 0x80) ? ram[0x45] : ram[0x43]; }

/* The map square (mx, my) from the game's routine, run on the side. The player's place is read
   from the machine as it was saved, so the squares asked for do not depend on what earlier calls
   left in memory. */
static int ask(int mx, int my, int px, int py, square_t *s) {
    if (!in_sandbox) { nes_sandbox_begin(); in_sandbox = 1; }
    /* the bank as the game's own switch leaves it: the PRG register, the half of the ROM through
       the CHR register, and the game's notes of both */
    mapper_select_bank16(GET_BLOCK_BANK);
    mapper_update_pointers();
    ram[0x507] = GET_BLOCK_BANK;
    ram[0x501] = (uint8_t)((ram[0x501] & 0xEF) | (GET_BLOCK_BANK & 0x10));
    ram[0x0D] = (uint8_t)(mx - px + 8);
    ram[0x3E1] = (uint8_t)(my - py + 6);
    asked_total++;
    if (!nes_sandbox_call(GET_BLOCK, 20000)) return 0;
    memcpy(s->tile, ram, 4);
    s->pal = ram[4] & 3;
    s->ok = 1;
    return 1;
}

/* the frame's context, read before the first call */
static int ctx_cam_x, ctx_cam_y, ctx_px, ctx_py;

/* the square at column c, row r of the picture's camera grid: the answer kept for it, or a new one */
static int fetch(int c, int r, square_t *s) {
    int mx = (ctx_cam_x + cam_dx + c) & 0xFF, my = (ctx_cam_y + cam_dy + r) & 0xFF;
    known_t *k = &known[my & 31][mx & 31];
    uint32_t now = (uint32_t)ppu.frame;
    int have = k->sq.ok && k->mx == mx && k->my == my && now - k->frame < 16;
    int due = have && k->frame != now && ((mx + 3 * my + now) & 7) == 0;      /* its turn to be asked again */
    if (have && !due) { *s = k->sq; return 1; }
    if (!ask(mx, my, ctx_px, ctx_py, s)) { k->sq.ok = 0; return 0; }
    k->sq = *s; k->mx = (uint8_t)mx; k->my = (uint8_t)my; k->frame = now;
    return 1;
}

/* the tile the PPU has in its nametables at a pixel of the scrolled plane */
static uint8_t nametable_tile(int px, int py) {
    px &= 511;
    py %= 480;
    if (py < 0) py += 480;
    int nt = (py >= 240 ? 2 : 0) | (px >= 256 ? 1 : 0);
    return ppu.vram[ppu.nt_offset[nt] + ((py % 240) / 8) * 32 + ((px & 255) / 8)];
}

/* does the PPU show this square where the map code says it is? */
static int square_on_screen(int c, int r, const square_t *s) {
    int px = scroll_x - fine_x + c * 16, py = scroll_y - fine_y + r * 16;
    return nametable_tile(px, py) == s->tile[0] && nametable_tile(px + 8, py) == s->tile[1] &&
           nametable_tile(px, py + 8) == s->tile[2] && nametable_tile(px + 8, py + 8) == s->tile[3];
}

/* a square with nothing drawn in it: four tiles without a pixel */
static int square_empty(const square_t *s) {
    unsigned base = (ppu.frame_ctrl & 0x10) ? 0x1000u : 0u;
    for (int q = 0; q < 4; q++) {
        unsigned addr = base + s->tile[q] * 16u;
        const uint8_t *p = mapper.chr_ptr[(addr >> 10) & 7] + (addr & 0x3FF);
        for (int i = 0; i < 16; i++)
            if (p[i]) return 0;
    }
    return 1;
}

/* four columns of the screen, the ones next to the bands and clear of most windows: how many of
   their squares the PPU shows as the map code names them (-1: the routine did not come back),
   and how many of those have something drawn in them */
static int probe(int *drawn) {
    static const int col[4] = { 1, 2, 13, 14 };
    static const int row[2] = { 1, 13 };
    int same = 0;
    *drawn = 0;
    for (int k = 0; k < 4; k++)
        for (int r = 0; r < 15; r++) {
            square_t s;
            if (!fetch(col[k], r, &s)) return -1;
            if (square_on_screen(col[k], r, &s)) { same++; *drawn += !square_empty(&s); }
        }
    for (int k = 0; k < 2; k++)             /* and two rows across, for a camera a row off */
        for (int c = 3; c <= 12; c++) {
            square_t s;
            if (!fetch(c, row[k], &s)) return -1;
            if (square_on_screen(c, row[k], &s)) { same++; *drawn += !square_empty(&s); }
        }
    return same;
}

/* the 8x8 sprite tile of a person at canvas (cx, top) into the bands and the strip */
static void decode(void);

static inline int inner(int sx, int line, int strip) {
    if (sx < (strip ? 8 : 0) || sx >= 256 || line < 0 || line >= 240) return 0;
    int c = (sx + fine_x) >> 4, r = (line + fine_y) >> 4;
    return c >= 1 && c <= 15 && r >= 1 && r <= 14;
}

int widescreen_inner(int sx, int line) {
    decode();
    if (!decoded || !(ppu.line_emph[line < 0 ? 0 : line > 239 ? 239 : line] & 0x08)) return sx >= 0 && sx < 256 && line >= 0 && line < 240;
    return inner(sx, line, !(ppu.line_emph[line] & 0x02));
}

static void person_tile(int cx, int top, unsigned tile, uint8_t attr, int strip) {
    unsigned base = (ppu.frame_ctrl & 0x08) ? 0x1000u : 0u;
    for (int j = 0; j < 8; j++) {
        int y = top + j, row = (attr & 0x80) ? 7 - j : j;       /* y: a canvas line */
        if (y < 0 || y >= WS_H) continue;
        unsigned addr = base + tile * 16 + (unsigned)row;
        const uint8_t *p = mapper.chr_ptr[(addr >> 10) & 7] + (addr & 0x3FF);
        for (int i = 0; i < 8; i++) {
            int x = cx + i, sx = x - WS_BAND, col = (attr & 0x40) ? 7 - i : i;
            if (x < 0 || x >= WS_W) continue;
            if (inner(sx, y - WS_VBAND, strip)) continue;         /* the PPU's own part of the picture */
            int value = ((p[0] >> (7 - col)) & 1) | (((p[8] >> (7 - col)) & 1) << 1);
            if (!value) continue;
            people[y][x] = (uint8_t)(ppu.palette[16 + (attr & 3) * 4 + value]);
            people_src[y][x] = HD_PACK(HD_TILE(ppu_hd_tile_of((uint16_t)addr)), row, col, attr & 3, value) |
                               ((attr & 0x40) ? HD_HFLIP : 0) | ((attr & 0x80) ? HD_VFLIP : 0) | ((attr & 0x20) ? HD_BEHIND : 0);
        }
    }
}

/* everyone standing in a band (or the strip), from the game's object table */
static void draw_people(int strip) {
    const uint8_t *w = cart.wram;                         /* $6000 on */
    int cam_px = (ram[0xEC] + cam_dx) * 16 + fine_x, cam_py = (ram[0xED] + cam_dy) * 16 + fine_y;
    people_drawn = 0;
    int last = 0, cleared = 0;
    while (last + 1 < OBJECTS && w[0x1020 + last + 1] != 0xFF) last++;
    for (int i = last; i >= 1; i--) {                     /* the first in the table end up on top */
        if (w[0x1020 + i] & 0x90) continue;               /* hidden */
        if ((w[0x1140 + i] & 0xE0) != ram[0x46]) continue;   /* on another floor */
        int px = ((w[0x0FA0 + i] << 8) | w[0x1100 + i]) >> 4, py = ((w[0x0FC0 + i] << 8) | w[0x1120 + i]) >> 4;
        int sx = px - cam_px, sy = py - cam_py - ram[0x7BC] + 1;      /* a sprite shows a line below its y */
        if (sx <= -WS_BAND - 16 || sx >= 256 + WS_BAND || sy <= -WS_VBAND - 16 || sy >= 240 + WS_VBAND) continue;
        if (inner(sx, sy, strip) && inner(sx + 15, sy, strip) && inner(sx, sy + 15, strip) && inner(sx + 15, sy + 15, strip)) continue;   /* wholly the PPU's */
        int kind = w[0x0FE0 + i] & 0x0F, facing = w[0x1000 + i] & 3;
        int step = (w[0x10E0 + i] & 0x80) && (ram[0x526] & 0x80);    /* the second walking frame */
        const uint8_t *def = w + 0x1200 + kind * 64 + facing * 16 + (step ? 8 : 0);
        if (!cleared) { memset(people, 0xFF, sizeof people); cleared = 1; }
        for (int q = 0; q < 4; q++)
            person_tile(sx + WS_BAND + (q & 1) * 8, sy + WS_VBAND + (q >> 1) * 8, def[q * 2], def[q * 2 + 1], strip);
        people_drawn++;
    }
}

/* once per frame shown: the scroll, the squares of the bands, and whether the screen is the map */
/* Where the picture stood in the map on the last decoded frame, in pixels, and the PPU's scroll
   then. From one frame to the next both move by a pixel or two, together: a frame in which the
   map's position moves by more while the scroll does not has the bands a square off (the camera
   square was taken wrong). widescreen_jumps counts them. */
static int last_ok, last_wx, last_wy, last_sx, last_sy;
static uint64_t last_frame;
long widescreen_jumps;

static int wrapped(int d, int size) { d %= size; if (d < -size / 2) d += size; if (d >= size / 2) d -= size; return d; }

/* does this place continue the last frame's? (within three pixels each way) */
static int continues(int wx, int wy) {
    if (!last_ok || ppu.frame - last_frame > 2) return 0;
    int dx = wrapped(wx - last_wx, 4096), dy = wrapped(wy - last_wy, 4096);
    return dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3;
}

static void note_position(void) {
    int wx = ((ctx_cam_x + cam_dx) & 0xFF) * 16 + fine_x, wy = ((ctx_cam_y + cam_dy) & 0xFF) * 16 + fine_y;
    if (decoded && last_ok && ppu.frame - last_frame <= 2 && ppu.frame != last_frame) {
        int sx = wrapped(scroll_x - last_sx, 512), sy = wrapped(scroll_y - last_sy, 480);
        if (sx >= -3 && sx <= 3 && sy >= -3 && sy <= 3 && !continues(wx, wy)) widescreen_jumps++;
    }
    last_ok = decoded; last_wx = wx; last_wy = wy; last_sx = scroll_x; last_sy = scroll_y; last_frame = ppu.frame;
}

static void decode(void) {
    if (stamped && stamp_frame == ppu.frame && stamp_cycles == cpu.cycles) return;
    stamped = 1; stamp_frame = ppu.frame; stamp_cycles = cpu.cycles;
    decoded = 0;
    people_drawn = 0;
    if ((cart.mapper != 1 && cart.mapper != 4) || cart.prg_size != 0x80000) return;
    uint16_t t = ppu.frame_t;
    scroll_x = ((t >> 10) & 1) * 256 + (t & 31) * 8 + ppu.frame_x;
    scroll_y = ((t >> 11) & 1) * 240 + ((t >> 5) & 31) * 8 + ((t >> 12) & 7);
    fine_x = scroll_x & 15;
    fine_y = scroll_y & 15;
    ctx_cam_x = ram[0xEC]; ctx_cam_y = ram[0xED];
    ctx_px = player_x(); ctx_py = player_y();

    /* The camera's square in memory and the scroll the PPU was given do not always change on the
       same frame, so the picture's camera may be a square off in any direction. All five are
       compared with the screen. A room is mostly floor: a camera one square off still matches
       most of it, so the first good match is not good enough. The one that matches most wins,
       except that the one continuing the last frame's scroll wins when it matches about as well
       (on a plain of one tile all of them match). A third of the squares has to match: the rest
       may be under windows. */
    static const int8_t tries[5][2] = { { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    int same[5], drawn[5], best = -1;
    for (int k = 0; k < 5; k++) {
        cam_dx = tries[k][0]; cam_dy = tries[k][1];
        same[k] = probe(&drawn[k]);
        if (same[k] < 0) { best = -2; break; }
    }
    for (int k = 0; k < 5 && best != -2; k++)
        if (same[k] >= PROBED / 3 && drawn[k] >= 4 && (best < 0 || same[k] > same[best])) best = k;
    for (int k = 0; k < 5 && best >= 0; k++) {
        int wx = ((ctx_cam_x + tries[k][0]) & 0xFF) * 16 + fine_x, wy = ((ctx_cam_y + tries[k][1]) & 0xFF) * 16 + fine_y;
        if (same[k] >= PROBED / 3 && drawn[k] >= 4 && same[k] + PROBED / 16 >= same[best] && continues(wx, wy)) { best = k; break; }
    }
    if (best >= 0) {
        int failed = 0;
        cam_dx = tries[best][0]; cam_dy = tries[best][1];
        for (int r = ROW0; r < ROW0 + ROWS && !failed; r++)
            for (int c = COL0; c < COL0 + COLS && !failed; c++) {
                if (c >= 2 && c <= 15 && r >= 1 && r <= 14) continue;     /* the PPU's to show (the second column's first pixels may be blanked) */
                if (!fetch(c, r, &squares[r - ROW0][c - COL0])) failed = 1;
            }
        decoded = !failed;
    }
    if (in_sandbox) { nes_sandbox_end(); in_sandbox = 0; }
    if (decoded) draw_people(!(ppu.mask & 0x02));
    note_position();
}

int widescreen_bad_box[4];        /* of the last check: the first and last column, the first and last row that differ */
long widescreen_better;           /* checks in which a neighbouring camera square fits the screen clearly better */
long widescreen_still_edge_bad;   /* checks with the screen at rest and a difference in its first column or row */

int widescreen_check(int *cells) {
    int bad = 0, n = 0, failed = 0, edge_still = 0;
    widescreen_bad_box[0] = widescreen_bad_box[2] = 99; widescreen_bad_box[1] = widescreen_bad_box[3] = -1;
    decode();
    if (cells) *cells = 0;
    if (!decoded) return -1;
    for (int r = 0; r < 15 && !failed; r++)
        for (int c = 0; c <= 15 && !failed; c++) {
            square_t s;
            if (!fetch(c, r, &s)) { failed = 1; break; }
            n++;
            if (!square_on_screen(c, r, &s)) {
                bad++;
                if (c < widescreen_bad_box[0]) widescreen_bad_box[0] = c;
                if (c > widescreen_bad_box[1]) widescreen_bad_box[1] = c;
                if (r < widescreen_bad_box[2]) widescreen_bad_box[2] = r;
                if (r > widescreen_bad_box[3]) widescreen_bad_box[3] = r;
                if ((c == 0 || r == 0) && !fine_x && !fine_y) edge_still = 1;
            }
        }
    /* a second opinion on the camera's square, from every square of the screen: does one of the
       neighbouring squares fit clearly better (by more than eight squares)? */
    if (!failed) {
        int keep_dx = cam_dx, keep_dy = cam_dy, better = 0;
        static const int8_t around[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (int k = 0; k < 4 && !failed; k++) {
            int same = 0;
            cam_dx = keep_dx + around[k][0]; cam_dy = keep_dy + around[k][1];
            for (int r = 1; r < 14 && !failed; r++)
                for (int c = 1; c <= 14 && !failed; c++) {
                    square_t s;
                    if (!fetch(c, r, &s)) { failed = 1; break; }
                    same += square_on_screen(c, r, &s);
                }
            if (!failed && same > (n - bad) + 8) better = 1;
        }
        cam_dx = keep_dx; cam_dy = keep_dy;
        widescreen_better += better && !failed;
    }
    if (in_sandbox) { nes_sandbox_end(); in_sandbox = 0; }
    if (cells) *cells = n;
    widescreen_still_edge_bad += edge_still && !failed;
    return failed ? -1 : bad;
}

int widescreen_people(void) { decode(); return decoded ? people_drawn : 0; }
unsigned long widescreen_calls(void) { return asked_total; }

/* the map's pixel at canvas column cx (0 = the left band's first), line y: its palette index, its
   2-bit value and its source for the remaster */
static inline uint8_t map_pixel(int cx, int y, uint8_t gray, uint32_t palset, uint32_t *src, int *value_out) {
    int wx = cx - WS_BAND + fine_x - COL0 * 16, wy = y - WS_VBAND + fine_y - ROW0 * 16;      /* pixels from the grid's corner */
    const square_t *s = &squares[wy >> 4][wx >> 4];
    unsigned tile = s->tile[((wy >> 3) & 1) * 2 + ((wx >> 3) & 1)];
    unsigned addr = ((ppu.frame_ctrl & 0x10) ? 0x1000u : 0u) + tile * 16 + (wy & 7);
    const uint8_t *p = mapper.chr_ptr[(addr >> 10) & 7] + (addr & 0x3FF);
    int bit = 7 - (wx & 7), value = ((p[0] >> bit) & 1) | (((p[8] >> bit) & 1) << 1);
    if (src) {
        uint32_t t = ppu_hd_tile_of((uint16_t)addr);
        *src = HD_PACK(HD_TILE(t), wy & 7, wx & 7, s->pal, value) | (palset << 28);
    }
    *value_out = value;
    return (uint8_t)(ppu.palette[value ? s->pal * 4 + value : 0] & gray);
}

int widescreen_render_hd(uint8_t *out, uint8_t *emph, uint32_t *hbg, uint32_t *hspr, int x, int y, int w, int h) {
    decode();
    for (int j = 0; j < h; j++) {
        int cy = y + j, line = cy - WS_VBAND;                 /* the canvas line, and the PPU's (beyond the picture above and below) */
        uint8_t *o = out + (size_t)j * w;
        uint32_t *ob = hbg ? hbg + (size_t)j * w : NULL, *os = hspr ? hspr + (size_t)j * w : NULL;
        if (cy < 0 || cy >= WS_H) {
            memset(o, 0x0F, (size_t)w);
            if (ob) memset(ob, 0, (size_t)w * sizeof *ob);
            if (os) memset(os, 0, (size_t)w * sizeof *os);
            emph[j] = 0;
            continue;
        }
        int in_frame = line >= 0 && line < 240, near = line < 0 ? 0 : line > 239 ? 239 : line;
        uint8_t mask = ppu.line_emph[near], gray = (mask & 1) ? 0x30 : 0x3F;
        int map_line = decoded && (mask & 0x08);              /* the background is on for this line */
        int strip = map_line && !(mask & 0x02);               /* and the PPU blanks its first 8 pixels */
        int people_line = map_line && (mask & 0x10) && people_drawn;
        uint32_t palset = hbg ? HD_PALSET(ppu.hd_bg[near * 256 + 128]) : 0;
        emph[j] = in_frame || decoded ? mask : 0;
        for (int i = 0; i < w; i++) {
            int cx = x + i, sx = cx - WS_BAND;
            uint32_t b = 0, s = 0;
            uint8_t v;
            if (cx < 0 || cx >= WS_W) v = 0x0F;
            else if (in_frame && sx >= 0 && sx < 256 && (!map_line || inner(sx, line, strip))) {
                v = ppu.fb[line * 256 + sx];
                if (hbg) { b = ppu.hd_bg[line * 256 + sx]; s = ppu.hd_spr[line * 256 + sx]; }
            } else if (map_line) {
                int value;
                v = map_pixel(cx, cy, gray, palset, hbg ? &b : NULL, &value);
                if (people_line && people[cy][cx] != 0xFF) {
                    s = people_src[cy][cx] | (palset << 28);
                    if (!value || !(s & HD_BEHIND)) v = people[cy][cx] & gray;
                }
            } else v = decoded && in_frame ? (uint8_t)(ppu.palette[0] & gray) : 0x0F;
            o[i] = v;
            if (ob) ob[i] = b;
            if (os) os[i] = s;
        }
    }
    return decoded;
}

int widescreen_render(uint8_t *out, uint8_t *emph, int x, int y, int w, int h) {
    return widescreen_render_hd(out, emph, NULL, NULL, x, y, w, h);
}

int widescreen_visible_width(int src_h, double pixel_aspect, double target_aspect) {
    int w = (int)(src_h * target_aspect / pixel_aspect + 0.5);
    w &= ~1;
    return w > WS_W ? WS_W : w < 256 ? 256 : w;
}
