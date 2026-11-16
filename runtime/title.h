/*
 * title.h - the title screen under the game's other name.
 *
 * Outside North America this game is Dragon Quest IV, and its title screen shows that logo where
 * the North American one shows a stone plaque being engraved with DRAGON WARRIOR IV. The scene
 * around it (the castle over the sea) is the same. With the option on, the picture the PPU made
 * is changed after the fact: where the plaque stands, the sky and the other logo. The game itself
 * runs exactly as before.
 *
 * The logo is not part of this repository: tools/title/extract_title.py takes it from a dump of
 * the Japanese cartridge into generated/title_dq.h, and a build made without that file has no
 * such option (title_available() is 0).
 */
#ifndef TITLE_H
#define TITLE_H

int  title_available(void);     /* the logo was built in */
int  title_plaque(void);        /* the plaque scene is on the screen now (the frame just rendered) */
void title_draw(void);          /* the logo in the plaque's place, in ppu.fb (call when title_plaque()) */

#endif
