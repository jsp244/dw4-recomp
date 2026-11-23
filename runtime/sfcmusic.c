/*
 * sfcmusic.c - the game's music on Super Famicom instruments.
 *
 * There is no Super Famicom Dragon Quest IV to take a soundtrack from, so the game plays its own
 * notes, and they sound on Dragon Quest III's instruments: the art pack holds the samples (cut
 * from the player's own cartridge by tools/sfc, like the art) and, for every tune, which
 * instrument takes which of the NES's three melodic channels. The samples play through the Super
 * Famicom's sound chip, the S-DSP, as LakeSnes emulates it (third_party/lakesnes, MIT license):
 * its sample decoding, its envelopes, its echo.
 *
 * Nothing of the game is changed. Once a frame this looks at what the NES's sound chip was told:
 *   - a channel's fourth register written while it sounds: a note is struck (the game's sound
 *     driver does so at every note and at nothing else);
 *   - its period: the pitch, followed frame by frame;
 *   - its volume: the game shapes every note with its own envelope, and the instrument follows
 *     as much of it as the arrangement says; the note ends when the channel falls silent.
 * A part may take only the notes below a pitch, or from it up: a bass line that climbs out of the
 * double basses' reach is handed to the next section, as a score would. (The chip plays a sample at
 * most two octaves above its own note; what would still lie above that sounds an octave lower.)
 * The game's fades are followed too: its sound driver steps a master volume (the low half of $B0)
 * while bit 6 of $BB is set, and the instruments' output is scaled by the same steps.
 * The game says which tune it plays (its sound driver keeps twice the number at $BF) and when a
 * sound effect has taken the second pulse channel or the noise ($0628, $BB): effects stay the
 * NES's own. The channels the instruments play are left out of the NES's mix (apu_mute_mask);
 * the noise channel is the NES's always.
 *
 * The music in a pack (after everything else, so that it is found from the file's end):
 *   "DW4M", version (16 bits), instruments, arrangements, gain (percent of the chip's output),
 *   echo delay, eight FIR taps, master volume; then
 *   an instrument: length of its BRR bytes, loop start (bytes), pitch at $1000 (Hz, float),
 *     the bytes;
 *   an arrangement: tune (255 = any tune without one of its own), flags (1 = the tune is left to
 *     the NES: a sound effect made as a tune), parts, echo feedback, echo volume, three unused;
 *   a part (12 bytes): channel (0 pulse 1, 1 pulse 2, 2 triangle), instrument, ADSR 1, ADSR 2,
 *     volume left and right (at the channel's loudest), octaves up, how much of the game's volume
 *     envelope it follows (percent), the volume that counts as loudest, flags (1 = a plucked
 *     string: it rings on when the channel falls silent; 2 = only the notes below the split, 4 = only
 *     those from it up), cents of detuning, the split (a MIDI note number);
 *   then the blob's length (32 bits) and "DW4M" again.
 */
#include "sfcmusic.h"
#include "nes.h"
#include "sfcdsp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INST 32
#define MAX_ARR 64
#define MAX_PARTS 8
#define DIR_AT 0x0200
#define SAMPLES_AT 0x0400
#define ECHO_AT 0x8000

typedef struct { uint8_t chan, inst, a1, a2; int8_t vl, vr, octave; uint8_t follow, top, flags; int8_t detune; uint8_t split; } part_t;
typedef struct { uint8_t tune, flags, nparts; int8_t efb, evol; part_t part[MAX_PARTS]; } arr_t;

static float inst_f0[MAX_INST];
static int ninst, narr;
static arr_t arr[MAX_ARR];
static double gain = 1.0;
static int on, speculating;

/* the player */
static const arr_t *now;            /* the arrangement of the tune that plays */
static int now_tune = -1;
static struct { uint8_t sounding; uint16_t pitch; int8_t vl, vr; } voice[MAX_PARTS];
static uint8_t koff;
/* the game's fades: what its master volume is of what it was before the fade, and where the output's scale is */
static double fade_to = 1.0, fade_now = 1.0;
static int fade_ref = 15;
#define FADE_STEP (1.0 / 512)               /* per sample of the chip: a step of the game's sixteen in 4 ms */
/* 32,000 Hz to the output's rate */
static double pos;
static int16_t hist[4][2];

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int sfcmusic_ready(void) { return sfcdsp_ready() && narr > 0; }
int sfcmusic_enabled(void) { return on && sfcmusic_ready(); }

