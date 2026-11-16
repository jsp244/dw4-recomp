/*
 * capture.c - sfc-capture: runs a Super Famicom game without a window, pressing the buttons a
 * script names, and writes the machine's memory at chosen frames: what tools/sfc cuts the
 * 16-bit textures' art from. The emulation is LakeSnes (third_party/lakesnes, MIT license).
 *
 *   sfc-capture ROM [--frames N] [--keys FILE] [--captures FILE DIR] [--capture FRAME PREFIX]...
 *               [--ppm FRAME FILE]... [--ppm-every N PREFIX] [--state-load FILE] [--state-save FILE]
 *               [--holds FILE]
 *
 *   --keys FILE          lines "frame buttons": the buttons held from that frame until the next
 *                        line (A B X Y L R, s = Select, S = Start, u d l r; "-" = none)
 *   --captures FILE DIR  lines "frame name": write DIR/name (see --capture) after that frame
 *   --capture FRAME P    write the memory after frame FRAME: P (the 128 KiB of work RAM), P.vram
 *                        (64 KiB), P.cgram (the 256 colors), P.oam (544 bytes) and P.regs (the
 *                        PPU's layer registers as text); tools/sfc/snesdump.py reads the set
 *   --ppm FRAME FILE     write that frame's picture (256x224) as a PPM file
 *   --state-save FILE    write the machine's state after the last frame; --state-load starts from one
 *   --holds FILE         lines "frame address value" (address and value in hex): from that frame on
 *                        the byte at that work RAM address is set to the value before every frame.
 *                        A script that walks through a cave keeps its hero's hit points that way:
 *                        the battles on the way are then only button presses, whatever they cost
 *
 * The same ROM and the same script give the same memory every time, which is what lets a script
 * of button presses stand for "walk to the castle and look around".
 */
#include "snes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 4096
#define MAX_KEYS 65536
#define MAX_HOLDS 32

static Snes *sn;

static uint8_t *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)*len + 1);
    if (!d || fread(d, 1, (size_t)*len, f) != (size_t)*len) { fprintf(stderr, "%s: cannot read\n", path); exit(1); }
    fclose(f);
    return d;
}

static void write_ppm(const char *path) {
    static uint8_t px[512 * 480 * 4];
    snes_setPixels(sn, px);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n256 224\n255\n");
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < 256; x++) {
            const uint8_t *q = &px[((y * 2 + 16) * 512 + x * 2) * 4];   /* XRGB, low byte first: B G R X */
            fputc(q[2], f); fputc(q[1], f); fputc(q[0], f);
        }
    fclose(f);
}

static void write_memory(const char *prefix) {
    char p[1200];
    Ppu *pp = sn->ppu;
    FILE *f = fopen(prefix, "wb");
    if (!f) { perror(prefix); return; }
    fwrite(sn->ram, 1, 0x20000, f); fclose(f);
    snprintf(p, sizeof p, "%s.vram", prefix);
    if ((f = fopen(p, "wb"))) { fwrite(pp->vram, 2, 0x8000, f); fclose(f); }
    snprintf(p, sizeof p, "%s.cgram", prefix);
    if ((f = fopen(p, "wb"))) { fwrite(pp->cgram, 2, 0x100, f); fclose(f); }
    snprintf(p, sizeof p, "%s.oam", prefix);
    if ((f = fopen(p, "wb"))) { fwrite(pp->oam, 2, 0x100, f); fwrite(pp->highOam, 1, 0x20, f); fclose(f); }
    snprintf(p, sizeof p, "%s.regs", prefix);
    if ((f = fopen(p, "w"))) {
        fprintf(f, "objSize %d objTileAdr1 %04X objTileAdr2 %04X\n", pp->objSize, pp->objTileAdr1, pp->objTileAdr2);
        fprintf(f, "mode %d bg3priority %d brightness %d forcedBlank %d\n", pp->mode, pp->bg3priority, pp->brightness, pp->forcedBlank);
        for (int b = 0; b < 4; b++)
            fprintf(f, "bg %d hScroll %d vScroll %d wider %d higher %d tilemapAdr %04X tileAdr %04X big %d\n", b, pp->bgLayer[b].hScroll,
                    pp->bgLayer[b].vScroll, pp->bgLayer[b].tilemapWider, pp->bgLayer[b].tilemapHigher, pp->bgLayer[b].tilemapAdr,
                    pp->bgLayer[b].tileAdr, pp->bgLayer[b].bigTiles);
        for (int b = 0; b < 5; b++) fprintf(f, "layer %d main %d sub %d\n", b, pp->layer[b].mainScreenEnabled, pp->layer[b].subScreenEnabled);
        fclose(f);
    }
}

