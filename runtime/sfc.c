/*
 * sfc.c - the 16-bit textures (sfc.h).
 *
 * Map squares. The game's maps are made of 16x16 squares, each four tiles and one of the four
 * background palettes, and they sit in the name tables on the 16-pixel grid the attribute table
 * has. The PPU notes for every background pixel which name table cell its tile came from
 * (ppu.hd_cell); the widescreen canvas does the same for the map it draws around the picture.
 * So every pixel names a square (a name table's, or one of the canvas's) and a place in it. A
 * square is known by what its four tiles hold in the CHR RAM, wherever the game has loaded them,
 * and by its palette's number: the hash of the four tiles' hashes and that number is the key the
 * pack is asked with, once per square and frame. The game uses some squares (grass, water, trees)
 * both on the overworld and in its towns, where the Super Famicom games draw them differently, so
 * the key also says which of the two the player is on ($41, bit 7: towns and everything indoors).
 * And one square may be a town's road and a castle's floor: art can be bound to the kind of place
 * it was made for. The game keeps the kind of the map it has loaded at $28 ($0D in Burland's
 * castle, $0A in its town, $0E in Izmit, $1D in the cave between them, 0 on the overworld; it
 * changes while the screen is black between two maps, and ROM hacks do not move it); a square is
 * looked up for this kind first, then for any.
 *
 * Windows. The game draws its windows into the same name tables, tile by tile, so a window may
 * cover part of a square. Its four tiles are then no square the pack knows; but the square that
 * was there before is remembered, and the quarters that still hold its tiles are still drawn
 * from it. (An empty tile never counts: a window's inside is empty tiles too.) And a window's
 * inside is not the map either, though four of its empty tiles make the same square as the
 * dark beyond a cave's rock or the black round a room, which the pack has art for: an empty
 * tile that lies between a window's left and right edge, with nothing but what a window holds
 * between it and one of them, is left out (the frame's tiles have the same numbers everywhere).
 * An empty square shows nothing but the backdrop color: the art the pack has for it is drawn only
 * while the backdrop is the color that art was made for (black), not on the title's pale sky.
 *
 * The windows themselves. The pack may bring a Super Famicom window: its fill, the color of its
 * line and the eight pieces of its frame. The game's windows are then drawn as that one: the
 * frame's tiles from the pieces (the game keeps its frame in ten tiles, $83-$8C: the edges, the
 * corners, the left edge with the mark of a thing worn and the top's end before a title, and the
 * Super Famicom frame is the same line in the same place), the inside in the fill, and the
 * letters, which stay the game's own, in the line's color. A tile belongs to a window when it is
 * one of the frame's or lies inside one, as above; letters that stand free on the screen are
 * left alone. The title and a few other screens load other tiles where the frame is kept: the
 * pack says what the frame's ten tiles hash to, and nothing of this is done while they do not.
 * The letters' color follows the game's (it fades them, and writes them in blue in a few dark
 * places): it is scaled as a square's colors are, and the fill by as much as the brightest
 * channel, so it fades with them and keeps its tone.
 *
 * People. Everyone on a map is four 8x8 sprites in a square. The frame's sprites are taken from
 * the PPU's sprite memory and put together again: four that form a 16x16 square are one figure,
 * known by the picture they make (the hash of its 256 two-bit pixels) and, where several people
 * share a picture, by their colors. A figure the pack knows is drawn from the pack, at the same
 * feet, and its NES pixels are left out of the frame; the people the canvas adds beside the
 * picture are done the same way.
 *
 * Colors. The pack's pixels are final colors, but the game still fades, flashes and changes to
 * night through its palette. Each square and figure therefore carries the NES colors it was
 * made for; while the game's palette holds other colors for it, its pixels are scaled, channel
 * by channel, by how the two sets of colors compare: black when the game has faded to black,
 * dark and blue at night. A square's three colors count by how much of the NES square is drawn
 * in each (the pack says): a wall that is all grey stays as it is in a place whose third color,
 * which the wall does not use, is another one. Where one color of a square is another thing
 * from level to level (the sky round a statue's rim, black where the same rim stands in the
 * dark) no scaling can follow it: the pack leaves those pixels clear, and the NES's own stay.
 *
 * Black. The Super Famicom art's black is pure black: the dark round a room, an empty screen
 * (the squares of nothing but backdrop are art too). Whatever the pack does not draw comes from
 * the Pixel Remaster's colors, whose black is a cool near-black; side by side the two show as
 * blocks and bands (round a battle's monsters, round letters that stand free, at the canvas's
 * sides). So here black is one: where the backdrop shows and the game has it black, and where a
 * square of the pack leaves a pixel to the NES that the NES draws black, it is pure black.
 *
 * Pictures laid over the map. A cloud one walks on is not a square repeated: the Super Famicom
 * games paint clouds as one wide picture. A square of the pack may therefore hand pixels to a
 * picture that is thought laid over the whole map, repeating: such a pixel shows the picture's
 * pixel at the place on the map where it lies (widescreen_origin says where the screen is), so
 * the picture runs on unbroken from square to square, and the square only says where it shows
 * (the cloud's own outline against the sky).
 *
 * Tall art. A Super Famicom tree is taller than a map square: its crown lies over the square
 * above, and over anyone standing there. A square of the pack may therefore have a top, sixteen
 * rows of pixels that belong above it (clear where the square above shows through). After the
 * squares and the figures are drawn, every pixel asks the square below its own for a top.
 *
 * Pack: "DW4S", version 4 (1: no tops; 2: no pictures; 3: no window), the number of squares, the number of
 * figures, then
 *   per square: key (64 bits), the four NES colors it was made for, the number of animation
 *     frames, the game frames each one lasts, a flags byte (bit 0: a top follows the frames; its
 *     high four bits: how much of the NES square is in its third color, 0 to 15), a byte with the
 *     same for the first color (low four bits) and the second (high; all three 0 in an older
 *     pack: they count alike), then the frames, each 256 pixels (ARGB, row by row; alpha 0
 *     where the NES's own pixel stays; alpha 1 where a picture shows, its number in the low
 *     24 bits), then the top (256 pixels, alpha 0 where it is clear);
 *   per figure: key, the twelve NES colors of its four quarters' palettes, where its picture
 *     starts from the NES figure's top left corner (two signed bytes), its width and height,
 *     then the pixels (ARGB; alpha 0 where it is clear);
 *   from version 3: the number of pictures, and per picture its width and height (16 bits
 *     each) and its pixels (ARGB; alpha 0 where the NES's own pixel stays);
 *   from version 4: the window: the key of the game's frame (64 bits: the hash of the hashes of
 *     its ten tiles), the color of its line and letters, and 24x24 pixels: the frame's corners
 *     and edges, 8x8 each, round a piece of the fill.
 * All little-endian.
 * After all that a pack may hold the music (runtime/sfcmusic.c reads it from the file's end); this
 * file does not look at it.
 */
