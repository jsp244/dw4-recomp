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

static uint8_t *dirty;          /* per PRG byte: changed from the dump */
static int applied, routines_off;
static long changed;

void patch_reset(void) {
    free(dirty);
    dirty = NULL;
    applied = routines_off = 0;
    changed = 0;
    recomp_patch(NULL, 0);
}

static int put(uint32_t file_off, uint8_t v, long *n) {
    if (file_off < HEADER) return 1;                       /* the header is not part of the image */
    uint32_t o = file_off - HEADER;
    if (o >= cart.prg_size) return 0;
    if (cart.prg[o] != v) {
        cart.prg[o] = v;
        if (!dirty[o]) { dirty[o] = 1; changed++; }
        (*n)++;
    }
    return 1;
}

long patch_apply_ips(const uint8_t *ips, size_t len, const char **err) {
    static const char *e_format = "not an IPS patch", *e_big = "the patch reaches beyond the ROM (bigger ROMs are not supported)",
                      *e_cart = "no cartridge loaded", *e_mem = "out of memory";
    const char *dummy;
    if (!err) err = &dummy;
    *err = NULL;
    if (!cart.prg || !cart.prg_size) { *err = e_cart; return -1; }
    if (len < 8 || memcmp(ips, "PATCH", 5) != 0) { *err = e_format; return -1; }
    if (!dirty) {
        dirty = calloc(cart.prg_size, 1);
        if (!dirty) { *err = e_mem; return -1; }
    }
    /* first pass: is every record inside the ROM? Nothing is changed by a patch that does not fit. */
    for (int pass = 0; pass < 2; pass++) {
        size_t p = 5;
        long n = 0;
        for (;;) {
            if (p + 3 > len) { *err = e_format; return -1; }
            if (!memcmp(ips + p, "EOF", 3)) break;
            uint32_t off = ((uint32_t)ips[p] << 16) | ((uint32_t)ips[p + 1] << 8) | ips[p + 2];
            if (p + 5 > len) { *err = e_format; return -1; }
            uint32_t size = ((uint32_t)ips[p + 3] << 8) | ips[p + 4];
            p += 5;
            if (size) {
                if (p + size > len) { *err = e_format; return -1; }
                if (off + size > HEADER + cart.prg_size) { *err = e_big; return -1; }
                if (pass)
                    for (uint32_t i = 0; i < size; i++) put(off + i, ips[p + i], &n);
                p += size;
            } else {
                if (p + 3 > len) { *err = e_format; return -1; }
                uint32_t run = ((uint32_t)ips[p] << 8) | ips[p + 1];
                if (off + run > HEADER + cart.prg_size) { *err = e_big; return -1; }
                if (pass)
                    for (uint32_t i = 0; i < run; i++) put(off + i, ips[p + 2], &n);
                p += 3;
            }
        }
        if (pass) { applied++; return n; }
    }
    return 0;
}

long patch_apply_file(const char *path, const char **err) {
    static const char *e_open = "cannot read the file";
    FILE *f = nes_fopen(path, "rb");
    if (!f) { if (err) *err = e_open; return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = len > 0 && len < (16 << 20) ? malloc((size_t)len) : NULL;
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); if (err) *err = e_open; return -1; }
    fclose(f);
    long n = patch_apply_ips(buf, (size_t)len, err);
    free(buf);
    return n;
}

int patch_commit(void) {
    routines_off = changed ? recomp_patch(dirty, cart.prg_size) : recomp_patch(NULL, 0);
    return routines_off;
}

int patch_count(void) { return applied; }
long patch_bytes(void) { return changed; }
int patch_routines_off(void) { return routines_off; }
