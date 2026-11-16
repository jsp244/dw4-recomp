/*
 * headless.c - command-line harness: runs the machine without a window,
 * for test ROMs, scripted input, screenshots, traces, coverage files and
 * interpreter-vs-recompiled cosimulation.
 */
#include "nes.h"
#include "png.h"
#include "palette.h"
#include "remaster.h"
#include "widescreen.h"
#include "sfc.h"
#include "patch.h"
#include "title.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *trace_file;
static uint32_t pal[512];
static int watch_addr = -1;
static void trace_hook(void) {
    fprintf(trace_file, "%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu",
            cpu.pc, cpu.a, cpu.x, cpu.y, cpu_get_p(0), cpu.sp, (unsigned long long)cpu.cycles);
    fprintf(trace_file, " B:%02X,%02X", mapper.prg_bank[0] >> 1, mapper.prg_bank[2] >> 1);   /* 16 KiB banks at $8000 / $C000 */
    if (watch_addr >= 0) fprintf(trace_file, " W:%02X", mem_read((uint16_t)watch_addr));
    fputc('\n', trace_file);
}

static uint8_t parse_buttons(const char *s) {
    uint8_t b = 0;
    while (*s) {
        const char *end = s;
        while (*end && *end != '+' && *end != ',') end++;
        size_t n = (size_t)(end - s);
        #define BTN(name, bit) if (n == strlen(name) && !strncmp(s, name, n)) b |= (bit)
        BTN("A", 0x01); BTN("B", 0x02); BTN("SELECT", 0x04); BTN("START", 0x08);
        BTN("UP", 0x10); BTN("DOWN", 0x20); BTN("LEFT", 0x40); BTN("RIGHT", 0x80);
        #undef BTN
        s = *end ? end + 1 : end;
    }
    return b;
}

typedef struct { int frame; uint8_t buttons; } input_event_t;
static input_event_t events[4096];
static int n_events;

static void add_event(int frame, uint8_t b) {
    if (n_events < 4096) { events[n_events].frame = frame; events[n_events].buttons = b; n_events++; }
}

static void load_input_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open input script %s\n", path); exit(1); }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int frame; char btn[200];
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%d %199s", &frame, btn) == 2) add_event(frame, parse_buttons(btn));
    }
    fclose(f);
}

static void parse_keys(const char *spec) {
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", spec);
    char *p = buf;
    while (*p) {
        char *semi = strchr(p, ';');
        if (semi) *semi = 0;
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = 0;
            add_event(atoi(p), parse_buttons(colon + 1));
        }
        if (!semi) break;
        p = semi + 1;
    }
}

/* --fuzz: random play. Walks about (a direction held for a while), presses A and B, now and then
   Start or Select, with a released frame between presses so each one registers. The same seed
   gives the same session, so the interpreter and the recompiled code can be compared over it. */
static uint32_t fuzz_state;
static int fuzz_left, fuzz_gap;
static uint8_t fuzz_btn;

static uint32_t fuzz_rnd(void) {
    fuzz_state ^= fuzz_state << 13; fuzz_state ^= fuzz_state >> 17; fuzz_state ^= fuzz_state << 5;
    return fuzz_state >> 8;
}

static uint8_t fuzz_pad(void) {
    if (fuzz_left <= 0) {
        uint32_t r = fuzz_rnd() % 100;
        if (r < 50)      { fuzz_btn = (uint8_t)(0x10 << (fuzz_rnd() & 3)); fuzz_left = 4 + (int)(fuzz_rnd() % 60); }
        else if (r < 70) { fuzz_btn = 0x01; fuzz_left = 2 + (int)(fuzz_rnd() % 6); }
        else if (r < 90) { fuzz_btn = 0x02; fuzz_left = 2 + (int)(fuzz_rnd() % 6); }
        else if (r < 92) { fuzz_btn = 0x08; fuzz_left = 3; }
        else if (r < 93) { fuzz_btn = 0x04; fuzz_left = 3; }
        else             { fuzz_btn = 0; fuzz_left = 2 + (int)(fuzz_rnd() % 20); }
        fuzz_gap = 1;
    }
    if (fuzz_gap) { fuzz_gap = 0; return 0; }
    fuzz_left--;
    return fuzz_btn;
}

/* --apu-log: the writes to each sound register, the values written to $4015 (channel enables) and
   $4017 (frame counter), and per channel the frames in which it could be heard */
static unsigned long apu_writes[0x18], apu_4015[32], apu_4017[4], apu_heard[5], apu_log_frames;
static int apu_log_on;

static void apu_log_hook(uint16_t a, uint8_t v) {
    if (a >= 0x4000 && a < 0x4018) apu_writes[a - 0x4000]++;
    if (a == 0x4015) apu_4015[v & 31]++;
    if (a == 0x4017) apu_4017[v >> 6]++;
}

static void apu_log_frame(void) {
    apu_log_frames++;
    for (int i = 0; i < 2; i++) {
        const apu_pulse_t *p = &apu.pulse[i];
        apu_heard[i] += p->length > 0 && p->timer_period >= 8 && (p->const_volume ? p->volume : p->env_decay) > 0;
    }
    apu_heard[2] += apu.tri.length > 0 && apu.tri.linear > 0 && apu.tri.timer_period >= 2;
    apu_heard[3] += apu.noise.length > 0 && (apu.noise.const_volume ? apu.noise.volume : apu.noise.env_decay) > 0;
    apu_heard[4] += apu.dmc.bytes_remaining > 0;
}