#include "sfc.h"
#include "nes.h"
#include "palette.h"
#include "remaster.h"
#include "widescreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 512                       /* tiles of the CHR RAM */
#define NT_SQUARES (4 * 15 * 16)        /* the map squares of four name tables */
#define BAND_SQUARES 1024               /* the canvas's own (HD_CELL_BAND numbers) */
#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

typedef struct { uint64_t key; uint8_t ref[4], frames, ticks, w[3]; const uint32_t *px, *top; } square_t;
static int any_tops;
typedef struct { uint64_t key; uint8_t ref[12]; int8_t dx, dy; uint8_t w, h; const uint32_t *px; int next; } figure_t;

typedef struct { uint32_t w, h; const uint32_t *px; } picture_t;
static square_t *squares;
static figure_t *figures;
static picture_t *pictures;             /* laid over the whole map */
static uint32_t n_squares, n_figures, n_pictures, sq_cap, fig_cap;
static uint64_t win_key;                /* the pack's window: 0 when it has none */
static uint32_t win_ink, win_fill, win_px[24 * 24];
static int win_on;                      /* and whether the game's frame is loaded on this frame */
static int32_t *sq_table, *fig_table;   /* open-addressed: index + 1, 0 = empty (figures: the first of a key's chain) */
static uint32_t *pixels;                /* every square's and figure's, one block */
static int ready;
static uint32_t nes_rgb[512];

/* what each slot of the CHR RAM holds */
static uint8_t slot_tile[SLOTS][16], slot_known[SLOTS], slot_blank[SLOTS];
static uint64_t slot_hash[SLOTS];

/* a square's place on this frame: the pack's square for it, which quarters are its own, its tint */
typedef struct { uint32_t gen; const square_t *sq; uint8_t quarters, pal, tinted, blank; uint16_t tint[3]; } place_t;
typedef struct { const square_t *sq; uint64_t th[4]; uint8_t pal; } seen_t;
static place_t nt_place[NT_SQUARES], band_place[BAND_SQUARES];
static seen_t nt_seen[NT_SQUARES];
static uint32_t gen;
#define OVERWORLD_MIX 0x9E3779B97F4A7C15ULL    /* into the keys of the overworld's squares */
static uint64_t scene_mix;
#define PLACE_MIX 0xD6E8FEB86659FD93ULL       /* times the map's kind + 1: into the keys of art bound to a kind of place */
static uint64_t map_mix;

static int stat_known, stat_unknown, stat_people, stat_people_unknown;
#define MISSED 64
static uint64_t missed_sq[MISSED], missed_fig[MISSED];
static int n_missed_sq, n_missed_fig;

static void missed(uint64_t *list, int *n, uint64_t key) {
    for (int i = 0; i < *n; i++)
        if (list[i] == key) return;
    if (*n < MISSED) list[(*n)++] = key;
}

void sfc_unknown(uint64_t *keys, uint64_t *people, int max, int n[2]) {
    n[0] = n_missed_sq < max ? n_missed_sq : max;
    n[1] = n_missed_fig < max ? n_missed_fig : max;
    memcpy(keys, missed_sq, (size_t)n[0] * sizeof *keys);
    memcpy(people, missed_fig, (size_t)n[1] * sizeof *people);
}

static uint64_t fnv(uint64_t h, const uint8_t *p, int n) {
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= FNV_PRIME; }
    return h;
}

static uint64_t square_key(const uint64_t th[4], int pal) {
    uint64_t h = FNV_OFFSET;
    for (int q = 0; q < 4; q++)
        for (int k = 0; k < 8; k++) { h ^= (th[q] >> (8 * k)) & 0xFF; h *= FNV_PRIME; }
    h ^= (uint64_t)pal; h *= FNV_PRIME;
    return h ? h : 1;
}

static const square_t *find_square(uint64_t key) {
    if (!sq_cap) return NULL;
    for (uint32_t i = (uint32_t)(key % sq_cap);; i = (i + 1) % sq_cap) {
        if (!sq_table[i]) return NULL;
        if (squares[sq_table[i] - 1].key == key) return &squares[sq_table[i] - 1];
    }
}

