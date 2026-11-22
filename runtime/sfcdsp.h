/* sfcdsp.h - the Super Famicom's sound chip (see sfcdsp.c) */
#ifndef SFCDSP_H
#define SFCDSP_H

#include <stdint.h>

int  sfcdsp_open(void);                 /* 1 = there is a chip now, reset */
void sfcdsp_close(void);
int  sfcdsp_ready(void);
uint8_t *sfcdsp_ram(void);              /* its 64 KiB: sample directory, samples, echo buffer */
void sfcdsp_reset(void);
void sfcdsp_write(uint8_t reg, uint8_t v);
void sfcdsp_sample(int16_t *lr);        /* runs the chip for one sample (1/32,000 s): left, right */

#endif
