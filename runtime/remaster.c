/*
 * remaster.c - the remastered graphics (remaster.h).
 *
 * Palette: the colors the game leans on most are given the natural, deeper tones of the Pixel
 * Remaster games (an ocean blue instead of the NES's violet, grass and forest greens, warm
 * browns, peach skin, a cool near-black for outlines); the rest of the NES palette is only
 * calmed where it is at its most saturated. Emphasis scales them as it scales the NES colors.
 *
 * Ramps: each color, under each emphasis setting, has seven steps (and the half steps between
 * them), the hue-shifted ramps pixel artists shade with, worked out in OKLCH so every step is
 * an even change of lightness: three darker ones that turn toward blue (warm colors a little
 * toward red) with their saturation easing toward one common level, so pale colors deepen and
 * vivid ones calm instead of turning garish; the color itself; and three lighter ones that
 * turn toward yellow and lose chroma. Greys shade toward a cool grey. The blues of the
 * backgrounds, the sea's, are the exception, after the Pixel Remaster's water: they keep their
 * saturation in shadow (a deep royal blue) and their lights turn turquoise.
 *
 * Automatic inks, from the tile's own 8x8 pattern: a pixel inside a region of one color is lit
 * one step where the region's edge faces up or left and shaded one step where it faces down or
 * right; pixels of a texture (fewer than two neighbors of their color) and a sprite's outline
 * (next to transparency) keep their color. Lettering (color 0 and one other color in thin strokes
 * forming at most three shapes: letters, digits, box borders) keeps its colors throughout, so text
 * stays crisp instead of embossed. Neighbors beyond the tile count as the same color, so a region
 * spanning tiles shows no seam. An ink that names the pixel's own color takes the color the PPU
 * actually drew; one that names another of the tile's colors takes it from the palette the PPU
 * drew that line with (ppu.hd_palettes), so mid-frame palette changes stay right.
 *
 * Tiles: the cartridge has no CHR ROM. The game copies its tiles from the program ROM into
 * 8 KiB of CHR RAM, 512 slots, as scenes need them, so a pixel's source names a slot and the
 * inks belong to what the slot holds: before a frame is colored, every slot whose 16 bytes have
 * changed gets its inks again, from the pack where it has that tile, else automatically.
 *
 * Pack: "DW4R", version 1, a tile count, then per tile: the 64-bit FNV-1a hash of its 16 bytes
 * (little-endian), the layer it applies to (0 background, 1 sprites, 2 both) and its 64 inks
 * (remaster.h), row by row.
 */
#include "remaster.h"
#include "nes.h"
#include "palette.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 512    /* tiles of the CHR RAM */
#define SHADES 13   /* steps 1, 1.5, 2, ... 7 */
#define BASE 6      /* step 4, the color itself */
static uint32_t ramp[2][8][64][SHADES];   /* [0] sprites, [1] backgrounds */
static uint8_t ink_bg[SLOTS * 64], ink_spr[SLOTS * 64];   /* per slot and layer: the pack's where it has the tile, else automatic */
static uint8_t slot_tile[SLOTS][16];      /* the tile each slot's inks were made for */
static uint8_t slot_known[SLOTS];
static int ready;

/* the pack: an open-addressed table keyed by the tile's hash */
typedef struct { uint64_t key; uint8_t has_bg, has_spr; uint8_t bg[64], spr[64]; } pack_tile_t;
static pack_tile_t *pack;
static uint32_t pack_cap, pack_count;

static uint64_t tile_hash(const uint8_t *t) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 16; i++) { h ^= t[i]; h *= 0x100000001b3ULL; }
    return h ? h : 1;   /* 0 marks an empty table cell */
}

static pack_tile_t *pack_find(uint64_t key, int add) {
    if (!pack_cap) return NULL;
    for (uint32_t i = (uint32_t)(key % pack_cap);; i = (i + 1) % pack_cap) {
        if (pack[i].key == key) return &pack[i];
        if (!pack[i].key) {
            if (!add) return NULL;
            pack[i].key = key; pack_count++;
            return &pack[i];
        }
    }
}

/* ---- ramps ---- */
static void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l) {
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *l = (mx + mn) / 2;
    if (mx == mn) { *h = 0; *s = 0; return; }
    double d = mx - mn;
    *s = *l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) *h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) *h = (b - r) / d + 2;
    else *h = (r - g) / d + 4;
    *h *= 60;
}

