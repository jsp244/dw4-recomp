/*
 * sfcdsp.c - the Super Famicom's sound chip for sfcmusic.c: LakeSnes's S-DSP alone
 * (third_party/lakesnes/snes/dsp.c, MIT license: see third_party/lakesnes/LICENSE.txt), with
 * the 64 KiB of memory it reads its samples from and keeps its echo in. No sound CPU runs: the
 * registers are written by sfcmusic.c. This file exists because LakeSnes's headers name their
 * functions as this port's own sound chip does (apu_reset, apu_write): they meet nowhere else.
 */
#include "sfcdsp.h"
#include "../third_party/lakesnes/snes/apu.h"
#include "../third_party/lakesnes/snes/dsp.h"
#include <stdlib.h>

static Apu *chip;                   /* only its memory and its DSP are used */

int sfcdsp_open(void) {
    sfcdsp_close();
    chip = calloc(1, sizeof *chip);
    if (!chip) return 0;
    chip->dsp = dsp_init(chip);
    if (!chip->dsp) { free(chip); chip = NULL; return 0; }
    dsp_reset(chip->dsp);
    return 1;
}

void sfcdsp_close(void) {
    if (!chip) return;
    if (chip->dsp) dsp_free(chip->dsp);
    free(chip);
    chip = NULL;
}

int sfcdsp_ready(void) { return chip != NULL; }
uint8_t *sfcdsp_ram(void) { return chip ? chip->ram : NULL; }
void sfcdsp_reset(void) { if (chip) dsp_reset(chip->dsp); }
void sfcdsp_write(uint8_t reg, uint8_t v) { if (chip) dsp_write(chip->dsp, reg, v); }

void sfcdsp_sample(int16_t *lr) {
    if (!chip) { lr[0] = lr[1] = 0; return; }
    Dsp *d = chip->dsp;
    dsp_cycle(d);
    unsigned i = (d->sampleOffset - 1) & 0x3ff;
    lr[0] = d->sampleBuffer[i * 2]; lr[1] = d->sampleBuffer[i * 2 + 1];
}