void sfcmusic_forget(void) {
    sfcdsp_close();
    ninst = narr = 0; now = NULL; now_tune = -1; on = 0;
    apu_mute_output = false; apu_mute_mask = 0; apu_mask_hook = NULL;
}

static void chip_start(const uint8_t *fir, int edl, int mvol) {
    sfcdsp_reset();
    sfcdsp_write(0x6C, 0x20);                 /* no echo writes while its buffer's place is set */
    sfcdsp_write(0x5D, DIR_AT >> 8);
    sfcdsp_write(0x6D, ECHO_AT >> 8);
    sfcdsp_write(0x7D, (uint8_t)edl);
    for (int i = 0; i < 8; i++) sfcdsp_write((uint8_t)(i * 16 + 15), fir[i]);
    sfcdsp_write(0x0C, (uint8_t)mvol); sfcdsp_write(0x1C, (uint8_t)mvol);
    sfcdsp_write(0x2C, 0); sfcdsp_write(0x3C, 0); sfcdsp_write(0x0D, 0);
    sfcdsp_write(0x2D, 0); sfcdsp_write(0x3D, 0); sfcdsp_write(0x4D, edl ? 0xFF : 0);
    sfcdsp_write(0x5C, 0); sfcdsp_write(0x4C, 0);
    for (int i = 0; i < 40000; i++) { int16_t lr[2]; sfcdsp_sample(lr); }      /* until the buffer's old place has run out */
    sfcdsp_write(0x6C, 0x00);
    memset(voice, 0, sizeof voice);
    koff = 0; pos = 0; memset(hist, 0, sizeof hist);
    now = NULL; now_tune = -1;
    fade_to = fade_now = 1.0; fade_ref = 15;
}

static int take(const uint8_t *d, size_t len) {
    if (len < 24 || memcmp(d, "DW4M", 4) || le16(d + 4) != 1) return 0;
    int ni = le16(d + 6), na = le16(d + 8);
    if (ni > MAX_INST || na > MAX_ARR || !na) return 0;
    if (!sfcdsp_open()) return 0;
    uint8_t *mem = sfcdsp_ram();
    gain = le16(d + 10) / 100.0;
    size_t o = 24;
    unsigned at = SAMPLES_AT;
    for (int i = 0; i < ni; i++) {
        if (o + 8 > len) goto bad;
        unsigned n = le16(d + o), loop = le16(d + o + 2);
        float f0;
        memcpy(&f0, d + o + 4, 4);
        o += 8;
        if (o + n > len || at + n > ECHO_AT || loop >= (n ? n : 1)) goto bad;
        memcpy(mem + at, d + o, n);
        o += n;
        mem[DIR_AT + i * 4] = (uint8_t)at; mem[DIR_AT + i * 4 + 1] = (uint8_t)(at >> 8);
        mem[DIR_AT + i * 4 + 2] = (uint8_t)(at + loop); mem[DIR_AT + i * 4 + 3] = (uint8_t)((at + loop) >> 8);
        inst_f0[i] = f0;
        at += n;
    }
    ninst = ni;
    for (int k = 0; k < na; k++) {
        if (o + 8 > len) goto bad;
        arr_t *a = &arr[k];
        a->tune = d[o]; a->flags = d[o + 1]; a->nparts = d[o + 2]; a->efb = (int8_t)d[o + 3]; a->evol = (int8_t)d[o + 4];
        o += 8;
        if (a->nparts > MAX_PARTS || o + (size_t)a->nparts * 12 > len) goto bad;
        for (int p = 0; p < a->nparts; p++, o += 12) {
            part_t *q = &a->part[p];
            q->chan = d[o]; q->inst = d[o + 1]; q->a1 = d[o + 2]; q->a2 = d[o + 3];
            q->vl = (int8_t)d[o + 4]; q->vr = (int8_t)d[o + 5]; q->octave = (int8_t)d[o + 6];
            q->follow = d[o + 7]; q->top = d[o + 8] ? d[o + 8] : 12; q->flags = d[o + 9]; q->detune = (int8_t)d[o + 10];
            q->split = d[o + 11];
            if (q->chan > 2 || q->inst >= ninst || inst_f0[q->inst] <= 0) goto bad;
        }
    }
    narr = na;
    chip_start(d + 13, d[12], d[21]);
    return 1;
bad:
    sfcmusic_forget();
    return 0;
}

