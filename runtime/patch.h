/*
 * patch.h - IPS patches (ROM hacks) applied to the cartridge image in memory.
 *
 * A patch is applied after the dump has been loaded and recognised by its hash, so the file on
 * disk is never changed and the game can always be started without it. The translated code was
 * made from the unpatched ROM: every routine of it that has a patched byte in it is switched off
 * (recomp_patch) and runs through the interpreter instead, from the patched image, as does any
 * code a patch adds. Everything else stays native.
 */
#ifndef PATCH_H
#define PATCH_H
#include <stddef.h>
#include <stdint.h>

/* Forget all patches: call after the cartridge was (re)loaded, which starts from the dump again. */
void patch_reset(void);

/* Apply an IPS patch (the whole file in memory) to the loaded cartridge's PRG ROM. Returns the
   number of bytes it changed, or -1 with *err set (not an IPS file, or it reaches beyond the ROM:
   patches that make the ROM bigger are not supported). Offsets in the file count from the start
   of the .nes file, header included; changes to the 16 header bytes are ignored. */
long patch_apply_ips(const uint8_t *ips, size_t len, const char **err);
long patch_apply_file(const char *path, const char **err);

/* After the patches: switch off the translated routines they touch. Returns how many. */
int patch_commit(void);

int  patch_count(void);                 /* patches applied since the reset */
long patch_bytes(void);                 /* PRG bytes that differ from the dump */
int  patch_routines_off(void);          /* translated routines handed to the interpreter */

#endif