static double hue_part(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

static uint32_t hsl_to_rgb(double h, double s, double l) {
    double r, g, b;
    if (s <= 0) r = g = b = l;
    else {
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, hh = h / 360;
        r = hue_part(p, q, hh + 1.0 / 3); g = hue_part(p, q, hh); b = hue_part(p, q, hh - 1.0 / 3);
    }
    int R = (int)(r * 255 + 0.5), G = (int)(g * 255 + 0.5), B = (int)(b * 255 + 0.5);
    return 0xFF000000u | (uint32_t)(R << 16) | (uint32_t)(G << 8) | (uint32_t)B;
}

/* turn hue h by up to deg degrees toward target, the short way round */
static double hue_toward(double h, double target, double deg) {
    double d = fmod(target - h + 540.0, 360.0) - 180.0;
    if (fabs(d) <= deg) return target;
    return fmod(h + (d > 0 ? deg : -deg) + 360.0, 360.0);
}

static double srgb_lin(double c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
static double lin_srgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055; }

static void rgb_to_oklch(uint32_t rgb, double *L, double *C, double *h) {
    double r = srgb_lin(((rgb >> 16) & 255) / 255.0), g = srgb_lin(((rgb >> 8) & 255) / 255.0), b = srgb_lin((rgb & 255) / 255.0);
    double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    double B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    *L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    *C = sqrt(A * A + B * B);   /* not hypot(): glibc 2.35 gave it a new symbol version, and the Linux build runs on 2.34 */
    *h = fmod(atan2(B, A) * 180 / M_PI + 360, 360);
}

/* the linear RGB of an OKLCH color; 0 where it lies outside sRGB */
static int oklch_lin(double L, double C, double h, double v[3]) {
    double A = C * cos(h * M_PI / 180), B = C * sin(h * M_PI / 180);
    double l = L + 0.3963377774 * A + 0.2158037573 * B;
    double m = L - 0.1055613458 * A - 0.0638541728 * B;
    double s = L - 0.0894841775 * A - 1.2914855480 * B;
    l = l * l * l; m = m * m * m; s = s * s * s;
    v[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    v[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    v[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    for (int i = 0; i < 3; i++)
        if (v[i] < -1e-4 || v[i] > 1.0001) return 0;
    return 1;
}

static uint32_t oklch_to_rgb(double L, double C, double h) {
    double v[3];
    L = L < 0 ? 0 : L > 1 ? 1 : L;
    if (!oklch_lin(L, C, h, v)) {   /* outside sRGB: keep the lightness and hue, give up chroma */
        double lo = 0, hi = C;
        for (int i = 0; i < 20; i++) {
            double mid = (lo + hi) / 2;
            if (oklch_lin(L, mid, h, v)) lo = mid; else hi = mid;
        }
        oklch_lin(L, lo, h, v);
    }
    uint32_t out = 0xFF000000u;
    for (int i = 0; i < 3; i++) {
        double c = v[i] < 0 ? 0 : v[i] > 1 ? 1 : v[i];
        int n = (int)(lin_srgb(c) * 255 + 0.5);
        out |= (uint32_t)(n > 255 ? 255 : n) << (16 - 8 * i);
    }
    return out;
}

/* NES color -> remastered color (0 = derive it) */
static const uint32_t remaster_colors[64] = {
    [0x0F] = 0x12121A, [0x1D] = 0x12121A, [0x30] = 0xF8F8F4, [0x20] = 0xF8F8F4,
    [0x12] = 0x2A62C8, [0x21] = 0x7FC4F0, [0x11] = 0x2F7BD8, [0x01] = 0x1C3C8C, [0x02] = 0x24308C,
    [0x2A] = 0x439E28, [0x1A] = 0x3C8F2A, [0x19] = 0x2F6A1C, [0x09] = 0x234A14, [0x0A] = 0x1F5A1A,
    [0x17] = 0x8E4A1C, [0x07] = 0x4A2410, [0x27] = 0xC88E4C, [0x37] = 0xF0D49C, [0x16] = 0xA0402C,
    [0x35] = 0xF0D0C0, [0x36] = 0xF2C6A8, [0x26] = 0xE08A68,
    [0x10] = 0x9A9AA4, [0x00] = 0x5E5E68, [0x2D] = 0x4A4A54, [0x3D] = 0xC8C8CC,
    [0x28] = 0xB4A84A, [0x18] = 0x5E5A1C, [0x38] = 0xE6DDB0, [0x08] = 0x3A3410,
};

/* colors only the background layer takes */
static const uint32_t remaster_bg_colors[64] = {
    [0x00] = 0x605A54, [0x10] = 0xA29A90,   /* stone: a warm grey */
};

static void build_ramps(void) {
    uint32_t base[512];
    nes_palette_build(base);
    for (int layer = 0; layer < 2; layer++)
    for (int e = 0; e < 8; e++)
        for (int c = 0; c < 64; c++) {
            uint32_t rgb = base[e * 64 + c], nes0 = base[c];
            uint32_t rc = layer && remaster_bg_colors[c] ? remaster_bg_colors[c] : remaster_colors[c];
            if (rc) {   /* the remastered color, with the emphasis's effect on the NES one */
                uint32_t out = 0;
                for (int sh = 0; sh <= 16; sh += 8) {
                    double n0 = ((nes0 >> sh) & 255) + 1, ne = ((rgb >> sh) & 255) + 1;
                    double v = ((rc >> sh) & 255) * ne / n0;
                    out |= (uint32_t)(v > 255 ? 255 : v) << sh;
                }
                rgb = out;
            } else {   /* calm the NES's most saturated colors */
                double h0, s0, l0;
                rgb_to_hsl(((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, &h0, &s0, &l0);
                if (s0 > 0.7) rgb = hsl_to_rgb(h0, 0.7 + (s0 - 0.7) * 0.5, l0) & 0xFFFFFF;
            }
            double L, C, h;
            rgb_to_oklch(rgb, &L, &C, &h);
            int water = layer && C >= 0.025 && h >= 225 && h <= 285;   /* the blues of the sea and the shallows */
            for (int i = 0; i < SHADES; i++) {
                double k = (i - BASE) / 2.0;
                double LL = L, CC = C, hh = h;
                if (k < 0) {
                    double n = -k;
                    LL = L - 0.085 * n;
                    if (C < 0.025) {   /* a grey: cool in shadow */
                        CC = C > 0.010 * n ? C : 0.010 * n;
                        hh = C < 0.01 ? 265 : hue_toward(h, 265, 4.0 * n);
                    } else if (water) {   /* deep water keeps its saturation: a royal blue, not a grey one */
                        CC = C * (1 - 0.04 * n);
                        hh = hue_toward(h, 272, 3.0 * n);
                    } else {
                        double sat = C / L + (0.22 - C / L) * 0.1 * n;
                        CC = sat * (LL > 0 ? LL : 0);
                        hh = h >= 100 && h <= 300 ? hue_toward(h, 265, 4.0 * n) : hue_toward(h, 30, 3.0 * n);
                    }
                } else if (k > 0) {
                    LL = L + (1 - L) * 0.24 * k;
                    CC = C * (1 - (water ? 0.05 : 0.12) * k);
                    if (water) hh = hue_toward(h, 195, 12.0 * k);   /* crests turn turquoise */
                    else if (C >= 0.025) hh = hue_toward(h, 100, 6.0 * k);
                }
                ramp[layer][e][c][i] = i != BASE ? oklch_to_rgb(LL, CC, hh) : (0xFF000000u | rgb);
            }
        }
}

/* ---- automatic inks ---- */

/* lettering: color 0 and one other color, in thin strokes (no 4x4 square of it) that form at most
   three connected shapes, as letters, digits and box borders do; speckled textures have many */
static int lettering(const int v[8][8]) {
    int used = 0, f, shapes = 0, seen[8][8] = { { 0 } }, stack[64][2];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) used |= 1 << v[y][x];
    if (used != 3 && used != 5 && used != 9) return 0;
    f = used & 2 ? 1 : used & 4 ? 2 : 3;
    for (int y0 = 0; y0 <= 4; y0++)
        for (int x0 = 0; x0 <= 4; x0++) {
            int all = 1;
            for (int j = 0; j < 16 && all; j++) all = v[y0 + j / 4][x0 + j % 4] == f;
            if (all) return 0;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int n = 0;
            if (v[y][x] != f || seen[y][x]) continue;
            if (++shapes > 3) return 0;
            seen[y][x] = 1;
            stack[n][0] = x; stack[n][1] = y; n++;
            while (n) {   /* the shape, through its 8 neighbors */
                int cx = stack[n - 1][0], cy = stack[n - 1][1];
                n--;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || nx > 7 || ny < 0 || ny > 7 || v[ny][nx] != f || seen[ny][nx]) continue;
                        seen[ny][nx] = 1;
                        stack[n][0] = nx; stack[n][1] = ny; n++;
                    }
            }
        }
    return 1;
}

static void auto_tile(const uint8_t *t, uint8_t *bg, uint8_t *spr) {
    int v[8][8];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) v[y][x] = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1);
    int flat = lettering(v);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int c = v[y][x];
            int up = y > 0 ? v[y - 1][x] : c, down = y < 7 ? v[y + 1][x] : c;
            int left = x > 0 ? v[y][x - 1] : c, right = x < 7 ? v[y][x + 1] : c;
            int same = (up == c) + (down == c) + (left == c) + (right == c);
            int lit = (up != c) + (left != c), shade = (down != c) + (right != c);
            int step = 4;
            if (same >= 2 && !flat) step += (lit > shade) - (shade > lit);
            bg[y * 8 + x] = REMASTER_INK(c, step);
            if (c == 0) { spr[y * 8 + x] = 0; continue; }
            int outline = up == 0 || down == 0 || left == 0 || right == 0;
            spr[y * 8 + x] = REMASTER_INK(c, outline ? 4 : step);
        }
}

