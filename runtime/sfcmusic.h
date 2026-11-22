/* sfcmusic.h - the game's music on Super Famicom instruments (see sfcmusic.c) */
#ifndef SFCMUSIC_H
#define SFCMUSIC_H

#include <stddef.h>
#include <stdint.h>

int  sfcmusic_init(const char *pack_path);   /* reads the music at the end of the art pack; 1 = there is some */
int  sfcmusic_ready(void);
void sfcmusic_forget(void);
void sfcmusic_enable(int on);                /* the game's own music channels fall silent, the instruments play */
int  sfcmusic_enabled(void);
void sfcmusic_frame(void);                   /* once after every frame that is heard */
void sfcmusic_speculate(int on);             /* around frames that are run ahead and taken back */
/* adds the music to `frames` interleaved stereo samples of rate `rate` */
void sfcmusic_mix(int16_t *stereo, uint32_t frames, uint32_t rate);
/* the sound chip's own output, 32,000 Hz stereo, with nothing added (tests) */
void sfcmusic_render(int16_t *stereo, uint32_t frames);

#endif
