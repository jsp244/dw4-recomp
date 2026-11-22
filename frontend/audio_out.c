/*
 * audio_out.c - queues the APU's samples to an SDL audio device.
 *
 * Latency is kept low and steady with dynamic rate control: the APU's output
 * sample rate is nudged by up to +-1.2% so the queue hovers around a target
 * instead of drifting to a large buffer (which the player feels as input lag,
 * since every sound effect arrives late) or running dry.
 *
 * The output is stereo: the NES's sound in both channels and, when the music plays on the art pack's
 * Super Famicom instruments (runtime/sfcmusic.c), those added, placed left and right as their
 * arrangement says.
 */
#include "audio_out.h"
#include <SDL.h>
#include "nes.h"
#include "sfcmusic.h"

static const struct { int target_ms, max_ms, prime_ms, device_samples; } presets[3] = {
    { 16, 60, 16, 256 },     /* low: ~25 ms including the device buffer */
    { 32, 100, 32, 512 },    /* normal */
    { 70, 180, 70, 1024 },   /* safe: for systems that stutter */
};

static SDL_AudioDeviceID dev;
static SDL_RWops *dump;         /* --audio-dump: a WAV of what is queued */
static Uint32 dump_bytes;
static int base_rate, cur_rate, preset;
static int rate_lo, rate_hi;   /* the range cur_rate moved over (statistics) */
static double trim;          /* learned rate correction (integral term) */
static double queued_avg = -1;   /* the queue's length in ms, smoothed; -1 = no reading yet */

static int open_device(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = base_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = (Uint16)presets[preset].device_samples;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return 0;
    audio_out_clear();
    SDL_PauseAudioDevice(dev, 0);
    return 1;
}

int audio_out_init(int sample_rate, int p) {
    base_rate = cur_rate = sample_rate;
    preset = (p < 0) ? 0 : (p > 2 ? 2 : p);
    return open_device();
}

void audio_out_set_preset(int p) {
    p = (p < 0) ? 0 : (p > 2 ? 2 : p);
    if (p == preset && dev) return;
    preset = p;
    if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }
    if (base_rate) open_device();
}

int audio_out_dump(const char *path) {
    dump = SDL_RWFromFile(path, "wb");
    if (!dump) return 0;
    Uint8 hdr[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0, 0,0,0,0, 0,0,0,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
    Uint32 rate = (Uint32)(base_rate ? base_rate : 48000), byte_rate = rate * 4;
    for (int i = 0; i < 4; i++) { hdr[24 + i] = (Uint8)(rate >> (8 * i)); hdr[28 + i] = (Uint8)(byte_rate >> (8 * i)); }
    SDL_RWwrite(dump, hdr, 1, sizeof hdr);
    dump_bytes = 0;
    return 1;
}

static void dump_close(void) {
    if (!dump) return;
    Uint32 riff = 36 + dump_bytes;
    Uint8 le[4];
    for (int i = 0; i < 4; i++) le[i] = (Uint8)(riff >> (8 * i));
    SDL_RWseek(dump, 4, RW_SEEK_SET); SDL_RWwrite(dump, le, 1, 4);
    for (int i = 0; i < 4; i++) le[i] = (Uint8)(dump_bytes >> (8 * i));
    SDL_RWseek(dump, 40, RW_SEEK_SET); SDL_RWwrite(dump, le, 1, 4);
    SDL_RWclose(dump);
    dump = NULL;
}

void audio_out_clear(void) {
    if (!dev) return;
    static int16_t silence[2 * 9600];
    SDL_ClearQueuedAudio(dev);
    queued_avg = -1;
    Uint32 n = (Uint32)(base_rate * presets[preset].prime_ms / 1000);
    if (n > 9600) n = 9600;
    SDL_QueueAudio(dev, silence, n * 2 * sizeof(int16_t));
}

int audio_out_queued_ms(void) {
    if (!dev || !base_rate) return 0;
    return (int)((SDL_GetQueuedAudioSize(dev) / (2 * sizeof(int16_t))) * 1000u / (unsigned)base_rate);
}

int audio_out_rate(void) { return cur_rate; }

void audio_out_rate_range(int *lo, int *hi) {
    *lo = rate_lo ? rate_lo : cur_rate; *hi = rate_hi ? rate_hi : cur_rate;
    rate_lo = rate_hi = 0;
}

void audio_out_pump(int muted, int volume) {
    static int16_t buf[4096], st[2 * 4096];
    if (dev && !muted) {
        int target = presets[preset].target_ms;
        /* The queue's length, read once a frame, jumps by a device block (about 10 ms) according
           to when the device last took one. Steering by the raw reading moved the rate over some
           350 Hz within every second: the pitch wavered by +-6 cents many times a second, which
           the NES's pure tones give away as a rough, unsteady sound. So the reading is smoothed
           over about half a second, and the steering is gentle and limited to half a percent:
           the pitch now moves by a fraction of a cent, slowly. */
        double q = audio_out_queued_ms();
        if (queued_avg < 0) queued_avg = q;
        else queued_avg += 0.03 * (q - queued_avg);
        double err = (target - queued_avg) / target;   /* +1 empty .. -1 at twice the target */
        if (err > 1.0) err = 1.0;
        if (err < -1.0) err = -1.0;
        /* a slow integral term learns a steady clock difference (a 120.6 Hz display paces the
           game 0.35 % fast), so the queue settles at the target instead of above it */
        trim += 0.00001 * err;
        if (trim > 0.01) trim = 0.01;
        if (trim < -0.01) trim = -0.01;
        double adj = 0.004 * err + trim;
        if (adj > 0.005) adj = 0.005;
        if (adj < -0.005) adj = -0.005;
        cur_rate = (int)(base_rate * (1.0 + adj) + 0.5);
    } else {
        cur_rate = base_rate;
    }
    apu.sample_rate = (uint32_t)cur_rate;
    if (!rate_lo || cur_rate < rate_lo) rate_lo = cur_rate;
    if (cur_rate > rate_hi) rate_hi = cur_rate;

    sfcmusic_frame();                /* the frame just run is one that is heard: its notes */
    uint32_t n = apu_samples_available();
    while (n > 0) {
        uint32_t got = apu_read_samples(buf, n < 4096 ? n : 4096);
        if (!got) break;
        n -= got;
        for (uint32_t i = 0; i < got; i++) st[2 * i] = st[2 * i + 1] = buf[i];
        if (muted) continue;
        sfcmusic_mix(st, got, (uint32_t)cur_rate);
        if (volume != 100) for (uint32_t i = 0; i < 2 * got; i++) st[i] = (int16_t)(st[i] * volume / 100);
        if (dump) { SDL_RWwrite(dump, st, 1, got * 2 * sizeof(int16_t)); dump_bytes += got * 2 * (Uint32)sizeof(int16_t); }
        if (!dev) continue;
        if (SDL_GetQueuedAudioSize(dev) > (Uint32)(base_rate * presets[preset].max_ms / 1000) * 2 * sizeof(int16_t)) continue;
        SDL_QueueAudio(dev, st, got * 2 * sizeof(int16_t));
    }
}

void audio_out_shutdown(void) {
    dump_close();
    if (dev) SDL_CloseAudioDevice(dev);
    dev = 0;
}