int sfcmusic_init(const char *pack_path) {
    sfcmusic_forget();
    FILE *f = fopen(pack_path, "rb");
    if (!f) return 0;
    uint8_t tail[8];
    int ok = 0;
    if (!fseek(f, -8, SEEK_END) && fread(tail, 1, 8, f) == 8 && !memcmp(tail + 4, "DW4M", 4)) {
        uint32_t n = le32(tail);
        uint8_t *d = n && n < (1u << 20) ? malloc(n) : NULL;
        if (d && !fseek(f, -8 - (long)n, SEEK_END) && fread(d, 1, n, f) == n) ok = take(d, n);
        free(d);
    }
    fclose(f);
    if (ok) fprintf(stderr, "16-bit music: %d instruments, %d arrangements from %s\n", ninst, narr, pack_path);
    return ok;
}

/* which of the NES's channels the instruments stand in for right now */
static uint8_t mask_now(void) {
    if (!on || !now || (now->flags & 1)) return 0;
    uint8_t m = 0;                                      /* bit 0 pulse 1, 1 pulse 2, 2 the triangle */
    for (int p = 0; p < now->nparts; p++) m |= (uint8_t)(1 << now->part[p].chan);
    if (ram[0x0628] && !(ram[0xBB] & 0x20)) m &= (uint8_t)~2;       /* a sound effect has the second pulse */
    return m;
}

static const arr_t *arrangement_of(int tune) {
    const arr_t *any = NULL;
    for (int k = 0; k < narr; k++) {
        if (arr[k].tune == tune) return &arr[k];
        if (arr[k].tune == 255) any = &arr[k];
    }
    return any;
}

static void all_off(void) {
    koff = 0xFF;
    sfcdsp_write(0x5C, koff);
    for (int v = 0; v < MAX_PARTS; v++) voice[v].sounding = 0;
}

void sfcmusic_enable(int want) {
    want = want && sfcmusic_ready();
    if (want == on) return;
    on = want;
    if (sfcdsp_ready()) all_off();
    now = NULL; now_tune = -1;
    apu_mute_output = on;
    apu_mask_hook = on ? mask_now : NULL;
    apu_mute_mask = 0;
    memset(apu_struck, 0, sizeof apu_struck);
}

void sfcmusic_speculate(int s) {
    speculating = s;
    apu_struck_hold = s != 0;
    if (!s && on) apu_mute_mask = mask_now();
}

