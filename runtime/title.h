/*
 * title.h - the title screen under the game's other name.
 *
 * Outside North America this game is Dragon Quest IV, and its title screen shows that logo where
 * the North American one shows a stone plaque being engraved with DRAGON WARRIOR IV. The scene
 * around it (the castle over the sea) is the same. With the option, the picture the PPU made is
 * changed after the fact: where one of the two stands, the sky and the other. The game itself
 * runs exactly as before. It works both ways, so it also holds for a ROM hack that brings the
 * other title along (a translation with the Japanese title sequence).
 *
 * The two pictures are not part of this repository: tools/title/extract_title.py takes them from
 * dumps of the two cartridges into generated/title_dq.h and title_dw.h, and a build made without
 * them has no such option (title_available() is 0).
 */
#ifndef TITLE_H
#define TITLE_H

enum { TITLE_NONE = 0, TITLE_PLAQUE, TITLE_LOGO };

int  title_available(void);     /* both pictures were built in */

/* What the frame just rendered shows: TITLE_PLAQUE (the plaque's frame, whatever is engraved in
   it), TITLE_LOGO (the Japanese logo), or TITLE_NONE. */
int  title_scene(void);

/* Draw the other one in its place, in ppu.fb: want is TITLE_LOGO or TITLE_PLAQUE. title_undo puts
   the PPU's own pixels back (the picture is drawn again from them while the game is paused). */
void title_draw(int want);
void title_undo(void);

#endif