static void apu_log_print(void) {
    static const char *const ch[5] = { "pulse 1", "pulse 2", "triangle", "noise", "DMC" };
    printf("apu-log: writes per register $4000-$4017:");
    for (int i = 0; i < 0x18; i++) printf("%s%lu", i % 4 ? " " : " | ", apu_writes[i]);
    printf("\napu-log: $4015 values written:");
    for (int i = 0; i < 32; i++) if (apu_4015[i]) printf(" %02X x%lu", i, apu_4015[i]);
    printf("\napu-log: $4017 values written (top two bits):");
    for (int i = 0; i < 4; i++) if (apu_4017[i]) printf(" %02X x%lu", i << 6, apu_4017[i]);
    printf("\napu-log: frames in which a channel could be heard, of %lu:", apu_log_frames);
    for (int i = 0; i < 5; i++) printf(" %s %lu", ch[i], apu_heard[i]);
    printf("\n");
}

static void fb_to_rgb(uint8_t *rgb) {
    for (int y = 0; y < 240; y++) {
        int emph = (ppu.line_emph[y] >> 5) & 7;
        for (int x = 0; x < 256; x++) {
            uint32_t c = pal[emph * 64 + (ppu.fb[y * 256 + x] & 0x3F)];
            rgb[(y * 256 + x) * 3] = (uint8_t)(c >> 16);
            rgb[(y * 256 + x) * 3 + 1] = (uint8_t)(c >> 8);
            rgb[(y * 256 + x) * 3 + 2] = (uint8_t)c;
        }
    }
}

/* the widescreen canvas (the PPU's picture with the map beside it, or black bands) as a PNG;
   returns 1 when the bands hold the map */
static int write_widescreen(const char *path, int remastered) {
    static uint8_t idx[WS_W * WS_H], emph[WS_H], rgb[WS_W * WS_H * 3];
    static uint32_t hbg[WS_W * WS_H], hspr[WS_W * WS_H], argb[WS_W * WS_H];
    int on;
    if (remastered) {
        on = widescreen_render_hd(idx, emph, hbg, hspr, 0, 0, WS_W, WS_H);
        remaster_frame(argb, WS_W, idx, hbg, hspr, WS_W, emph, WS_W, WS_H);
    } else {
        on = widescreen_render(idx, emph, 0, 0, WS_W, WS_H);
        for (int y = 0; y < WS_H; y++)
            for (int x = 0; x < WS_W; x++) argb[y * WS_W + x] = pal[((emph[y] >> 5) & 7) * 64 + (idx[y * WS_W + x] & 0x3F)];
    }
    for (int i = 0; i < WS_W * WS_H; i++) {
        rgb[i * 3] = (uint8_t)(argb[i] >> 16); rgb[i * 3 + 1] = (uint8_t)(argb[i] >> 8); rgb[i * 3 + 2] = (uint8_t)argb[i];
    }
    png_write_rgb(path, WS_W, WS_H, rgb);
    return on;
}

/* the canvas's middle against the PPU's own picture: the number of pixels that differ, not counting
   the ones the canvas takes from the map instead (widescreen_inner) */
static long widescreen_middle_diff(void) {
    static uint8_t idx[WS_W * WS_H], emph[WS_H];
    long bad = 0;
    widescreen_render(idx, emph, 0, 0, WS_W, WS_H);
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 256; x++)
            if (widescreen_inner(x, y)) bad += idx[(y + WS_VBAND) * WS_W + WS_BAND + x] != ppu.fb[y * 256 + x];
    return bad;
}

/* the frame with the 16-bit textures as a PNG: the PPU's picture, or the whole widescreen canvas */
static void write_sfc(const char *path, int wide) {
    static uint8_t idx[WS_W * WS_H], emph[WS_H], rgb[WS_W * WS_H * 3];
    static uint32_t hbg[WS_W * WS_H], hspr[WS_W * WS_H], argb[WS_W * WS_H];
    static uint16_t hcell[WS_W * WS_H];
    int x = wide ? 0 : WS_BAND, y = wide ? 0 : WS_VBAND, w = wide ? WS_W : 256, h = wide ? WS_H : 240;
    widescreen_render_cells(idx, emph, hbg, hspr, hcell, x, y, w, h);
    sfc_frame(argb, w, idx, hbg, hspr, hcell, w, emph, w, h, x - WS_BAND, y - WS_VBAND);
    for (int i = 0; i < w * h; i++) {
        rgb[i * 3] = (uint8_t)(argb[i] >> 16); rgb[i * 3 + 1] = (uint8_t)(argb[i] >> 8); rgb[i * 3 + 2] = (uint8_t)argb[i];
    }
    png_write_rgb(path, w, h, rgb);
}

/* the frame with the remastered graphics as a PNG (rgb: a 256x240x3 scratch buffer) */
static void write_remastered(const char *path, uint8_t *rgb) {
    static uint32_t argb[256 * 240];
    remaster_frame(argb, 256, ppu.fb, ppu.hd_bg, ppu.hd_spr, 256, ppu.line_emph, 256, 240);
    for (int i = 0; i < 256 * 240; i++) {
        rgb[i * 3] = (uint8_t)(argb[i] >> 16); rgb[i * 3 + 1] = (uint8_t)(argb[i] >> 8); rgb[i * 3 + 2] = (uint8_t)argb[i];
    }
    png_write_rgb(path, 256, 240, rgb);
}

