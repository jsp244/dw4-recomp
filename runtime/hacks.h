/*
 * hacks.h - the ROM hacks that come with the program.
 *
 * tools/hacks/embed_hacks.py turns the patch files named in tools/hacks/hacks.txt into
 * generated/hacks.h; a build made with that file has them built in, each with its name, version,
 * author, year and page, and the Patches tab lists them ready to switch on. A build without it
 * has none (hacks_count() is 0). The patches are their authors' work; they are applied like any
 * other IPS file (patch.h).
 */
#ifndef HACKS_H
#define HACKS_H

#define HACKS_MAX 16

typedef struct {
    const char *id;                 /* its name in the settings file */
    const char *name, *version, *author, *year, *page;
    const char *parent;             /* for an add-on: the id of the hack it goes with (applied after it), else "" */
    int kind;                       /* PATCH_WHOLE, PATCH_UNCENSOR, PATCH_MANUAL, PATCH_SEA; PATCH_OTHER for an add-on */
    const unsigned char *data;      /* the IPS file */
    unsigned len;
} hack_t;

int hacks_count(void);
const hack_t *hacks_get(int i);

#endif
