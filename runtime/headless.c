/*
 * headless.c - command-line harness: runs the machine without a window,
 * for test ROMs, scripted input, screenshots, traces, coverage files and
 * interpreter-vs-recompiled cosimulation.
 */
#include "nes.h"
#include "png.h"
#include "palette.h"
#include "remaster.h"
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
        "  --input FILE        input script: lines of 'frame BTN+BTN'\n"
        "  --fuzz SEED         random controller input (the same for the same seed), from --fuzz-start N (default 0)\n"
        "  --png FILE          write the last frame as PNG\n"
        "  --png-every N       write FILE with the frame number appended every N frames\n"
        "  --remaster FILE     write the last frame with the remastered graphics (also every N frames)\n"
        "  --remaster-pack F   the pack of hand-made tiles for --remaster\n"
        "  --hd-dump FILE      write the last frame's pixel sources (tools/remaster): palette, line\n"
        "                      masks, pixels and each layer's source tile per pixel\n"
        "  --wav FILE          write audio to a 48 kHz WAV\n"
        "  --trace FILE        write a per-instruction trace (interpreter)\n"
        "  --nestest N         nestest mode: start at $C000 and run N instructions\n"
        "  --blargg            poll the blargg test status at $6000 and exit when done\n"
        "  --coverage FILE     write executed-instruction coverage for the recompiler\n"
        "  --recomp            run recompiled code with interpreter fallback\n"
        "  --frame-hash FILE   write a per-frame state hash (for cosimulation diffs)\n"
        "  --sav FILE          load/save battery RAM from FILE\n"
        "  --ram-dump FILE     write RAM + PRG RAM at the end (with --ram-dump-every N: FILE.NNNNN.bin every N frames)\n"
        "  --quiet             no per-run summary\n");
}

int main(int argc, char **argv) {
    const char *rom = NULL, *png = NULL, *wav = NULL, *trace = NULL, *cov = NULL, *hash = NULL, *sav = NULL, *input = NULL, *debug_log = NULL, *ram_dump = NULL;
    const char *state_save = NULL, *state_load = NULL, *remaster = NULL, *remaster_pack = NULL, *hd_dump = NULL;
    int fuzz_start = 0;
    int frames = 600, png_every = 0, ram_dump_every = 0, nestest = 0, blargg = 0, recomp = 0, quiet = 0, trace_start = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) parse_keys(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input = argv[++i];
        else if (!strcmp(argv[i], "--fuzz") && i + 1 < argc) fuzz_state = (uint32_t)strtoul(argv[++i], NULL, 0) * 2654435761u + 1;
        else if (!strcmp(argv[i], "--fuzz-start") && i + 1 < argc) fuzz_start = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) png_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--remaster") && i + 1 < argc) remaster = argv[++i];
        else if (!strcmp(argv[i], "--remaster-pack") && i + 1 < argc) remaster_pack = argv[++i];
        else if (!strcmp(argv[i], "--hd-dump") && i + 1 < argc) hd_dump = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
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
        else { usage(); return 2; }
    }
    if (!rom) { usage(); return 2; }

    apu_set_sample_rate(48000);
    if (nes_load(rom) != 0) return 1;
    if (sav) cart_load_battery(sav);
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
    int blargg_result = -1;
    int reset_hold = 0;

    for (int frame = 0; frame < frames; frame++) {
        if (trace_file && trace_start && frame == trace_start) sys.trace_hook = trace_hook;
        while (ev < n_events && events[ev].frame <= frame) { nes_set_pad(0, events[ev].buttons); ev++; }
        if (fuzz_state && frame >= fuzz_start) nes_set_pad(0, fuzz_pad());
        nes_run_frame();
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
        if (png && png_every && (frame % png_every) == 0) {
            char name[1100];
            snprintf(name, sizeof name, "%s.%05d.png", png, frame);
            fb_to_rgb(rgb);
            png_write_rgb(name, 256, 240, rgb);
        }
        if (remaster && png_every && (frame % png_every) == 0) {
            char name[1100];
            snprintf(name, sizeof name, "%s.%05d.png", remaster, frame);
            write_remastered(name, rgb);
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