static const figure_t *find_figure(uint64_t key) {
    if (!fig_cap) return NULL;
    for (uint32_t i = (uint32_t)(key % fig_cap);; i = (i + 1) % fig_cap) {
        if (!fig_table[i]) return NULL;
        if (figures[fig_table[i] - 1].key == key) return &figures[fig_table[i] - 1];
    }
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

void sfc_forget(void) {
    free(squares); free(figures); free(pictures); free(sq_table); free(fig_table); free(pixels);
    squares = NULL; figures = NULL; pictures = NULL; sq_table = fig_table = NULL; pixels = NULL;
    n_squares = n_figures = n_pictures = sq_cap = fig_cap = 0;
    win_key = 0; win_on = 0;
    ready = 0;
    any_tops = 0;
}

int sfc_ready(void) { return ready; }

int sfc_init(const char *pack_path) {
    sfc_forget();
    nes_palette_build(nes_rgb);
    memset(slot_known, 0, sizeof slot_known);
    memset(nt_place, 0, sizeof nt_place); memset(band_place, 0, sizeof band_place); memset(nt_seen, 0, sizeof nt_seen);
    gen = 1;
    FILE *f = pack_path ? nes_fopen(pack_path, "rb") : NULL;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = len >= 16 && len < (64L << 20) ? malloc((size_t)len) : NULL;
    if (!d || fread(d, 1, (size_t)len, f) != (size_t)len || memcmp(d, "DW4S", 4) || rd32(d + 4) < 1 || rd32(d + 4) > 4) { free(d); fclose(f); return 0; }
    fclose(f);
    uint32_t ns = rd32(d + 8), nf = rd32(d + 12);
    if (ns > 65536 || nf > 65536) { free(d); return 0; }
    squares = calloc(ns + 1, sizeof *squares);
    figures = calloc(nf + 1, sizeof *figures);
    pixels = malloc((size_t)len);                       /* more than the pixels take */
    sq_cap = ns * 2 + 16; fig_cap = nf * 2 + 16;
    sq_table = calloc(sq_cap, sizeof *sq_table);
    fig_table = calloc(fig_cap, sizeof *fig_table);
    if (!squares || !figures || !pixels || !sq_table || !fig_table) { free(d); sfc_forget(); return 0; }
    size_t o = 16, used = 0;
    int bad = 0;
    for (uint32_t i = 0; i < ns && !bad; i++) {
        if (o + 16 > (size_t)len) { bad = 1; break; }
        square_t *s = &squares[n_squares];
        s->key = rd64(d + o);
        memcpy(s->ref, d + o + 8, 4);
        s->w[0] = d[o + 15] & 15; s->w[1] = d[o + 15] >> 4; s->w[2] = d[o + 14] >> 4;
        if (!(s->w[0] | s->w[1] | s->w[2])) s->w[0] = s->w[1] = s->w[2] = 1;
        s->frames = d[o + 12] ? d[o + 12] : 1; s->ticks = d[o + 13] ? d[o + 13] : 1;
        int has_top = d[o + 14] & 1;
        o += 16;
        size_t n = (size_t)s->frames * 256;
        if (o + (n + (has_top ? 256 : 0)) * 4 > (size_t)len) { bad = 1; break; }
        s->px = pixels + used;
        for (size_t k = 0; k < n; k++) pixels[used++] = rd32(d + o + k * 4);
        o += n * 4;
        s->top = NULL;
        if (has_top) {
            s->top = pixels + used;
            for (size_t k = 0; k < 256; k++) pixels[used++] = rd32(d + o + k * 4);
            o += 256 * 4;
            any_tops = 1;
        }
        uint32_t t = (uint32_t)(s->key % sq_cap);
        while (sq_table[t] && squares[sq_table[t] - 1].key != s->key) t = (t + 1) % sq_cap;
        if (!sq_table[t]) { sq_table[t] = (int32_t)n_squares + 1; n_squares++; }      /* a key given twice: the first stays */
    }
    for (uint32_t i = 0; i < nf && !bad; i++) {
        if (o + 24 > (size_t)len) { bad = 1; break; }
        figure_t *g = &figures[n_figures];
        g->key = rd64(d + o);
        memcpy(g->ref, d + o + 8, 12);
        g->dx = (int8_t)d[o + 20]; g->dy = (int8_t)d[o + 21]; g->w = d[o + 22]; g->h = d[o + 23];
        o += 24;
        size_t n = (size_t)g->w * g->h;
        if (!n || o + n * 4 > (size_t)len) { bad = 1; break; }
        g->px = pixels + used;
        for (size_t k = 0; k < n; k++) pixels[used++] = rd32(d + o + k * 4);
        o += n * 4;
        g->next = 0;
        uint32_t t = (uint32_t)(g->key % fig_cap);
        while (fig_table[t] && figures[fig_table[t] - 1].key != g->key) t = (t + 1) % fig_cap;
        if (!fig_table[t]) fig_table[t] = (int32_t)n_figures + 1;
        else {                                          /* the same picture in other colors: chained */
            figure_t *h = &figures[fig_table[t] - 1];
            while (h->next) h = &figures[h->next - 1];
            h->next = (int)n_figures + 1;
        }
        n_figures++;
    }
    if (!bad && rd32(d + 4) >= 3) {
        uint32_t np = o + 4 <= (size_t)len ? rd32(d + o) : 0;
        o += 4;
        if (np > 256 || !(pictures = calloc(np + 1, sizeof *pictures))) bad = 1;
        for (uint32_t i = 0; i < np && !bad; i++) {
            if (o + 4 > (size_t)len) { bad = 1; break; }
            picture_t *pc = &pictures[n_pictures];
            pc->w = d[o] | (d[o + 1] << 8); pc->h = d[o + 2] | (d[o + 3] << 8);
            o += 4;
            size_t n = (size_t)pc->w * pc->h;
            if (!n || o + n * 4 > (size_t)len) { bad = 1; break; }
            pc->px = pixels + used;
            for (size_t k = 0; k < n; k++) pixels[used++] = rd32(d + o + k * 4);
            o += n * 4;
            n_pictures++;
        }
    }
    if (!bad && rd32(d + 4) >= 4) {
        if (o + 12 + sizeof win_px > (size_t)len) bad = 1;
        else {
            win_key = rd64(d + o); win_ink = rd32(d + o + 8);
            for (int k = 0; k < 24 * 24; k++) win_px[k] = rd32(d + o + 12 + k * 4);
            win_fill = win_px[12 * 24 + 12];
        }
    }
    free(d);
    if (bad || !(n_squares + n_figures)) { sfc_forget(); return 0; }
    ready = 1;
    printf("16-bit textures: %u map squares and %u figures%s from %s\n", n_squares, n_figures, win_key ? ", the windows" : "", pack_path);
    return (int)(n_squares + n_figures);
}

void sfc_stats(int *known, int *unknown, int *people, int *people_unknown) {
    if (known) *known = stat_known;
    if (unknown) *unknown = stat_unknown;
    if (people) *people = stat_people;
    if (people_unknown) *people_unknown = stat_people_unknown;
}

static void refresh_slots(void) {
    for (int i = 0; i < SLOTS; i++) {
        const uint8_t *t = cart.chr_ram + i * 16;
        if (slot_known[i] && !memcmp(slot_tile[i], t, 16)) continue;
        memcpy(slot_tile[i], t, 16);
        slot_known[i] = 1;
        uint64_t h = fnv(FNV_OFFSET, t, 16);
        slot_hash[i] = h ? h : 1;
        int blank = 1;
        for (int k = 0; k < 16; k++) blank &= !t[k];
        slot_blank[i] = (uint8_t)blank;
    }
}

/* the slot behind a tile number of the background's (or the sprites') half; SLOTS when it is none */
static int slot_of(int tile, int sprites) {
    unsigned base = (ppu.frame_ctrl & (sprites ? 0x08 : 0x10)) ? 0x1000u : 0u;
    unsigned s = HD_TILE(ppu_hd_tile_of((uint16_t)(base + (unsigned)tile * 16)));
    return s < SLOTS && !cart.chr ? (int)s : SLOTS;
}

/* the tile at column x, row y of the four name tables (64 x 60, going round) */
static uint8_t nt_tile(int x, int y) {
    x &= 63;
    y = ((y % 60) + 60) % 60;
    return ppu.vram[ppu.nt_offset[(y >= 30 ? 2 : 0) | (x >= 32 ? 1 : 0)] + (y % 30) * 32 + (x & 31)];
}

/* Is the empty tile at (x, y) of the name tables a window's inside? Along its row the frame's left edge
   ($84, its corners $86 $87, $83 where a line starts from it) comes first to the left, or its right edge
   ($88, the corners $89 $8B) first to the right, past nothing but what a window holds: letters and empty
   tiles (all below $83) and the frame's top and bottom ($85 $8A, $8C before a title). The map's own tiles
   and another window's far edge end the search. */
static int in_window(int x, int y) {
    if (win_key && !win_on) return 0;   /* (the pack knows the frame, and these tiles are not it) */
    for (int dir = -1; dir <= 1; dir += 2)
        for (int k = 1; k < 32; k++) {
            uint8_t t = nt_tile(x + dir * k, y);
            if (t < 0x83 || t == 0x85 || t == 0x8A || t == 0x8C) continue;
            if (dir < 0 ? (t == 0x83 || t == 0x84 || t == 0x86 || t == 0x87) : (t == 0x88 || t == 0x89 || t == 0x8B)) return 1;
            break;
        }
    return 0;
}

/* Are the ten tiles of the game's frame loaded where it keeps them? */
static int frame_loaded(void) {
    uint64_t h = FNV_OFFSET;
    for (int t = 0x83; t <= 0x8C; t++) {
        int s = slot_of(t, 0);
        if (s >= SLOTS) return 0;
        for (int k = 0; k < 8; k++) { h ^= (slot_hash[s] >> (8 * k)) & 0xFF; h *= FNV_PRIME; }
    }
    return (h ? h : 1) == win_key;
}

/* a tile of the name tables on this frame, as part of a window: what it is (0: none of one; 1: a letter or the
   empty inside; else the frame's tile), and the window's two colors as the game's palette has them now */
typedef struct { uint32_t gen, ink, fill; uint8_t tile, pal, tinted, cols; } wtile_t;
static wtile_t win_tile[4 * 960];
/* the frame's tiles $83-$8C: which of the nine pieces (three rows of three) each is drawn from */
static const uint8_t frame_piece[10] = { 3, 3, 1, 0, 6, 5, 2, 7, 8, 1 };

static void resolve_window(wtile_t *w, int nt, int row, int col) {
    const uint8_t *t = ppu.vram + ppu.nt_offset[nt];
    uint8_t n = t[row * 32 + col];
    w->gen = gen; w->tile = 0; w->tinted = 0;
    if (n > 0x8C) return;
    if (n < 0x83 && !in_window((nt & 1) * 32 + col, (nt >> 1) * 30 + row)) return;
    w->tile = n < 0x83 ? 1 : n;
    w->pal = (t[0x3C0 + (row >> 2) * 8 + (col >> 2)] >> (((row & 2) ? 4 : 0) + ((col & 2) ? 2 : 0))) & 3;
    w->cols = 0;
    if (n == 0x8C) {                    /* the top's end: its line goes as far as the game draws it */
        int s = slot_of(n, 0);
        for (int k = 0; k < 16 && s < SLOTS; k++) w->cols |= slot_tile[s][k];
    }
}

/* (tx, ty): where the square's first tile lies in the name tables; tx < 0 for a square of the canvas's map */
static void resolve(place_t *p, const uint8_t tile[4], int pal, seen_t *seen, int tx, int ty) {
    uint64_t th[4];
    int slot[4], ok = 1, inside = 0;
    p->gen = gen; p->sq = NULL; p->quarters = 0; p->pal = (uint8_t)pal; p->tinted = 0; p->blank = 0;
    for (int q = 0; q < 4; q++) {
        slot[q] = slot_of(tile[q], 0);
        if (slot[q] >= SLOTS) { ok = 0; break; }
        th[q] = slot_hash[slot[q]];
    }
    if (!ok) { stat_unknown++; return; }
    for (int q = 0; q < 4 && tx >= 0; q++)
        if (slot_blank[slot[q]] && in_window(tx + (q & 1), ty + (q >> 1))) inside |= 1 << q;
    if (inside == 15) return;           /* a window's inside: nothing of the map (what was here stays remembered) */
    int blank = 1;
    for (int q = 0; q < 4; q++) blank &= slot_blank[slot[q]];
    p->blank = (uint8_t)blank;
    p->sq = find_square(square_key(th, pal) ^ scene_mix ^ map_mix);
    if (!p->sq) p->sq = find_square(square_key(th, pal) ^ scene_mix);
    if (!p->sq) p->sq = find_square(square_key(th, pal) ^ scene_mix ^ OVERWORLD_MIX);   /* (between the two, for a frame or two, memory says one and the screen still shows the other) */
    if (p->sq) {
        p->quarters = (uint8_t)(15 & ~inside);
        stat_known++;
        if (seen) { seen->sq = p->sq; memcpy(seen->th, th, sizeof th); seen->pal = (uint8_t)pal; }
        return;
    }
    if (seen && seen->sq) {       /* part of the square that was here, under a window? */
        int m = 0;
        for (int q = 0; q < 4; q++)
            if (th[q] == seen->th[q] && !slot_blank[slot[q]]) m |= 1 << q;
        if (m) { p->sq = seen->sq; p->quarters = (uint8_t)m; p->pal = seen->pal; return; }
        seen->sq = NULL;
    }
    if (!blank) {
        stat_unknown++;
        /* (listed only where the canvas drew it from the map: in the name tables it may be a window's) */
        if (!seen) missed(missed_sq, &n_missed_sq, square_key(th, pal) ^ scene_mix);
    }
}

static void resolve_nt(place_t *p, int n) {
    int nt = n / 240, r = (n % 240) / 16, c = n % 16;
    const uint8_t *t = ppu.vram + ppu.nt_offset[nt];
    uint8_t at = t[0x3C0 + (r >> 1) * 8 + (c >> 1)], tile[4];
    int pal = (at >> (((r & 1) ? 4 : 0) + ((c & 1) ? 2 : 0))) & 3;
    for (int q = 0; q < 4; q++) tile[q] = t[(r * 2 + (q >> 1)) * 32 + c * 2 + (q & 1)];
    resolve(p, tile, pal, &nt_seen[n], (nt & 1) * 32 + c * 2, (nt >> 1) * 30 + r * 2);
}

static void resolve_band(place_t *p, int n) {
    uint8_t tile[4], pal;
    if (!widescreen_square(n, tile, &pal)) { p->gen = gen; p->sq = NULL; p->quarters = 0; return; }
    resolve(p, tile, pal, NULL, -1, -1);
}

/* how the colors the game has now compare with the ones the art was made for: a factor per
   channel, 256 = the same. w: how much each color counts (NULL: all alike; a color that counts
   for nothing may be any). Returns 1 when they are the same colors. */
static int tint_of(const uint8_t *ref, const uint8_t *now, const uint8_t *w, int n, int emph, uint8_t gray, uint16_t tint[3]) {
    int same = emph == 0;
    for (int i = 0; i < n && same; i++) same = (w && !w[i]) || (now[i] & gray & 0x3F) == (ref[i] & 0x3F);
    if (same) { tint[0] = tint[1] = tint[2] = 256; return 1; }
    for (int c = 0; c < 3; c++) {
        unsigned a = 0, b = 0;
        for (int i = 0; i < n; i++) {
            unsigned k = w ? w[i] : 1;
            a += k * ((nes_rgb[emph * 64 + (now[i] & gray & 0x3F)] >> (16 - 8 * c)) & 255);
            b += k * ((nes_rgb[ref[i] & 0x3F] >> (16 - 8 * c)) & 255);
        }
        unsigned t = (a * 256 + b / 2 + 128) / (b + 1);
        tint[c] = (uint16_t)(t > 640 ? 640 : t);
    }
    return 0;
}

static inline uint32_t tinted(uint32_t px, const uint16_t tint[3]) {
    unsigned r = (((px >> 16) & 255) * tint[0]) >> 8, g = (((px >> 8) & 255) * tint[1]) >> 8, b = ((px & 255) * tint[2]) >> 8;
    return 0xFF000000u | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}

/* the place of the square below the one a pixel is in: by the name tables (two tile rows on, into
   the name table underneath past the 30th) where the PPU shows it, else by the canvas's map */
static place_t *place_below(uint16_t c, int px, int py) {
    int n = widescreen_below(px, py);
    place_t *p;
    if (n >= 0) {
        if (n >= BAND_SQUARES) return NULL;
        p = &band_place[n];
        if (p->gen != gen) resolve_band(p, n);
        return p;
    }
    if (!(c & HD_CELL)) return NULL;                    /* (no map and no name table cell: nothing known below) */
    int row = ((c >> 5) & 31) + 2, col = c & 31, nt = (c >> 10) & 3;
    if (row >= 30) { row -= 30; nt ^= 2; }
    n = nt * 240 + (row >> 1) * 16 + (col >> 1);
    p = &nt_place[n];
    if (p->gen != gen) resolve_nt(p, n);
    return p;
}

static place_t *place_of(uint16_t c, int *q) {
    place_t *p;
    if (c & HD_CELL) {
        int row = (c >> 5) & 31, col = c & 31, nt = (c >> 10) & 3;
        if (row >= 30) return NULL;
        int n = nt * 240 + (row >> 1) * 16 + (col >> 1);
        *q = (row & 1) * 2 + (col & 1);
        p = &nt_place[n];
        if (p->gen != gen) resolve_nt(p, n);
    } else if (c & HD_CELL_BAND) {
        int n = c & 0x3FF;
        *q = (c >> 10) & 3;
        p = &band_place[n];
        if (p->gen != gen) resolve_band(p, n);
    } else return NULL;
    return p;
}

/* ---- the people ---- */
typedef struct { int x, y; const figure_t *who; uint8_t behind, band; uint16_t tint[3]; } draw_t;
static draw_t draws[64 + 32];
static int n_draws;
static uint8_t slot_replaced[SLOTS];

/* the figure four sprite quarters make (tile and attributes each: top left, top right, bottom
   left, bottom right), in the palette the frame was drawn with */
static const figure_t *figure_of(const uint8_t tile[4], const uint8_t attr[4], const uint8_t *pal, int emph, uint8_t gray,
                                 uint16_t tint[3], int slots[4]) {
    uint8_t pat[256], now[12];
    for (int q = 0; q < 4; q++) {
        slots[q] = slot_of(tile[q], 1);
        if (slots[q] >= SLOTS) return NULL;
        const uint8_t *t = slot_tile[slots[q]];
        for (int y = 0; y < 8; y++) {
            int row = (attr[q] & 0x80) ? 7 - y : y;
            for (int x = 0; x < 8; x++) {
                int bit = (attr[q] & 0x40) ? x : 7 - x;
                pat[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = (uint8_t)(((t[row] >> bit) & 1) | (((t[row + 8] >> bit) & 1) << 1));
            }
        }
        for (int i = 0; i < 3; i++) now[q * 3 + i] = pal[16 + (attr[q] & 3) * 4 + 1 + i];
    }
    uint64_t key = fnv(FNV_OFFSET, pat, 256);
    const figure_t *first = find_figure((key ? key : 1) ^ map_mix), *g, *best = NULL;     /* bound to this kind of place? */
    if (!first) first = find_figure(key ? key : 1);
    g = first;
    if (!first) { missed(missed_fig, &n_missed_fig, key ? key : 1); return NULL; }
    for (;; g = &figures[g->next - 1]) {       /* the one made for these colors, else the first */
        int same = 1;
        for (int i = 0; i < 12 && same; i++) same = (now[i] & 0x3F) == (g->ref[i] & 0x3F);
        if (same) { best = g; break; }
        if (!g->next) break;
    }
    if (!best) best = first;
    tint_of(best->ref, now, NULL, 12, emph, gray, tint);
    return best;
}

static void add_draw(int x, int y, const uint8_t tile[4], const uint8_t attr[4], int band) {
    int line = y < 0 ? 0 : y > 239 ? 239 : y, slots[4];
    uint8_t e = ppu.line_emph[line];
    draw_t *d = &draws[n_draws];
    const uint8_t *pal = ppu.hd_palettes[HD_PALSET(ppu.hd_bg[line * 256 + 128])];
    d->who = figure_of(tile, attr, pal, (e >> 5) & 7, (e & 1) ? 0x30 : 0x3F, d->tint, slots);
    if (!d->who) { stat_people_unknown++; return; }
    stat_people++;
    d->x = x; d->y = y; d->behind = (attr[0] & 0x20) != 0; d->band = (uint8_t)band;
    for (int q = 0; q < 4; q++) slot_replaced[slots[q]] = 1;
    n_draws++;
}

/* this frame's figures: from the PPU's sprite memory, and the ones the canvas adds */
static void gather(void) {
    n_draws = 0;
    memset(slot_replaced, 0, sizeof slot_replaced);
    if (!n_figures || (ppu.frame_ctrl & 0x20)) return;          /* (8x16 sprites: not this game's maps) */
    const uint8_t *o = ppu.oam;
    uint8_t used[64] = { 0 };
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 64; i++) {
            if (used[i] || o[i * 4] >= 0xEF) continue;
            int x = o[i * 4 + 3], y = o[i * 4], part[4] = { i, -1, -1, -1 };
            for (int j = 0; j < 64; j++) {
                if (j == i || used[j] || o[j * 4] >= 0xEF) continue;
                if (pass == 0 && (j < i || j > i + 3)) continue;      /* first the ones that follow each other */
                int dx = o[j * 4 + 3] - x, dy = o[j * 4] - y;
                int q = dx == 8 && dy == 0 ? 1 : dx == 0 && dy == 8 ? 2 : dx == 8 && dy == 8 ? 3 : 0;
                if (q && part[q] < 0) part[q] = j;
            }
            if (part[1] < 0 || part[2] < 0 || part[3] < 0) continue;
            uint8_t tile[4], attr[4];
            for (int q = 0; q < 4; q++) { tile[q] = o[part[q] * 4 + 1]; attr[q] = o[part[q] * 4 + 2]; used[part[q]] = 1; }
            if (n_draws < 64) add_draw(x, y + 1, tile, attr, 0);
        }
    const ws_person_t *list;
    int n = widescreen_people_list(&list);
    for (int i = 0; i < n && n_draws < (int)(sizeof draws / sizeof draws[0]); i++)
        add_draw(list[i].x, list[i].y, list[i].tile, list[i].attr, 1);
}

static uint32_t spr_kept[WS_W * WS_H];

void sfc_frame(uint32_t *out, int stride, const uint8_t *idx, const uint32_t *bg, const uint32_t *spr, const uint16_t *cell,
               int src_stride, const uint8_t *emph, int w, int h, int org_x, int org_y) {
    if (w > WS_W) w = WS_W;
    if (h > WS_H) h = WS_H;
    refresh_slots();
    gen++;
    scene_mix = (ram[0x41] & 0x80) ? 0 : OVERWORLD_MIX;
    map_mix = (uint64_t)(ram[0x28] + 1) * PLACE_MIX;
    stat_known = stat_unknown = stat_people = stat_people_unknown = 0;
    n_missed_sq = n_missed_fig = 0;
    win_on = win_key && frame_loaded();
    gather();

    /* the NES pixels of the figures that are drawn from the pack are left out */
    for (int y = 0; y < h; y++) memcpy(spr_kept + (size_t)y * w, spr + (size_t)y * src_stride, (size_t)w * sizeof *spr);
    for (int k = 0; k < n_draws; k++) {
        const draw_t *d = &draws[k];
        for (int j = 0; j < 16; j++) {
            int y = d->y + j - org_y;
            if (y < 0 || y >= h) continue;
            for (int i = 0; i < 16; i++) {
                int x = d->x + i - org_x;
                if (x < 0 || x >= w) continue;
                uint32_t s = spr_kept[(size_t)y * w + x];
                if ((s & HD_PRESENT) && HD_TILE(s) < SLOTS && slot_replaced[HD_TILE(s)]) spr_kept[(size_t)y * w + x] = 0;
            }
        }
    }
    if (src_stride == w) remaster_frame(out, stride, idx, bg, spr_kept, w, emph, w, h);
    else {      /* (the sources' lines are longer than the part asked for: line by line) */
        for (int y = 0; y < h; y++)
            remaster_frame(out + (size_t)y * stride, stride, idx + (size_t)y * src_stride, bg + (size_t)y * src_stride,
                           spr_kept + (size_t)y * w, w, emph + y, w, 1);
    }

    /* black is one black: the backdrop's, wherever it shows (under a figure of the pack's as well) */
    for (int y = 0; y < h; y++) {
        const uint8_t *ir = idx + (size_t)y * src_stride;
        const uint32_t *br = bg + (size_t)y * src_stride, *sr = spr_kept + (size_t)y * w;
        uint32_t *o = out + (size_t)y * stride;
        uint8_t gray = (emph[y] & 1) ? 0x30 : 0x3F;
        for (int x = 0; x < w; x++) {
            uint32_t b = br[x];
            if ((sr[x] & HD_PRESENT) || ((b & HD_PRESENT) && HD_VALUE(b))) continue;
            uint8_t c = (b & HD_PRESENT) ? ppu.hd_palettes[HD_PALSET(b)][0] & gray : ir[x];
            if (!(nes_rgb[c & 0x3F] & 0xFFFFFF)) o[x] = 0xFF000000u;
        }
    }

    /* the map squares */
    int wx = 0, wy = 0, laid = n_pictures && widescreen_origin(&wx, &wy);       /* where the pictures lie */
    for (int y = 0; y < h; y++) {
        const uint32_t *br = bg + (size_t)y * src_stride, *sr = spr_kept + (size_t)y * w;
        const uint16_t *cr = cell + (size_t)y * src_stride;
        uint32_t *o = out + (size_t)y * stride;
        uint8_t gray = (emph[y] & 1) ? 0x30 : 0x3F;
        int em = (emph[y] >> 5) & 7;
        for (int x = 0; x < w; x++) {
            uint16_t c = cr[x];
            uint32_t b = br[x], s = sr[x];
            int q = 0;
            if (!c || !(b & HD_PRESENT)) continue;
            if ((s & HD_PRESENT) && (HD_VALUE(b) == 0 || !(s & HD_BEHIND))) continue;     /* a sprite of the NES's own */
            place_t *p = place_of(c, &q);
            if (!p || !p->sq || !(p->quarters & (1 << q))) continue;
            const square_t *sq = p->sq;
            if (!p->tinted) {
                const uint8_t *pal = ppu.hd_palettes[HD_PALSET(b)];
                uint8_t now[3] = { pal[p->pal * 4 + 1], pal[p->pal * 4 + 2], pal[p->pal * 4 + 3] };
                if (p->blank && ((pal[0] ^ sq->ref[0]) & 0x3F)) { p->quarters = 0; continue; }     /* an empty square on another backdrop */
                p->tinted = (uint8_t)(1 + !tint_of(sq->ref + 1, now, sq->w, 3, em, gray, p->tint));
            }
            unsigned f = sq->frames > 1 ? (unsigned)((ppu.frame / sq->ticks) % sq->frames) : 0;
            uint32_t px = sq->px[f * 256 + ((q >> 1) * 8 + HD_ROW(b)) * 16 + (q & 1) * 8 + HD_COL(b)];
            if ((px >> 24) == 1) {          /* a picture's: the one at this place of the map */
                if (!laid || (px & 0xFFFFFF) >= n_pictures) continue;
                const picture_t *pc = &pictures[px & 0xFFFFFF];
                int mx = (wx + x + org_x) % (int)pc->w, my = (wy + y + org_y) % (int)pc->h;
                px = pc->px[(size_t)(my < 0 ? my + (int)pc->h : my) * pc->w + (mx < 0 ? mx + (int)pc->w : mx)];
            }
            if (!(px >> 24)) {              /* left to the NES: its own pixel, in whatever color it has there */
                uint8_t c = ppu.hd_palettes[HD_PALSET(b)][HD_VALUE(b) ? HD_PAL(b) * 4 + HD_VALUE(b) : 0] & gray;
                if (!(nes_rgb[c & 0x3F] & 0xFFFFFF)) o[x] = 0xFF000000u;       /* (black: the art's black) */
                continue;
            }
            o[x] = p->tinted == 1 ? (0xFF000000u | px) : tinted(px, p->tint);
        }
    }

    /* the windows */
    for (int y = 0; y < h && win_on; y++) {
        const uint32_t *br = bg + (size_t)y * src_stride, *sr = spr_kept + (size_t)y * w;
        const uint16_t *cr = cell + (size_t)y * src_stride;
        uint32_t *o = out + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            uint16_t c = cr[x];
            uint32_t b = br[x], s = sr[x];
            if (!(c & HD_CELL) || !(b & HD_PRESENT) || ((c >> 5) & 31) >= 30) continue;
            if ((s & HD_PRESENT) && (HD_VALUE(b) == 0 || !(s & HD_BEHIND))) continue;     /* a sprite of the NES's own */
            wtile_t *wt = &win_tile[((c >> 10) & 3) * 960 + ((c >> 5) & 31) * 32 + (c & 31)];
            if (wt->gen != gen) resolve_window(wt, (c >> 10) & 3, (c >> 5) & 31, c & 31);
            if (!wt->tile) continue;
            if (!wt->tinted) {              /* the letters' color now, against the white they are at rest */
                uint8_t gray = (emph[y] & 1) ? 0x30 : 0x3F, white = 0x30;
                uint8_t now = ppu.hd_palettes[HD_PALSET(b)][wt->pal * 4 + 1] & gray & 0x3F;
                int em = (emph[y] >> 5) & 7;
                uint16_t t[3] = { 256, 256, 256 }, f[3];
                wt->tinted = 1;
                wt->ink = 0xFF000000u | win_ink; wt->fill = 0xFF000000u | win_fill;
                /* (the game has two whites, $30 and $20: one color) */
                if (nes_rgb[em * 64 + now] != nes_rgb[white] && !tint_of(&white, &now, NULL, 1, em, gray, t)) {
                    f[0] = t[0] > t[1] ? t[0] : t[1];
                    if (t[2] > f[0]) f[0] = t[2];
                    if (f[0] > 256) f[0] = 256;
                    f[1] = f[2] = f[0];
                    wt->ink = tinted(win_ink, t); wt->fill = tinted(win_fill, f);
                }
            }
            int row = HD_ROW(b), col = HD_COL(b);
            uint32_t px = HD_VALUE(b) ? win_ink : win_fill;
            if (wt->tile > 1) {             /* the frame: its piece */
                int piece = frame_piece[wt->tile - 0x83];
                uint32_t own = px;
                px = win_px[((piece / 3) * 8 + row) * 24 + (piece % 3) * 8 + col];
                if (wt->tile == 0x8C && !((wt->cols >> (7 - col)) & 1)) px = win_fill;
                if (wt->tile == 0x83 && col >= 4) px = own;                         /* the mark beside the edge */
            }
            o[x] = px == win_fill ? wt->fill : wt->ink;
        }
    }

    /* the figures, the last of the sprite memory first (the first are on top) */
    for (int k = n_draws - 1; k >= 0; k--) {
        const draw_t *d = &draws[k];
        const figure_t *g = d->who;
        int plain = d->tint[0] == 256 && d->tint[1] == 256 && d->tint[2] == 256;
        for (int j = 0; j < g->h; j++) {
            int py = d->y + g->dy + j, y = py - org_y;
            if (y < 0 || y >= h) continue;
            if (py >= 0 && py < 240 && !(ppu.line_emph[py] & 0x10)) continue;       /* sprites are off on this line */
            for (int i = 0; i < g->w; i++) {
                int px = d->x + g->dx + i, x = px - org_x;
                if (x < 0 || x >= w) continue;
                uint32_t v = g->px[j * g->w + i];
                if (!(v >> 24)) continue;
                int in_picture = px >= 0 && px < 256 && py >= 0 && py < 240 && widescreen_inner(px, py);
                if (in_picture == d->band) continue;                    /* the picture's figures in the picture, the canvas's beside it */
                uint32_t b = bg[(size_t)y * src_stride + x];
                if (d->behind && (b & HD_PRESENT) && HD_VALUE(b)) continue;
                int inside = px >= d->x && px < d->x + 16 && py >= d->y && py < d->y + 16;
                if (!inside) {      /* beyond the NES figure's own square: only over the map, never over a window */
                    int q = 0;
                    uint16_t c = cell[(size_t)y * src_stride + x];
                    place_t *p = c ? place_of(c, &q) : NULL;
                    if (!p || !p->sq || !(p->quarters & (1 << q))) continue;
                }
                out[(size_t)y * stride + x] = plain ? (0xFF000000u | v) : tinted(v, d->tint);
            }
        }
    }

    /* what stands taller than its square: over the square above it, and over whoever stands there */
    if (!any_tops) return;
    for (int y = 0; y < h; y++) {
        const uint32_t *br = bg + (size_t)y * src_stride;
        const uint16_t *cr = cell + (size_t)y * src_stride;
        uint32_t *o = out + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            uint16_t c = cr[x];
            uint32_t b = br[x];
            int q = 0;
            if (!c || !(b & HD_PRESENT)) continue;
            place_t *own = place_of(c, &q);
            if (!own || !own->sq || !(own->quarters & (1 << q))) continue;     /* (only over the map: never over a window) */
            place_t *p = place_below(c, x + org_x, y + org_y);
            if (!p || !p->sq || p->quarters != 15 || !p->sq->top) continue;
            uint32_t v = p->sq->top[((q >> 1) * 8 + HD_ROW(b)) * 16 + (q & 1) * 8 + HD_COL(b)];
            if (!(v >> 24)) continue;
            if (!p->tinted) {
                const uint8_t *pal = ppu.hd_palettes[HD_PALSET(b)];
                uint8_t now[3] = { pal[p->pal * 4 + 1], pal[p->pal * 4 + 2], pal[p->pal * 4 + 3] };
                uint8_t e = emph[y];
                p->tinted = (uint8_t)(1 + !tint_of(p->sq->ref + 1, now, p->sq->w, 3, (e >> 5) & 7, (e & 1) ? 0x30 : 0x3F, p->tint));
            }
            o[x] = p->tinted == 1 ? (0xFF000000u | v) : tinted(v, p->tint);
        }
    }
}
