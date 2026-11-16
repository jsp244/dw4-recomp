/*
 * patch.h - IPS patches (ROM hacks) applied to the cartridge image in memory, switchable while
 * the game runs.
 *
 * The file on disk is never changed. The image the machine runs is always "the dump plus the
 * patches that are on", built fresh from a copy of the dump each time the set changes, so
 * patches can be switched on and off in any order. The translated code was made from the dump:
 * every routine of it with a patched byte in it is switched off (recomp_patch) and runs through
 * the interpreter from the patched image, as does any code a patch adds.
 *
 * Switching while the game runs takes two steps. patch_prepare builds the new image on the side.
 * patch_switch puts it in place, but only at a moment when that cannot pull code from under the
 * CPU (see patch.c: the code it is in and every call it has to return from must be the same in
 * both images). With patch_live(1) the machine tries by itself while it runs a frame, after every
 * call and return (a game idles in a loop that a patch may well have moved, so the moment between
 * two frames can be the wrong one every time); the caller may also try after each frame. Data a
 * patch changes (text, graphics, tables) is simply what the game reads the next time it reads it.
 *
 * A patch may also move the game to another cartridge board by changing the mapper number in the
 * header (MMC1 to MMC3 is the one the runtime can follow). The switch then also turns the mapper
 * into the other kind with the same memory map, and waits for a moment at which the map in force
 * is one both boards can show.
 */
#ifndef PATCH_H
#define PATCH_H
#include <stddef.h>
#include <stdint.h>

/* The cartridge was (re)loaded: forget all patches and take a copy of the dump. */
void patch_reset(void);

/* What a patch file is, told from what it changes (so other versions of a hack are known too).
   A whole-game hack rewrites text, code and data all over the ROM (or moves it to another board)
   and goes first; the three small ones are well-known fixes that such a hack often contains. */
enum { PATCH_OTHER = 0, PATCH_UNCENSOR, PATCH_MANUAL, PATCH_SEA, PATCH_WHOLE };
typedef struct {
    int  kind;
    long bytes;         /* ROM bytes it writes */
    int  board;         /* 1 when it changes the cartridge board */
} patch_info_t;
int patch_inspect(const char *path, patch_info_t *info);        /* 0: not a readable IPS file */

/* How many ROM bytes both files write (0: they cannot affect each other, in either order). */
long patch_overlap(const char *a, const char *b);

/* A patch from a file (path) or from memory (data, len: the hacks built into the program); name
   is what an error message calls it. The functions above and patch_prepare, for either kind: */
typedef struct {
    const char *name;
    const char *path;
    const uint8_t *data;
    size_t len;
} patch_src_t;
int  patch_inspect_src(const patch_src_t *src, patch_info_t *info);
long patch_overlap_src(const patch_src_t *a, const patch_src_t *b);
int  patch_prepare_list(const patch_src_t *src, int n, char *errors, size_t errors_len);

/* Build "the dump plus these IPS files, applied in this order" on the side. Returns how many of
   the files were applied; a file that is not an IPS patch or reaches beyond the ROM (patches
   that make the ROM bigger are not supported) is left out and named in errors, one line each.
   Offsets in an IPS file count from the start of the .nes file, header included; of the 16
   header bytes only the mapper number is taken (a board the runtime does not have: left out). */
int patch_prepare(const char *const *paths, int n, char *errors, size_t errors_len);

/* 1 while a prepared image waits that differs from the one running. */
int patch_pending(void);

/* Put the prepared image in place if that is safe right now (force: do it regardless, for a
   machine that is about to be powered on). Returns 1 when it was done (or nothing was pending),
   0 when the CPU is inside bytes that would change: try again later. */
int patch_switch(int force);
int patch_try(int after);               /* the same from inside a frame: after a JSR (1), an RTS (2), an RTI (3) */

/* Let nes_run_frame make a pending switch by itself. Off for frames that are run and then taken
   back (run-ahead): a switch made in one of those would not be undone with it. */
void patch_live(int on);
extern int patch_live_wait;             /* (for the frame loop: a switch is pending and may be made) */
extern long patch_refused[4];           /* statistics: switches put off because of the memory map, the program counter, the stack */
extern uint16_t patch_refused_at;       /* the address that did it last */

/* Counts the switches made. States saved before a switch belong to the other image: whoever keeps
   some (rewind) watches this. */
unsigned patch_generation(void);

/* Identifies the image running: 0 for the dump, else a hash of what differs. A saved state can
   only be loaded into the image it was saved from. */
uint32_t patch_signature(void);

int  patch_count(void);                 /* patches in the image running */
long patch_bytes(void);                 /* PRG bytes of it that differ from the dump */
int  patch_routines_off(void);          /* translated routines handed to the interpreter */
int  patch_board_changed(void);         /* the image running is on another board than the dump */

#endif