void sfcmusic_frame(void) {
    if (!on || speculating) return;
    int tune = ram[0xBF] >> 1;
    if (tune != now_tune) {
        now_tune = tune;
        now = arrangement_of(tune);
        all_off();
        if (now && !(now->flags & 1)) {
            sfcdsp_write(0x0D, (uint8_t)now->efb);
            sfcdsp_write(0x2C, (uint8_t)now->evol); sfcdsp_write(0x3C, (uint8_t)now->evol);
        }
    }
    apu_mute_mask = mask_now();
    if (!now || (now->flags & 1)) { memset(apu_struck, 0, sizeof apu_struck); return; }
    int mv = ram[0xB0] & 15;                            /* the sound driver's master volume */
    if (!mv) fade_to = 0.0;                             /* (a tune faded out plays on, silent) */
    else if (ram[0xBB] & 0x40) fade_to = mv >= fade_ref ? 1.0 : (double)mv / fade_ref;
    else { fade_ref = mv; fade_to = 1.0; }              /* no fade: whatever the tune set is its full volume */
    int sfx_p2 = ram[0x0628] && !(ram[0xBB] & 0x20);
    uint8_t kon = 0, newoff = koff;
    for (int v = 0; v < now->nparts; v++) {
        const part_t *q = &now->part[v];
        int per, vol, struck;
        double freq;
        if (q->chan < 2) {
            const apu_pulse_t *p = &apu.pulse[q->chan];
            per = p->timer_period;
            vol = p->length > 0 && per >= 8 ? (p->const_volume ? p->volume : p->env_decay) : 0;
            struck = apu_struck[q->chan];
            if (q->chan == 1 && sfx_p2) vol = 0, struck = 0;
            freq = 1789772.7 / (16.0 * (per + 1));
        } else {
            per = apu.tri.timer_period;
            vol = apu.tri.length > 0 && apu.tri.linear > 0 && per >= 2 ? 12 : 0;
            struck = apu_struck[2];
            freq = 1789772.7 / (32.0 * (per + 1));
        }
        if (q->flags & 6) {                             /* the part has one side of the split only */
            long m = lround(69.0 + 12.0 * log2(freq / 440.0));
            if ((q->flags & 2) ? m >= q->split : m < q->split) vol = 0, struck = 0;
        }
        double pd = 4096.0 * freq * pow(2.0, q->octave + q->detune / 1200.0) / inst_f0[q->inst];
        while (pd > 16383.0) pd *= 0.5;                 /* above the chip's reach: the octave below */
        uint16_t pitch = (uint16_t)lround(pd);
        double follow = q->follow / 100.0, lev = (double)vol / q->top;
        double g = (1.0 - follow) + follow * (lev > 1.0 ? 1.0 : lev);
        int8_t vl = (int8_t)lround(q->vl * g), vr = (int8_t)lround(q->vr * g);
        uint8_t r = (uint8_t)(v * 16);
        if (vol && (struck || !voice[v].sounding)) {
            sfcdsp_write(r + 4, q->inst);
            sfcdsp_write(r + 2, (uint8_t)pitch); sfcdsp_write(r + 3, (uint8_t)(pitch >> 8));
            sfcdsp_write(r + 0, (uint8_t)vl); sfcdsp_write(r + 1, (uint8_t)vr);
            sfcdsp_write(r + 5, q->a1); sfcdsp_write(r + 6, q->a2);
            kon |= (uint8_t)(1 << v); newoff &= (uint8_t)~(1 << v);
            voice[v].sounding = 1; voice[v].pitch = pitch; voice[v].vl = vl; voice[v].vr = vr;
        } else if (voice[v].sounding && !vol) {
            if (!(q->flags & 1)) newoff |= (uint8_t)(1 << v);
            voice[v].sounding = 0;
        } else if (voice[v].sounding) {
            if (pitch != voice[v].pitch) {
                sfcdsp_write(r + 2, (uint8_t)pitch); sfcdsp_write(r + 3, (uint8_t)(pitch >> 8));
                voice[v].pitch = pitch;
            }
            if (vl != voice[v].vl || vr != voice[v].vr) {
                sfcdsp_write(r + 0, (uint8_t)vl); sfcdsp_write(r + 1, (uint8_t)vr);
                voice[v].vl = vl; voice[v].vr = vr;
            }
        }
    }
    if (newoff != koff) { koff = newoff; sfcdsp_write(0x5C, koff); }
    if (kon) sfcdsp_write(0x4C, kon);
    memset(apu_struck, 0, sizeof apu_struck);
}

/* a sample of the chip, scaled by where the game's fade is */
static void chip_sample(int16_t *lr) {
    sfcdsp_sample(lr);
    if (fade_now != fade_to) {
        double d = fade_to - fade_now;
        fade_now += d > FADE_STEP ? FADE_STEP : d < -FADE_STEP ? -FADE_STEP : d;
    }
    if (fade_now < 1.0) {
        lr[0] = (int16_t)lround(lr[0] * fade_now);
        lr[1] = (int16_t)lround(lr[1] * fade_now);
    }
}

void sfcmusic_render(int16_t *stereo, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) chip_sample(stereo + i * 2);
}

void sfcmusic_mix(int16_t *stereo, uint32_t frames, uint32_t rate) {
    if (!on || !sfcdsp_ready() || !rate) return;
    double step = 32000.0 / rate;
    for (uint32_t i = 0; i < frames; i++) {
        pos += step;
        while (pos >= 1.0) {
            memmove(hist[0], hist[1], 3 * sizeof hist[0]);
            chip_sample(hist[3]);
            pos -= 1.0;
        }
        for (int c = 0; c < 2; c++) {
            /* a cubic through the last four of the chip's samples (Catmull-Rom), between the middle two */
            double a = hist[0][c], b = hist[1][c], cc = hist[2][c], d = hist[3][c], t = pos;
            double y = b + 0.5 * t * (cc - a + t * (2.0 * a - 5.0 * b + 4.0 * cc - d + t * (3.0 * (b - cc) + d - a)));
            double s = stereo[i * 2 + c] + y * gain;
            stereo[i * 2 + c] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : lround(s));
        }
    }
}
