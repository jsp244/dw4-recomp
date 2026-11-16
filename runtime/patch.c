/*
 * patch.c - IPS patches applied to the cartridge image in memory (patch.h).
 *
 * IPS: "PATCH", then records of a 3-byte offset and a 2-byte length followed by that many bytes;
 * a length of 0 is a run (2-byte count, 1 byte to repeat); "EOF" ends the list, optionally
 * followed by a 3-byte size to cut the file to, which is ignored here. All numbers big-endian.
 */
#include "patch.h"
#include "nes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 16
#define BANK8 0x2000u

static uint8_t *base;           /* the dump as loaded */
static uint8_t *next;           /* the prepared image */
static uint8_t *dirty;          /* per PRG byte of the image running: differs from the dump */
static uint32_t size, n8;       /* PRG bytes, and 8 KiB banks */
static int base_mapper, next_mapper;
static int pending, next_count;
static int applied, routines_off;
static long changed;
static int live;
static unsigned generation;
static uint32_t signature;
static uint32_t next_signature;     /* of the prepared image */
static uint32_t boot_image;         /* the image this machine was powered on under */
int patch_live_wait;
long patch_refused[4];          /* how often a switch was put off: the memory map, the program counter, the stack */
uint16_t patch_refused_at;      /* the address that did it last */

void patch_reset(void) {
    free(base); free(next); free(dirty);
    base = next = dirty = NULL;
    size = n8 = 0;
    pending = next_count = applied = routines_off = 0;
    changed = 0;
    signature = next_signature = boot_image = 0;
    patch_live_wait = 0;
    generation++;
    recomp_patch(NULL, 0);
    if (!cart.prg || !cart.prg_size) return;
    size = cart.prg_size;
    n8 = size / BANK8;
    base_mapper = next_mapper = cart.mapper;
    base = malloc(size); next = malloc(size); dirty = calloc(size, 1);
    if (!base || !next || !dirty) { free(base); free(next); free(dirty); base = next = dirty = NULL; size = n8 = 0; return; }
    memcpy(base, cart.prg, size);
    memcpy(next, cart.prg, size);
}

/* The mapper number lives in the top halves of header bytes 6 and 7. A patch that writes them
   turns the cartridge into another board: the low half of the number from byte 6, the high half
   from byte 7 (when the patch writes that one too). */
static int mapper_after(int mapper, const uint8_t *hdr, const uint8_t *hdr_set) {
    if (hdr_set[6]) mapper = (mapper & ~0x0F) | (hdr[6] >> 4);
    if (hdr_set[7]) mapper = (mapper & ~0xF0) | (hdr[7] & 0xF0);
    return mapper;
}

/* one IPS file onto img; the first pass only checks it. Returns NULL or what is wrong with it. */
static const char *apply(uint8_t *img, int *mapper, const uint8_t *ips, size_t len) {
    static const char *e_format = "not an IPS patch", *e_big = "reaches beyond the ROM (bigger ROMs are not supported)",
                      *e_board = "needs a cartridge board this port does not have";
    if (len < 8 || memcmp(ips, "PATCH", 5) != 0) return e_format;
    for (int pass = 0; pass < 2; pass++) {
        uint8_t hdr[HEADER] = { 0 }, hdr_set[HEADER] = { 0 };
        size_t p = 5;
        for (;;) {
            if (p + 3 > len) return e_format;
            if (!memcmp(ips + p, "EOF", 3)) break;
            if (p + 5 > len) return e_format;
            uint32_t off = ((uint32_t)ips[p] << 16) | ((uint32_t)ips[p + 1] << 8) | ips[p + 2];
            uint32_t n = ((uint32_t)ips[p + 3] << 8) | ips[p + 4];
            int run = n == 0;
            p += 5;
            if (run) {
                if (p + 3 > len) return e_format;
                n = ((uint32_t)ips[p] << 8) | ips[p + 1];
            } else if (p + n > len) return e_format;
            if (off + n > HEADER + size) return e_big;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t v = run ? ips[p + 2] : ips[p + i];
                if (off + i < HEADER) { hdr[off + i] = v; hdr_set[off + i] = 1; }
                else if (pass) img[off + i - HEADER] = v;
            }
            p += run ? 3 : n;
        }
        int m = mapper_after(*mapper, hdr, hdr_set);
        if (m != *mapper && m != 1 && m != 4) return e_board;      /* MMC1 and MMC3 are what the runtime has */
        if (m == 4 && (size < 4 * BANK8 || cart.chr_size)) return e_board;
        if (pass) *mapper = m;
    }
    return NULL;
}