static uint64_t fnv1a(uint64_t h, const void *data, size_t len) {
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
    return h;
}

static uint64_t frame_hash(void) {
    uint64_t h = 0xcbf29ce484222325ULL;
    uint8_t regs[8] = { cpu.a, cpu.x, cpu.y, cpu.sp, (uint8_t)cpu.pc, (uint8_t)(cpu.pc >> 8), cpu_get_p(0), 0 };
    h = fnv1a(h, regs, sizeof regs);
    h = fnv1a(h, &cpu.cycles, sizeof cpu.cycles);
    h = fnv1a(h, ram, sizeof ram);
    h = fnv1a(h, cart.wram, sizeof cart.wram);
    h = fnv1a(h, stk_kind, sizeof stk_kind);      /* what pushed each stack slot: both ways of running must agree */
    h = fnv1a(h, stk_bank, sizeof stk_bank);
    h = fnv1a(h, ppu.fb, sizeof ppu.fb);
    h = fnv1a(h, ppu.oam, sizeof ppu.oam);
    h = fnv1a(h, ppu.palette, sizeof ppu.palette);
    h = fnv1a(h, ppu.vram, sizeof ppu.vram);
    h = fnv1a(h, mapper.prg_bank, sizeof mapper.prg_bank);
    h = fnv1a(h, mapper.chr_bank, sizeof mapper.chr_bank);
    return h;
}

/* n sample frames of `channels` interleaved samples */
static void write_wav(const char *path, const int16_t *samples, uint32_t n, uint32_t rate, int channels) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data_bytes = n * 2 * (uint32_t)channels;
    uint8_t hdr[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0, 'd','a','t','a', 0,0,0,0 };
    uint32_t riff = 36 + data_bytes, byte_rate = rate * 2 * (uint32_t)channels;
    uint16_t ch = (uint16_t)channels, align = (uint16_t)(2 * channels);
    memcpy(hdr + 4, &riff, 4); memcpy(hdr + 22, &ch, 2); memcpy(hdr + 24, &rate, 4); memcpy(hdr + 28, &byte_rate, 4);
    memcpy(hdr + 32, &align, 2); memcpy(hdr + 40, &data_bytes, 4);
    fwrite(hdr, 1, 44, f);
    fwrite(samples, 2, (size_t)n * (size_t)channels, f);
    fclose(f);
}

static void usage(void) {
    fprintf(stderr,
        "usage: nes-headless --rom FILE [options]\n"
        "  --frames N          run N frames (default 600)\n"
        "  --keys SPEC         input events: 'frame:BTN+BTN;frame:-' (BTN = A B SELECT START UP DOWN LEFT RIGHT, '-' = none)\n"
        "  --ips FILE          apply an IPS patch (a ROM hack) to the loaded ROM; may be given several times,\n"
        "                      applied in that order. With --recomp the routines it touches are interpreted\n"
        "  --ips-toggle N      every N frames switch the patches off, or on again, while the game runs\n"
        "                      (each switch waits for a frame at which no running code is affected)\n"
        "  --title-quest       the Dragon Quest IV logo in place of the Dragon Warrior IV plaque (runtime/title.h)\n"
        "  --ppu-dump FILE     write the PPU's memory after the last frame (name tables, palette, CHR RAM,\n"
        "                      sprites, picture: tools/title reads it)\n"
        "  --sfc FILE          write the last frame with the 16-bit textures (also every N frames); needs\n"
        "  --sfc-pack F        the pack tools/sfc builds from the Super Famicom games\n"
        "  --sfc-wide FILE     the same for the whole widescreen canvas\n"
        "  --sfc-check         every frame: draw the canvas with the 16-bit textures and count what the pack does\n"
        "                      not know; prints the totals and the keys of the squares and figures it lacks\n"
        "  --map-dump FILE     write the whole map the player is on after the last frame, as the game's own map\n"
        "                      code gives it (256 x 256 squares: four tiles and a palette), with the palette, the\n"
        "                      CHR RAM and the game's memory (tools/sfc reads it)\n"
        "  --input FILE        input script: lines of 'frame BTN+BTN'\n"
        "  --fuzz SEED         random controller input (the same for the same seed), from --fuzz-start N (default 0)\n"
        "  --png FILE          write the last frame as PNG\n"
        "  --png-every N       write FILE with the frame number appended every N frames\n"
        "  --widescreen FILE   write the last frame as the widescreen canvas, the map beside the picture\n"
        "                      (also every N frames; with --remaster given, in the remastered graphics)\n"
        "  --ws-check          every frame: ask the map code for the squares on the screen and compare them\n"
        "                      with what the PPU was given to draw; prints the totals\n"
        "  --remaster FILE     write the last frame with the remastered graphics (also every N frames)\n"
        "  --remaster-pack F   the pack of hand-made tiles for --remaster\n"
        "  --hd-dump FILE      write the last frame's pixel sources (tools/remaster): palette, line\n"
        "                      masks, pixels and each layer's source tile per pixel\n"
        "  --wav FILE          write audio to a 48 kHz WAV\n"
        "  --apu-log           print what the game wrote to the sound chip and how much each channel sounded\n"
        "  --mute MASK         leave channels out of the WAV: 1 pulse 1, 2 pulse 2, 4 triangle, 8 noise (hex, sum)\n"
        "  --trace FILE        write a per-instruction trace (interpreter)\n"
        "  --nestest N         nestest mode: start at $C000 and run N instructions\n"
        "  --blargg            poll the blargg test status at $6000 and exit when done\n"
        "  --coverage FILE     write executed-instruction coverage for the recompiler\n"
        "  --recomp            run recompiled code with interpreter fallback\n"
        "  --frame-hash FILE   write a per-frame state hash (for cosimulation diffs)\n"
        "  --sav FILE          load/save battery RAM from FILE\n"
        "  --ram-dump FILE     write RAM + PRG RAM at the end (with --ram-dump-every N: FILE.NNNNN.bin every N frames)\n"
        "  --poke F:ADDR:VAL   before frame F (or every frame of F-G) put the byte VAL at ADDR in the game's memory\n"
        "                      (hex; RAM or battery RAM): tools use it to have the game load a map without\n"
        "                      playing to it. May be given up to 32 times\n"
        "  --quiet             no per-run summary\n");
}