static int load_pack(const char *path) {
    FILE *f = path ? nes_fopen(path, "rb") : NULL;
    free(pack); pack = NULL; pack_cap = pack_count = 0;
    if (!f) return 0;
    uint8_t head[12];
    if (fread(head, 1, 12, f) == 12 && !memcmp(head, "DW4R", 4) && head[4] == 1) {
        uint32_t count = head[8] | (head[9] << 8) | ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
        if (count > 65536) count = 65536;
        pack_cap = count * 2 + 16;
        pack = calloc(pack_cap, sizeof *pack);
        uint8_t e[73];
        for (uint32_t i = 0; pack && i < count && fread(e, 1, sizeof e, f) == sizeof e; i++) {
            uint64_t key = 0;
            for (int k = 7; k >= 0; k--) key = (key << 8) | e[k];
            pack_tile_t *t = pack_find(key ? key : 1, 1);
            if (e[8] == 0 || e[8] == 2) { memcpy(t->bg, e + 9, 64); t->has_bg = 1; }
            if (e[8] == 1 || e[8] == 2) { memcpy(t->spr, e + 9, 64); t->has_spr = 1; }
        }
        if (!pack) pack_cap = pack_count = 0;
    }
    fclose(f);
    if (pack_count) printf("remaster: %u hand-made tiles from %s\n", pack_count, path);
    return (int)pack_count;
}