static int button_of(char c) {
    switch (c) {
    case 'B': return 0; case 'Y': return 1; case 's': return 2; case 'S': return 3;
    case 'u': return 4; case 'd': return 5; case 'l': return 6; case 'r': return 7;
    case 'A': return 8; case 'X': return 9; case 'L': return 10; case 'R': return 11;
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: sfc-capture ROM [options] (see tools/sfc/capture.c)\n"); return 2; }
    static struct { int kind; unsigned f; char path[1024]; } ev[MAX_EVENTS];        /* 0 picture, 1 memory */
    static struct { unsigned f; uint16_t b; } keys[MAX_KEYS];
    static struct { unsigned f; uint32_t a; uint8_t v; } holds[MAX_HOLDS];
    int nev = 0, nkeys = 0, nholds = 0;
    unsigned frames = 600, start = 0, every = 0;
    const char *state_load = NULL, *state_save = NULL, *every_prefix = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--state-load") && i + 1 < argc) state_load = argv[++i];
        else if (!strcmp(argv[i], "--state-save") && i + 1 < argc) state_save = argv[++i];
        else if (!strcmp(argv[i], "--ppm-every") && i + 2 < argc) { every = (unsigned)atoi(argv[i + 1]); every_prefix = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
            FILE *k = fopen(argv[++i], "r");
            char ln[256];
            if (!k) { perror(argv[i]); return 1; }
            while (fgets(ln, sizeof ln, k) && nkeys < MAX_KEYS) {
                unsigned f; char b[64] = "";
                if (sscanf(ln, "%u %63s", &f, b) < 1) continue;
                uint16_t m = 0;
                for (char *c = b; *c; c++) { int bt = button_of(*c); if (bt >= 0) m |= (uint16_t)(1 << bt); }
                keys[nkeys].f = f; keys[nkeys].b = m; nkeys++;
            }
            fclose(k);
        } else if (!strcmp(argv[i], "--holds") && i + 1 < argc) {
            FILE *k = fopen(argv[++i], "r");
            char ln[256];
            if (!k) { perror(argv[i]); return 1; }
            while (fgets(ln, sizeof ln, k) && nholds < MAX_HOLDS) {
                unsigned fr, a, v;
                if (ln[0] == '#' || sscanf(ln, "%u %x %x", &fr, &a, &v) != 3) continue;
                holds[nholds].f = fr; holds[nholds].a = a & 0x1FFFF; holds[nholds].v = (uint8_t)v; nholds++;
            }
            fclose(k);
        } else if (!strcmp(argv[i], "--captures") && i + 2 < argc) {
            FILE *k = fopen(argv[i + 1], "r");
            char ln[512];
            if (!k) { perror(argv[i + 1]); return 1; }
            while (fgets(ln, sizeof ln, k) && nev < MAX_EVENTS) {
                unsigned f; char name[256];
                if (ln[0] == '#' || sscanf(ln, "%u %255s", &f, name) != 2) continue;
                ev[nev].kind = 1; ev[nev].f = f;
                snprintf(ev[nev].path, sizeof ev[nev].path, "%s/%s", argv[i + 2], name);
                nev++;
            }
            fclose(k);
            i += 2;
        } else if ((!strcmp(argv[i], "--ppm") || !strcmp(argv[i], "--capture") || !strcmp(argv[i], "--ram-dump")) && i + 2 < argc && nev < MAX_EVENTS) {
            ev[nev].kind = strcmp(argv[i], "--ppm") != 0;
            ev[nev].f = (unsigned)atoi(argv[i + 1]);
            snprintf(ev[nev].path, sizeof ev[nev].path, "%s", argv[i + 2]);
            nev++; i += 2;
        } else { fprintf(stderr, "sfc-capture: what is %s?\n", argv[i]); return 2; }
    }
    long len;
    uint8_t *rom = read_file(argv[1], &len);
    sn = snes_init();
    if (!snes_loadRom(sn, rom, (int)len)) { fprintf(stderr, "%s: not a ROM this can run\n", argv[1]); return 1; }
    snes_setPixelFormat(sn, pixelFormatXRGB);
    if (state_load) {
        long n;
        uint8_t *d = read_file(state_load, &n);
        if (!snes_loadState(sn, d, (int)n)) { fprintf(stderr, "%s: not a state of this ROM\n", state_load); return 1; }
        free(d);
    }
    int ki = 0;
    uint16_t held = 0;
    for (unsigned frame = start; frame < frames; frame++) {
        while (ki < nkeys && keys[ki].f <= frame) { held = keys[ki].b; ki++; }
        for (int b = 0; b < 12; b++) snes_setButtonState(sn, 1, b, (held >> b) & 1);
        for (int h = 0; h < nholds; h++) if (frame >= holds[h].f) sn->ram[holds[h].a] = holds[h].v;
        snes_runFrame(sn);
        if (every && frame % every == 0) { char p[1100]; snprintf(p, sizeof p, "%s.%06u.ppm", every_prefix, frame); write_ppm(p); }
        for (int e = 0; e < nev; e++) {
            if (ev[e].f != frame) continue;
            if (ev[e].kind) write_memory(ev[e].path); else write_ppm(ev[e].path);
        }
    }
    if (state_save) {
        static uint8_t buf[4 << 20];
        int n = snes_saveState(sn, buf);
        FILE *f = fopen(state_save, "wb");
        if (f) { fwrite(buf, 1, (size_t)n, f); fclose(f); }
    }
    printf("sfc-capture: %u frames\n", frames);
    return 0;
}