int main(int argc, char **argv) {
    const char *rom = NULL, *png = NULL, *wav = NULL, *trace = NULL, *cov = NULL, *hash = NULL, *sav = NULL, *input = NULL, *debug_log = NULL, *ram_dump = NULL;
    const char *state_save = NULL, *state_load = NULL, *remaster = NULL, *remaster_pack = NULL, *hd_dump = NULL, *ppu_dump = NULL, *map_dump = NULL;
    const char *sfc_png = NULL, *sfc_wide = NULL, *sfc_pack = NULL;
    int sfc_check = 0;
    long sfc_frames = 0, sfc_map_frames = 0, sfc_sq[2] = { 0, 0 }, sfc_fig[2] = { 0, 0 };
    static struct { uint64_t key; long n; int first, scene; } sfc_miss[2][512];
    int sfc_n_miss[2] = { 0, 0 };
    int fuzz_start = 0, ws_check = 0, n_ips = 0, ips_toggle = 0, ips_on = 1;
    long ips_switches = 0, ips_waits = 0, title_frames = 0;
    int title_quest = 0;
    unsigned ips_gen0 = 0;
    const char *ips[16];
    const char *wide = NULL;
    int frames = 600, png_every = 0, ram_dump_every = 0, nestest = 0, blargg = 0, recomp = 0, quiet = 0, trace_start = 0;
    static struct { int first, last, addr, value; } pokes[32];
    int n_pokes = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--ips") && i + 1 < argc && n_ips < 16) ips[n_ips++] = argv[++i];
        else if (!strcmp(argv[i], "--ips-toggle") && i + 1 < argc) ips_toggle = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) fuzz_state = (uint32_t)strtoul(argv[++i], NULL, 0) * 2654435761u + 1;
        else if (!strcmp(argv[i], "--fuzz-start") && i + 1 < argc) fuzz_start = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) png_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--remaster") && i + 1 < argc) remaster = argv[++i];
        else if (!strcmp(argv[i], "--widescreen") && i + 1 < argc) wide = argv[++i];
        else if (!strcmp(argv[i], "--ws-check")) ws_check = 1;
        else if (!strcmp(argv[i], "--remaster-pack") && i + 1 < argc) remaster_pack = argv[++i];
        else if (!strcmp(argv[i], "--hd-dump") && i + 1 < argc) hd_dump = argv[++i];
        else if (!strcmp(argv[i], "--ppu-dump") && i + 1 < argc) ppu_dump = argv[++i];
        else if (!strcmp(argv[i], "--map-dump") && i + 1 < argc) map_dump = argv[++i];
        else if (!strcmp(argv[i], "--sfc") && i + 1 < argc) sfc_png = argv[++i];
        else if (!strcmp(argv[i], "--sfc-wide") && i + 1 < argc) sfc_wide = argv[++i];
        else if (!strcmp(argv[i], "--sfc-pack") && i + 1 < argc) sfc_pack = argv[++i];
        else if (!strcmp(argv[i], "--sfc-check")) sfc_check = 1;
        else if (!strcmp(argv[i], "--title-quest")) title_quest = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--apu-log")) { apu_log_on = 1; apu_write_hook = apu_log_hook; }
        else if (!strcmp(argv[i], "--mute") && i + 1 < argc) { apu_mute_mask = (uint8_t)strtol(argv[++i], NULL, 16); apu_mute_output = true; }
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = argv[++i];
        else if (!strcmp(argv[i], "--nestest") && i + 1 < argc) nestest = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--blargg")) blargg = 1;
        else if (!strcmp(argv[i], "--coverage") && i + 1 < argc) cov = argv[++i];
        else if (!strcmp(argv[i], "--recomp")) recomp = 1;
        else if (!strcmp(argv[i], "--frame-hash") && i + 1 < argc) hash = argv[++i];
        else if (!strcmp(argv[i], "--sav") && i + 1 < argc) sav = argv[++i];
        else if (!strcmp(argv[i], "--state-save") && i + 1 < argc) state_save = argv[++i];
        else if (!strcmp(argv[i], "--state-load") && i + 1 < argc) state_load = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--no-sprite-limit")) ppu_no_sprite_limit = true;
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) watch_addr = (int)strtol(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--trace-start") && i + 1 < argc) trace_start = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--debug-log") && i + 1 < argc) debug_log = argv[++i];
        else if (!strcmp(argv[i], "--ram-dump") && i + 1 < argc) ram_dump = argv[++i];
        else if (!strcmp(argv[i], "--ram-dump-every") && i + 1 < argc) ram_dump_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--poke") && i + 1 < argc && n_pokes < 32) {
            int a = 0, b = 0, addr = 0, value = 0;
            const char *s = argv[++i];
            if (sscanf(s, "%d-%d:%x:%x", &a, &b, &addr, &value) != 4) {
                if (sscanf(s, "%d:%x:%x", &a, &addr, &value) != 3) { usage(); return 2; }
                b = a;
            }
            if (!(addr < 0x800 || (addr >= 0x6000 && addr < 0x8000))) { fprintf(stderr, "--poke: $%04X is not RAM\n", addr); return 2; }
            pokes[n_pokes].first = a; pokes[n_pokes].last = b; pokes[n_pokes].addr = addr; pokes[n_pokes].value = value & 0xFF;
            n_pokes++;
        }
        else { usage(); return 2; }
    }
    if (!rom) { usage(); return 2; }

    apu_set_sample_rate(48000);
    if (nes_load(rom) != 0) return 1;
    if (sav) cart_load_battery(sav);
    patch_reset();
    if (n_ips) {
        char errs[800];
        if (patch_prepare(ips, n_ips, errs, sizeof errs) != n_ips) { fprintf(stderr, "%s\n", errs); return 1; }
        patch_switch(1);
        nes_power();      /* the reset vector and everything else, from the patched image */
        if (sav) cart_load_battery(sav);
        if (!quiet) printf("patches: %d applied, %ld bytes differ from the dump, %d translated routines left to the interpreter\n",
                           patch_count(), patch_bytes(), patch_routines_off());
    }
    patch_live(1);
    ips_gen0 = patch_generation();
    if (state_load) {
        size_t len = nes_state_size();
        void *buf = malloc(len);
        FILE *f = nes_fopen(state_load, "rb");
        size_t n = f ? fread(buf, 1, len, f) : 0;
        if (f) fclose(f);
        if (n != len || nes_state_load(buf, len) != 0) { fprintf(stderr, "cannot load state %s\n", state_load); return 1; }
        free(buf);
        if (!quiet) printf("state loaded from %s (cpu cycle %llu)\n", state_load, (unsigned long long)cpu.cycles);
    }
    nes_palette_build(pal);
    if (input) load_input_script(input);
    if (hd_dump) ppu_hd_capture = true;
    if (remaster) {
        ppu_hd_capture = true;
        remaster_init(remaster_pack);
    }
    if (sfc_png || sfc_wide || sfc_check) {
        ppu_hd_capture = true;
        if (!remaster) remaster_init(remaster_pack);
        if (!sfc_init(sfc_pack)) { fprintf(stderr, "no 16-bit texture pack (--sfc-pack FILE)\n"); return 1; }
    }

    if (recomp) {
        if (!recomp_rom_sha1) { fprintf(stderr, "this build has no recompiled code\n"); return 1; }
        if (strcmp(recomp_rom_sha1, cart.sha1_hex) != 0) {
            fprintf(stderr, "recompiled code was generated from ROM %s but this ROM is %s\n", recomp_rom_sha1, cart.sha1_hex);
            return 1;
        }
        sys.use_recomp = true;
    }
    if (trace) {
        trace_file = !strcmp(trace, "-") ? stdout : fopen(trace, "w");
        if (!trace_file) { fprintf(stderr, "cannot open %s\n", trace); return 1; }
        if (trace_start == 0) sys.trace_hook = trace_hook;
    }
    if (debug_log) sys.debug_log = fopen(debug_log, "w");
    if (cov) {
        sys.cov = calloc(cart.prg_size + 0x8000, 1);
        /* load an existing coverage file so runs accumulate */
        FILE *f = fopen(cov, "rb");
        if (f) { size_t n = fread(sys.cov, 1, cart.prg_size + 0x8000, f); (void)n; fclose(f); }
    }

    if (nestest) {
        cpu.pc = 0xC000;
        for (int i = 0; i < nestest; i++) {
            if (cpu.int_pending) cpu_take_interrupt();
            cpu_step();
        }
        if (trace_file && trace_file != stdout) fclose(trace_file);
        printf("nestest: $02=%02X $03=%02X\n", ram[2], ram[3]);
        return 0;
    }

    FILE *hash_file = hash ? fopen(hash, "w") : NULL;
    int channels = 1;
    int16_t *audio = wav ? malloc(sizeof(int16_t) * 48000u * (size_t)channels * (size_t)(frames / 60 + 2)) : NULL;
    uint32_t audio_n = 0, audio_cap = wav ? 48000u * (uint32_t)(frames / 60 + 2) : 0;
    uint8_t *rgb = malloc(256 * 240 * 3);
    int ev = 0;
    long ws_middle = 0;
    long ws_frames = 0, ws_map_frames = 0, ws_cells = 0, ws_bad = 0, ws_bad_frames = 0, ws_wide_on = 0, ws_wide = 0;
    int blargg_result = -1;
    int reset_hold = 0;

    for (int frame = 0; frame < frames; frame++) {
        if (trace_file && trace_start && frame == trace_start) sys.trace_hook = trace_hook;
        while (ev < n_events && events[ev].frame <= frame) { nes_set_pad(0, events[ev].buttons); ev++; }
        if (fuzz_state && frame >= fuzz_start) nes_set_pad(0, fuzz_pad());
        if (ips_toggle > 0 && n_ips && frame > 0 && frame % ips_toggle == 0) {
            ips_on = !ips_on;
            patch_prepare(ips, ips_on ? n_ips : 0, NULL, 0);
        }
        for (int k = 0; k < n_pokes; k++)
            if (frame >= pokes[k].first && frame <= pokes[k].last) {
                if (pokes[k].addr < 0x800) ram[pokes[k].addr] = (uint8_t)pokes[k].value;
                else cart.wram[pokes[k].addr - 0x6000] = (uint8_t)pokes[k].value;
            }
        nes_run_frame();
        if (patch_pending()) {                 /* a whole frame without a safe moment */
            ips_waits++;
            if (!quiet && ips_waits % 500 == 1)
                printf("frame %d: switch waiting (map %ld, pc %ld, stack %ld; last at $%04X, pc $%04X, sp $%02X, banks %d/%d)\n", frame,
                       patch_refused[0], patch_refused[1], patch_refused[2], patch_refused_at, cpu.pc, cpu.sp,
                       mapper.prg_bank[0] >> 1, mapper.prg_bank[2] >> 1);
            if (!quiet && ips_waits % 500 == 1) { for (int i = cpu.sp + 1; i <= 0xFF; i++) printf("%02X ", ram[0x100 + i]); printf("\n"); }
        }
        if (apu_log_on) apu_log_frame();
        if (hash_file) fprintf(hash_file, "%d %016llx\n", frame, (unsigned long long)frame_hash());
        if (audio) {
            uint32_t avail = apu_samples_available();
            if (audio_n + avail > audio_cap) avail = audio_cap - audio_n;
            audio_n += apu_read_samples(audio + audio_n, avail);
        } else {
            apu.ring_read = apu.ring_write;
        }
        if (ram_dump && ram_dump_every && (frame % ram_dump_every) == 0) {
            char name[1100];
            snprintf(name, sizeof name, "%s.%05d.bin", ram_dump, frame);
            FILE *f = fopen(name, "wb");
            if (f) { fwrite(ram, 1, sizeof ram, f); fwrite(cart.wram, 1, sizeof cart.wram, f); fclose(f); }
        }
        if (title_quest && title_plaque()) { title_draw(); title_frames++; }      /* (after the hash: the picture only) */
        if (png && png_every && (frame % png_every) == 0) {
            char name[1100];
            snprintf(name, sizeof name, "%s.%05d.png", png, frame);
            fb_to_rgb(rgb);
            png_write_rgb(name, 256, 240, rgb);
        }
        if (ws_check) {
            int cells, bad = widescreen_check(&cells);
            ws_frames++;
            ws_middle += widescreen_middle_diff();
            if (bad >= 0) { ws_map_frames++; ws_cells += cells; ws_bad += bad; ws_bad_frames += bad > 0; }
            if (bad > 0 && !quiet) printf("frame %d: %d of %d squares differ from the screen (columns %d-%d, rows %d-%d)\n", frame, bad, cells,
                                          widescreen_bad_box[0], widescreen_bad_box[1], widescreen_bad_box[2], widescreen_bad_box[3]);
        }
        if (wide && png_every && (frame % png_every) == 0) {
            char name[1100];
            snprintf(name, sizeof name, "%s.%05d.png", wide, frame);
            ws_wide_on += write_widescreen(name, remaster != NULL); ws_wide++;
        }
        if (remaster && png_every && (frame % png_every) == 0) {
            char name[1100];
            snprintf(name, sizeof name, "%s.%05d.png", remaster, frame);
            write_remastered(name, rgb);
        }
        if (sfc_check) {
            static uint8_t idx[WS_W * WS_H], emph[WS_H];
            static uint32_t hbg[WS_W * WS_H], hspr[WS_W * WS_H], argb[WS_W * WS_H];
            static uint16_t hcell[WS_W * WS_H];
            int on = widescreen_render_cells(idx, emph, hbg, hspr, hcell, 0, 0, WS_W, WS_H), k[4], n[2];
            uint64_t keys[2][64];
            sfc_frame(argb, WS_W, idx, hbg, hspr, hcell, WS_W, emph, WS_W, WS_H, -WS_BAND, -WS_VBAND);
            sfc_stats(&k[0], &k[1], &k[2], &k[3]);
            sfc_unknown(keys[0], keys[1], 64, n);
            sfc_frames++;
            if (on) {       /* (other screens, battles and menus, are not the map's) */
                sfc_map_frames++;
                sfc_sq[0] += k[0]; sfc_sq[1] += k[1]; sfc_fig[0] += k[2]; sfc_fig[1] += k[3];
                for (int t = 0; t < 2; t++)
                    for (int i = 0; i < n[t]; i++) {
                        int j = 0;
                        while (j < sfc_n_miss[t] && sfc_miss[t][j].key != keys[t][i]) j++;
                        if (j == sfc_n_miss[t]) {
                            if (j == 512) continue;
                            sfc_miss[t][j].key = keys[t][i]; sfc_miss[t][j].n = 0; sfc_miss[t][j].first = frame;
                            sfc_miss[t][j].scene = (ram[0x41] & 0x80) ? 0 : 1;
                            sfc_n_miss[t]++;
                        }
                        sfc_miss[t][j].n++;
                    }
            }
        }
        if ((sfc_png || sfc_wide) && png_every && (frame % png_every) == 0) {
            char name[1100];
            if (sfc_png) { snprintf(name, sizeof name, "%s.%05d.png", sfc_png, frame); write_sfc(name, 0); }
            if (sfc_wide) { snprintf(name, sizeof name, "%s.%05d.png", sfc_wide, frame); write_sfc(name, 1); }
        }
        if (blargg && reset_hold > 0) { reset_hold--; continue; }
        if (blargg && cart.wram[1] == 0xDE && cart.wram[2] == 0xB0 && cart.wram[3] == 0x61) {
            uint8_t st = cart.wram[0];
            if (st == 0x81) {
                /* the test asks for a reset after ~100 ms; give it a few frames, then ignore status briefly */
                for (int k = 0; k < 8; k++) nes_run_frame();
                nes_reset();
                reset_hold = 10;
                continue;
            }
            if (st != 0x80) {
                blargg_result = st;
                printf("blargg: status %02X\n%s\n", st, (const char *)cart.wram + 4);
                break;
            }
        }
    }

    if (png) { fb_to_rgb(rgb); png_write_rgb(png, 256, 240, rgb); }
    if (remaster) write_remastered(remaster, rgb);
    if (sfc_png) write_sfc(sfc_png, 0);
    if (sfc_wide) write_sfc(sfc_wide, 1);
    if (sfc_check) {
        printf("sfc-check: %ld of %ld frames show the map; on them the pack had %ld squares and lacked %ld (windows' among them), had %ld figures and lacked %ld\n",
               sfc_map_frames, sfc_frames, sfc_sq[0], sfc_sq[1], sfc_fig[0], sfc_fig[1]);
        printf("sfc-check: %d different map squares and %d different figures are lacking\n", sfc_n_miss[0], sfc_n_miss[1]);
        for (int t = 0; t < 2; t++)
            for (int i = 0; i < sfc_n_miss[t]; i++)
                printf("sfc-check: %s %016llX lacking in %ld frames, first in frame %d%s\n", t ? "figure" : "square",
                       (unsigned long long)(sfc_miss[t][i].key ^ (!t && sfc_miss[t][i].scene ? 0x9E3779B97F4A7C15ULL : 0)),
                       sfc_miss[t][i].n, sfc_miss[t][i].first, !t && sfc_miss[t][i].scene ? " (overworld)" : "");
    }
    if ((sfc_png || sfc_wide) && !quiet) {
        int known, unknown, people, strangers;
        sfc_stats(&known, &unknown, &people, &strangers);
        printf("16-bit textures: the last picture has %d map squares from the pack and %d it does not know, %d figures and %d it does not know\n",
               known, unknown, people, strangers);
    }
    if (wide) { ws_wide_on += write_widescreen(wide, remaster != NULL); ws_wide++; }
    if (wide && !quiet) printf("widescreen: %ld of %ld pictures have the map in their bands\n", ws_wide_on, ws_wide);
    if (wide || ws_check) printf("widescreen: the map routine was run %lu times, %.1f a frame\n", widescreen_calls(), (double)widescreen_calls() / frames);
    if (ws_check)
        printf("ws-check: %ld of %ld frames show the map; %ld of %ld squares on them differ from the screen (%ld frames with a difference)\n",
               ws_map_frames, ws_frames, ws_bad, ws_cells, ws_bad_frames);
    if (ws_check) printf("ws-check: %ld frames at rest with a difference in the first column or row\n", widescreen_still_edge_bad);
    if (ws_check) printf("ws-check: %ld frames in which the map around the picture jumped while the picture did not\n", widescreen_jumps);
    if (ws_check) printf("ws-check: %ld frames in which a neighbouring camera square fits the screen clearly better\n", widescreen_better);
    if (ws_check) printf("ws-check: %ld pixels of the canvas's middle differ from the PPU's picture\n", ws_middle);
    if (ppu_dump) {  /* "DWPP" 1, ctrl, mask, scroll t (2), fine x, 3 bytes padding, palette[32], the four name tables as
                        the PPU addresses them (4 x 1024), CHR RAM (8192), OAM (256), the frame buffer (256 x 240) */
        FILE *f = fopen(ppu_dump, "wb");
        if (f) {
            uint8_t head[8] = { ppu.frame_ctrl, ppu.mask, (uint8_t)ppu.frame_t, (uint8_t)(ppu.frame_t >> 8), ppu.frame_x, 0, 0, 0 };
            fwrite("DWPP\1\0\0\0", 1, 8, f);
            fwrite(head, 1, 8, f);
            fwrite(ppu.palette, 1, 32, f);
            for (int t = 0; t < 4; t++) fwrite(ppu.vram + ppu.nt_offset[t], 1, 1024, f);
            fwrite(cart.chr_ram, 1, sizeof cart.chr_ram, f);
            fwrite(ppu.oam, 1, 256, f);
            fwrite(ppu.fb, 1, sizeof ppu.fb, f);
            fclose(f);
        }
    }
    if (map_dump) {  /* "DWMP" 1, ctrl, player x, y, the map's kind ($41), 4 bytes padding, palette[32], the squares
                        (256 x 256 x 5), CHR RAM (8192), the low RAM (2048), the battery RAM (8192) and OAM (256) */
        static uint8_t squares[256 * 256 * 5];
        int px, py;
        if (!widescreen_map_dump(squares, &px, &py)) fprintf(stderr, "map-dump: the map routine did not come back\n");
        FILE *f = fopen(map_dump, "wb");
        if (f) {
            uint8_t head[8] = { ppu.frame_ctrl, (uint8_t)px, (uint8_t)py, ram[0x41], 0, 0, 0, 0 };
            fwrite("DWMP\1\0\0\0", 1, 8, f);
            fwrite(head, 1, 8, f);
            fwrite(ppu.palette, 1, 32, f);
            fwrite(squares, 1, sizeof squares, f);
            fwrite(cart.chr_ram, 1, sizeof cart.chr_ram, f);
            fwrite(ram, 1, sizeof ram, f);
            fwrite(cart.wram, 1, sizeof cart.wram, f);
            fwrite(ppu.oam, 1, 256, f);
            fclose(f);
        }
    }
    if (hd_dump) {   /* "DWHD" 1, scroll t, fine x, ctrl, palette[32], line masks[240], fb, hd_bg, hd_spr (little-endian),
                        the number of palette snapshots, 3 bytes padding, the four snapshots (ppu.hd_palettes)
                        and the 8 KiB of CHR RAM the sources' tile numbers are slots of */
        FILE *f = fopen(hd_dump, "wb");
        if (f) {
            uint8_t scroll[4] = { (uint8_t)ppu.frame_t, (uint8_t)(ppu.frame_t >> 8), ppu.frame_x, ppu.frame_ctrl };
            fwrite("DWHD\1\0\0\0", 1, 8, f);
            fwrite(scroll, 1, 4, f);
            fwrite(ppu.palette, 1, 32, f);
            fwrite(ppu.line_emph, 1, 240, f);
            fwrite(ppu.fb, 1, sizeof ppu.fb, f);
            fwrite(ppu.hd_bg, 4, 256 * 240, f);
            fwrite(ppu.hd_spr, 4, 256 * 240, f);
            uint8_t count[4] = { ppu.hd_palette_count, 0, 0, 0 };
            fwrite(count, 1, 4, f);
            fwrite(ppu.hd_palettes, 1, sizeof ppu.hd_palettes, f);
            fwrite(cart.chr_ram, 1, sizeof cart.chr_ram, f);
            fclose(f);
        }
    }
    if (wav) { write_wav(wav, audio, audio_n, 48000, channels); free(audio); }
    if (apu_log_on) apu_log_print();
    if (title_quest) printf("title: the logo %s; the plaque was on the screen in %ld frames\n", title_available() ? "is built in" : "is NOT built in", title_frames);