int remaster_init(const char *pack_path) {
    build_ramps();
    memset(slot_known, 0, sizeof slot_known);
    memset(ink_bg, 0, sizeof ink_bg);
    memset(ink_spr, 0, sizeof ink_spr);
    int n = load_pack(pack_path);
    ready = 1;
    return n;
}

int remaster_ready(void) { return ready; }
void remaster_forget(void) { ready = 0; }

/* the inks of every slot whose tile changed since the last frame */
static void refresh_slots(void) {
    for (int i = 0; i < SLOTS; i++) {
        const uint8_t *t = cart.chr_ram + i * 16;
        if (slot_known[i] && !memcmp(slot_tile[i], t, 16)) continue;
        memcpy(slot_tile[i], t, 16);
        slot_known[i] = 1;
        auto_tile(t, ink_bg + i * 64, ink_spr + i * 64);
        const pack_tile_t *p = pack_find(tile_hash(t), 0);
        if (p && p->has_bg) memcpy(ink_bg + i * 64, p->bg, 64);
        if (p && p->has_spr) memcpy(ink_spr + i * 64, p->spr, 64);
    }
}

/* ---- the frame ---- */
void remaster_frame(uint32_t *out, int stride, const uint8_t *idx, const uint32_t *bg, const uint32_t *spr,
                    int src_stride, const uint8_t *emph, int w, int h) {
    refresh_slots();
    for (int y = 0; y < h; y++) {
        const uint32_t (*rs)[SHADES] = ramp[0][(emph[y] >> 5) & 7], (*rb)[SHADES] = ramp[1][(emph[y] >> 5) & 7];
        uint8_t gray = (emph[y] & 1) ? 0x30 : 0x3F;
        const uint8_t *ir = idx + (size_t)y * src_stride;
        const uint32_t *br = bg + (size_t)y * src_stride, *sr = spr + (size_t)y * src_stride;
        uint32_t *o = out + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            uint32_t b = br[x], s = sr[x], src;
            const uint8_t *inks;
            int sprite;
            if ((s & HD_PRESENT) && (HD_VALUE(b) == 0 || !(s & HD_BEHIND))) { src = s; inks = ink_spr; sprite = 1; }
            else if (b & HD_PRESENT) { src = b; inks = ink_bg; sprite = 0; }
            else { o[x] = rb[ir[x] & 0x3F][BASE]; continue; }
            unsigned tile = HD_TILE(src);
            uint8_t ink = tile < SLOTS && !cart.chr ? inks[tile * 64 + HD_ROW(src) * 8 + HD_COL(src)] : 0;
            int value = (int)HD_VALUE(src), c = ink & 3;
            const uint32_t (*rp)[SHADES] = sprite ? rs : rb;
            if (!ink || (sprite && c == 0)) { o[x] = rp[ir[x] & 0x3F][BASE]; continue; }
            uint8_t nes;
            const uint8_t *pal = ppu.hd_palettes[HD_PALSET(src)];   /* the palette of the pixel's line */
            if (c == value) nes = ir[x];   /* the color the PPU drew */
            else if (c == 0) nes = pal[0] & gray;
            else nes = pal[(sprite ? 16 : 0) + HD_PAL(src) * 4 + c] & gray;
            int step = (ink >> 2) & 7, shade = step ? (step - 1) * 2 + ((ink & REMASTER_HALF) != 0) : BASE;
            o[x] = rp[nes & 0x3F][shade < SHADES ? shade : SHADES - 1];
        }
    }
}