/* identifies an image: 0 for the dump on its own board, else a hash of what differs */
static uint32_t signature_of(const uint8_t *img, int mapper_number) {
    uint32_t h = 2166136261u;
    long n = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (img[i] == base[i]) continue;
        n++;
        h = (h ^ i) * 16777619u;
        h = (h ^ img[i]) * 16777619u;
    }
    h = (h ^ (uint32_t)mapper_number) * 16777619u;
    return (n || mapper_number != base_mapper) ? (h ? h : 1) : 0;
}

static uint8_t *read_file(const char *path, long *len) {
    FILE *f = nes_fopen(path, "rb");
    uint8_t *buf = NULL;
    *len = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = *len > 0 && *len < (16 << 20) ? malloc((size_t)*len) : NULL;
    if (buf && fread(buf, 1, (size_t)*len, f) != (size_t)*len) { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

/* a source's bytes: its own (memory) or read from its file (*owned: to be freed) */
static const uint8_t *bytes_of(const patch_src_t *src, long *len, uint8_t **owned) {
    *owned = NULL;
    if (src->data) { *len = (long)src->len; return src->data; }
    *owned = src->path ? read_file(src->path, len) : NULL;
    return *owned;
}

/* marks the bytes of the .nes file (header included) that an IPS file writes; -1 if it is not one */
static long mark(const uint8_t *ips, long len, uint8_t *map, uint32_t map_len) {
    long n_written = 0;
    if (len < 8 || memcmp(ips, "PATCH", 5) != 0) return -1;
    for (long p = 5;;) {
        if (p + 3 > len) return -1;
        if (!memcmp(ips + p, "EOF", 3)) break;
        if (p + 5 > len) return -1;
        uint32_t off = ((uint32_t)ips[p] << 16) | ((uint32_t)ips[p + 1] << 8) | ips[p + 2];
        uint32_t n = ((uint32_t)ips[p + 3] << 8) | ips[p + 4];
        int run = n == 0;
        p += 5;
        if (run) { if (p + 3 > len) return -1; n = ((uint32_t)ips[p] << 8) | ips[p + 1]; }
        else if (p + (long)n > len) return -1;
        for (uint32_t k = 0; k < n; k++)
            if (off + k < map_len && !map[off + k]) { map[off + k] = 1; if (off + k >= HEADER) n_written++; }
        p += run ? 3 : (long)n;
    }
    return n_written;
}

int patch_inspect_src(const patch_src_t *src, patch_info_t *info) {
    memset(info, 0, sizeof *info);
    if (!base) return 0;
    long len;
    uint8_t *owned;
    const uint8_t *buf = bytes_of(src, &len, &owned);
    uint8_t *map = calloc(HEADER + size, 1);
    long n = buf && map ? mark(buf, len, map, HEADER + size) : -1;
    uint8_t *img = malloc(size ? size : 1);
    if (n >= 0 && img) {
        int m = base_mapper;
        info->bytes = n;
        info->board = map[6] || map[7];
        memcpy(img, base, size);
        apply(img, &m, buf, (size_t)len);
        /* The hacks the menu knows, by what they change in the dump (file offsets, 512 KiB ROM): the
           sea encounter fix is one branch in bank 24; manual tactics hooks the tactics menu in bank 17
           and the battle code in bank 11; the uncensoring replaces the graphics of bank 10. (A patch
           that writes the dump's own bytes there - one that takes such a hack out of a bigger one -
           changes nothing and is none of them.) */
#define CHANGED(off) (img[(off) - HEADER] != base[(off) - HEADER])
        if (size == 0x80000) {
            long in10 = 0;
            for (uint32_t o = 0x28010; o < 0x2C010; o++) in10 += CHANGED(o);
            if (info->board || n > 100000) info->kind = PATCH_WHOLE;
            else if (n <= 4 && CHANGED(0x61C8B)) info->kind = PATCH_SEA;
            else if (n < 4000 && CHANGED(0x44067) && CHANGED(0x2EF9F)) info->kind = PATCH_MANUAL;
            else if (in10 > 12000 && n < 60000) info->kind = PATCH_UNCENSOR;
        }
#undef CHANGED
    }
    free(owned); free(map); free(img);
    return n >= 0;
}

int patch_inspect(const char *path, patch_info_t *info) {
    patch_src_t src = { path, path, NULL, 0 };
    return patch_inspect_src(&src, info);
}

long patch_overlap_src(const patch_src_t *a, const patch_src_t *b) {
    if (!base) return 0;
    long la, lb, both = 0;
    uint8_t *oa, *ob;
    const uint8_t *fa = bytes_of(a, &la, &oa), *fb = bytes_of(b, &lb, &ob);
    uint8_t *ma = calloc(HEADER + size, 1), *mb = calloc(HEADER + size, 1);
    if (fa && fb && ma && mb && mark(fa, la, ma, HEADER + size) >= 0 && mark(fb, lb, mb, HEADER + size) >= 0)
        for (uint32_t o = HEADER; o < HEADER + size; o++) both += ma[o] & mb[o];
    free(oa); free(ob); free(ma); free(mb);
    return both;
}

long patch_overlap(const char *a, const char *b) {
    patch_src_t sa = { a, a, NULL, 0 }, sb = { b, b, NULL, 0 };
    return patch_overlap_src(&sa, &sb);
}

int patch_prepare_list(const patch_src_t *src, int n, char *errors, size_t errors_len) {
    if (errors && errors_len) errors[0] = 0;
    if (!base) return 0;
    memcpy(next, base, size);
    next_mapper = base_mapper;
    next_count = 0;
    for (int k = 0; k < n; k++) {
        long len;
        uint8_t *owned;
        const uint8_t *buf = bytes_of(&src[k], &len, &owned);
        const char *err = buf ? apply(next, &next_mapper, buf, (size_t)len) : "cannot read the file";
        free(owned);
        if (!err) next_count++;
        else if (errors && errors_len) {
            const char *name = src[k].name ? src[k].name : src[k].path ? src[k].path : "patch";
            for (const char *c = name; *c; c++) if (*c == '/' || *c == '\\') name = c + 1;
            size_t used = strlen(errors);
            snprintf(errors + used, errors_len - used, "%s%s: %s", used ? "\n" : "", name, err);
        }
    }
    pending = memcmp(next, cart.prg, size) != 0 || next_count != applied || next_mapper != cart.mapper;
    next_signature = signature_of(next, next_mapper);
    patch_live_wait = pending && live;
    return next_count;
}

int patch_needs_restart(void) { return pending && next_mapper != base_mapper && next_signature != boot_image; }
void patch_powered(void) { boot_image = signature; }
uint32_t patch_boot_image(void) { return boot_image; }
void patch_set_boot_image(uint32_t image) { boot_image = image; }

int patch_prepare(const char *const *paths, int n, char *errors, size_t errors_len) {
    patch_src_t src[64];
    if (n > 64) n = 64;
    for (int k = 0; k < n; k++) { src[k].name = paths[k]; src[k].path = paths[k]; src[k].data = NULL; src[k].len = 0; }
    return patch_prepare_list(src, n, errors, errors_len);
}

int patch_pending(void) { return pending; }
void patch_live(int on) { live = on; patch_live_wait = pending && live; }
unsigned patch_generation(void) { return generation; }
uint32_t patch_signature(void) { return signature; }

/* ---- is this a moment at which the image can change under the running game? ----
 *
 * The CPU must not find itself in the middle of something that is no longer there. Three things
 * are looked at, each where it is mapped now against where the same address is mapped after the
 * switch:
 *
 * - Every return address on the stack. The runtime notes what pushed each one and from which
 *   bank (stk_kind, stk_bank). For a call (JSR, or BRK: the game's calls into other banks) the
 *   call itself must be the same bytes in both images: then the new image makes the same call
 *   from the same place and has its own continuation right behind it, changed or not. For an
 *   interrupt the instruction it will resume must be the same.
 * - The program counter. Right after a JSR it stands at the start of a routine, and the check of
 *   that call covers it. Right after a return it stands behind a call, which must be the same in
 *   both images; failing that, the bytes it stands on must be.
 * - The memory map, when the patch changes the board: both must be able to show the one in force.
 *
 * Addresses the game pushed by hand are data to this check. After a saved state from before the
 * stack was annotated, a slot's origin is unknown: such a slot counts as an address of every
 * bank that has a call just before it, and the bytes behind it must be the same.
 */

/* can 8 KiB bank p answer at slot s (0-3: $8000, $A000, $C000, $E000) in the image running? */
static int plausible(uint32_t p, int s) {
    if (cart.mapper == 4) return s < 2 || p == n8 - 4 + (uint32_t)s;
    if ((p & 1) != (uint32_t)(s & 1)) return 0;                       /* MMC1: 16 KiB banks */
    int last16 = ((p >> 1) & 15) == 15 || (p >> 1) == n8 / 2 - 1;     /* the fixed bank of a 256 KiB half */
    return s < 2 ? 1 : last16;
}

/* the bank of the new image that answers at slot s after the switch, where bank p answers now */
static uint32_t bank_after(uint32_t p, int s) {
    if (s >= 2 && next_mapper == 4 && cart.mapper != 4) return n8 - 4 + (uint32_t)s;   /* one fixed bank, the last */
    return p;
}

/* are the bytes a+from .. a+to of bank p (at slot s) the same after the switch? (within the bank) */
static int same(uint32_t p, int s, uint16_t a, int from, int to) {
    uint32_t q = bank_after(p, s);
    for (int k = from; k <= to; k++) {
        int o = (int)(a & (BANK8 - 1)) + k;
        if (o < 0 || o >= (int)BANK8) return 0;
        if (cart.prg[p * BANK8 + (uint32_t)o] != next[q * BANK8 + (uint32_t)o]) return 0;
    }
    return 1;
}

static uint8_t now_at(uint32_t p, uint16_t a, int k) {
    int o = (int)(a & (BANK8 - 1)) + k;
    return o < 0 || o >= (int)BANK8 ? 0xFF : cart.prg[p * BANK8 + (uint32_t)o];
}

static int pc_ok(int after_xfer) {
    uint16_t pc = cpu.pc;
    if (pc < 0x8000) return 1;
    int s = (pc >> 13) & 3;
    uint32_t p = mapper.prg_bank[s] % n8;
    if (same(p, s, pc, 0, 3)) return 1;
    switch (after_xfer) {
    case 1:     /* at the start of a routine: the call that came here is on top of the stack and checked there */
        return stk_kind[(uint8_t)(cpu.sp + 1)] == STK_JSR;
    case 2:     /* behind a JSR, or behind a BRK and the two or three bytes the game puts after it */
    case 3:
        if (now_at(p, pc, -3) == 0x20 || now_at(p, pc, -3) == 0x00) return same(p, s, pc, -3, -1);
        if (now_at(p, pc, -4) == 0x00) return same(p, s, pc, -4, -1);
        return 0;
    }
    return 0;
}

static int return_ok(int i) {
    uint8_t kind = stk_kind[i];
    if (kind == STK_DATA || i == 0xFF) return 1;
    uint16_t w = (uint16_t)(ram[0x100 + i] | (ram[0x101 + i] << 8));
    if (w < 0x8000) return 1;
    int s = (w >> 13) & 3;
    uint32_t p = stk_bank[i];
    if (kind == STK_UNKNOWN || p >= n8) {
        for (p = 0; p < n8; p++) {
            if (!plausible(p, s)) continue;
            if (p != mapper.prg_bank[s] % n8 && now_at(p, w, -2) != 0x20 && now_at(p, w, -2) != 0x00) continue;
            if (!same(p, s, w, 0, 4)) return 0;
        }
        return 1;
    }
    if (kind == STK_JSR) return same(p, s, w, -2, 0);
    if (kind == STK_BRK) {
        /* the game's call into another bank: BRK and two bytes, or three, with the address left on
           (or moved to) the last of them; it comes back through RTS */
        if (now_at(p, w, -2) == 0x00) return same(p, s, w, -2, now_at(p, w, -3) == 0x00 ? 0 : 1) && (now_at(p, w, -3) != 0x00 || same(p, s, w, -3, 0));
        if (now_at(p, w, -3) == 0x00) return same(p, s, w, -3, 0);
        return same(p, s, w, -4, 1);
    }
    return same(p, s, w, 0, 3);
}

static void put_in_place(int force);

int patch_try(int after_xfer) {
    if (!base || !pending) { pending = patch_live_wait = 0; return 1; }
    if (patch_needs_restart()) { patch_refused[3]++; return 0; }      /* it has to start with the game */
    uint16_t after[4];
    if (!mapper_convertible(next_mapper, after)) { patch_refused[0]++; return 0; }
    if (!pc_ok(after_xfer)) {
        patch_refused[1]++; patch_refused_at = cpu.pc;
        if (sys.debug_log) fprintf((FILE *)sys.debug_log, "patch: no, pc $%04X after %d (bank %d)\n", cpu.pc, after_xfer, mapper.prg_bank[(cpu.pc >> 13) & 3]);
        return 0;
    }
    for (int i = cpu.sp + 1; i <= 0xFF; i++)
        if (!return_ok(i)) {
            patch_refused[2]++; patch_refused_at = (uint16_t)(ram[0x100 + i] | (ram[0x100 + ((i + 1) & 0xFF)] << 8));
            if (sys.debug_log) fprintf((FILE *)sys.debug_log, "patch: no, stack $%02X holds $%04X kind %d bank %d (pc $%04X, mapper %d)\n",
                                       i, patch_refused_at, stk_kind[i], stk_bank[i], cpu.pc, cart.mapper);
            return 0;
        }
    put_in_place(0);
    return 1;
}

int patch_switch(int force) {
    if (!base || !pending) { pending = patch_live_wait = 0; return 1; }
    if (!force) return patch_try(0);
    put_in_place(1);
    return 1;
}

static void put_in_place(int force) {
    memcpy(cart.prg, next, size);
    if (next_mapper != cart.mapper) {
        if (force) cart.mapper = next_mapper;      /* the machine is about to be powered on */
        else {
            /* the return addresses on the stack now belong to the banks that answer for them on the new board */
            for (int i = 0; i < 0xFF; i++) {
                uint16_t w = (uint16_t)(ram[0x100 + i] | (ram[0x101 + i] << 8));
                if (stk_kind[i] != STK_DATA && stk_kind[i] != STK_UNKNOWN && w >= 0x8000 && stk_bank[i] < n8)
                    stk_bank[i] = (uint8_t)bank_after(stk_bank[i], (w >> 13) & 3);
            }
            mapper_convert(next_mapper);
        }
    }
    changed = 0;
    for (uint32_t i = 0; i < size; i++) { dirty[i] = next[i] != base[i]; changed += dirty[i]; }
    signature = signature_of(next, cart.mapper);
    applied = next_count;
    routines_off = changed ? recomp_patch(dirty, size) : recomp_patch(NULL, 0);
    pending = patch_live_wait = 0;
    generation++;
}

int patch_count(void) { return applied; }
long patch_bytes(void) { return changed; }
int patch_routines_off(void) { return routines_off; }
int patch_board_changed(void) { return base && cart.mapper != base_mapper; }