#ifdef STK_AUDIT
    printf("stack audit: %llu returns through an address pushed by hand, %llu of them with a call or return in between\n",
           (unsigned long long)stk_hand_returns, (unsigned long long)stk_hand_returns_late);
#endif
    if (ips_toggle > 0 && n_ips) {
        ips_switches = (long)(patch_generation() - ips_gen0);
        printf("patches: switched %ld times while running; %ld frames ended with a switch still waiting for its moment\n", ips_switches, ips_waits);
        printf("returns by what pushed the address (data/JSR/BRK/interrupt/unknown): RTS %llu/%llu/%llu/%llu/%llu, RTI %llu/%llu/%llu/%llu/%llu\n",
               (unsigned long long)sys.ret_kind[0][0], (unsigned long long)sys.ret_kind[0][1], (unsigned long long)sys.ret_kind[0][2],
               (unsigned long long)sys.ret_kind[0][3], (unsigned long long)sys.ret_kind[0][4],
               (unsigned long long)sys.ret_kind[1][0], (unsigned long long)sys.ret_kind[1][1], (unsigned long long)sys.ret_kind[1][2],
               (unsigned long long)sys.ret_kind[1][3], (unsigned long long)sys.ret_kind[1][4]);
    }
    if (hash_file) fclose(hash_file);
    if (trace_file && trace_file != stdout) fclose(trace_file);
    if (cov) {
        FILE *f = fopen(cov, "wb");
        if (f) { fwrite(sys.cov, 1, cart.prg_size + 0x8000, f); fclose(f); }
    }
    if (sav) cart_save_battery(sav);
    if (state_save) {
        size_t len = nes_state_size();
        void *buf = malloc(len);
        FILE *f = nes_fopen(state_save, "wb");
        if (!f || nes_state_save(buf, len) != 0) { fprintf(stderr, "cannot save state %s\n", state_save); return 1; }
        fwrite(buf, 1, len, f);
        fclose(f);
        free(buf);
        if (!quiet) printf("state saved to %s (%zu bytes, cpu cycle %llu)\n", state_save, len, (unsigned long long)cpu.cycles);
    }
    if (sys.debug_log) fclose((FILE *)sys.debug_log);
    if (ram_dump) {
        FILE *f = fopen(ram_dump, "wb");
        if (f) { fwrite(ram, 1, sizeof ram, f); fwrite(cart.wram, 1, sizeof cart.wram, f); fclose(f); }
    }
    if (!quiet) {
        printf("ran %d frames, %llu instructions, cpu cycles %llu, ppu frame %llu\n", frames,
               (unsigned long long)sys.instructions, (unsigned long long)cpu.cycles, (unsigned long long)ppu.frame);
        printf("ppu ctrl %02X mask %02X | mmc1: ctrl %02X chr %02X %02X prg %02X | 16 KiB banks %02d / %02d | irqs %llu\n",
               ppu.ctrl, ppu.mask, mapper.mmc1_ctrl, mapper.mmc1_chr0, mapper.mmc1_chr1, mapper.mmc1_prg,
               mapper.prg_bank[0] >> 1, mapper.prg_bank[2] >> 1, (unsigned long long)sys.irq_count);
        if (sys.use_recomp) printf("recomp: %llu native routine entries, %llu interpreter fallback instructions\n",
                                   (unsigned long long)sys.native_blocks, (unsigned long long)sys.fallback_steps);
    }
    free(rgb);
    if (blargg) return blargg_result == 0 ? 0 : 1;
    return 0;
}
