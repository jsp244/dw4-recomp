Dragon Quest IV (Dragon Warrior IV) - native PC port
====================================================

This program contains no game data. To play you need your own Dragon Warrior IV
(USA) cartridge dump as a .nes file, made from a cartridge you own. The program
checks the file's hash and refuses anything that is not Dragon Warrior IV.

Getting started
---------------
1. Run dw4.exe (Windows) or dw4 (Linux). The Steam Deck and Android editions have their
   own notes: STEAMDECK.txt in the Deck package, docs/ANDROID.md in the source.
2. On the ROM tab choose "Choose ROM file..." and pick your .nes file, or drop
   the file onto the window, or start the program with the file as an argument.
3. Go to Play > Start game. The chosen file is remembered in dw4.ini.

Menu (Esc in game, Start + Select together, or LT + RT on Android and the Steam Deck)
-------------------------------------------------------------------------------------
Play          Start / resume, reset, save state, load state, the save state slot
              (1-9, with what the slot holds), end game, quit
Save states   All nine slots: the chapter, the level of the chapter's main
              character and when each was saved; choose one to load it or to
              save into it
Graphics      Display mode (windowed / fullscreen / fullscreen exclusive),
              resolution (auto, the sizes your display offers, and always
              1920x1080 and 3840x2160: the window size, the display mode in
              exclusive fullscreen, or the size the picture is rendered at in
              fullscreen), window size 1x-6x (shown while the resolution is Auto),
              scaling (integer: exact pixel multiples, may leave borders /
              fit: fills the screen, keeps the shape / stretch: fills, distorts),
              aspect ratio (square pixels 8:7 / 4:3 CRT / 16:9 or 16:10 widescreen:
              more of the map beside the picture, below),
              textures (Original, Pixel Remaster, or SNES Remaster with a pack of
              your own: both below),
              title screen (Dragon Quest IV or Dragon Warrior IV: below),
              filter (nearest / linear / sharp),
              shader (off / scanlines / CRT / CRT curved / glow) and its strength,
              overscan crop,
              sprite limit (8 per line as on the console, or unlimited: no flicker
              when many sprites share a line),
              low-latency sync (phase-locks to the display), run-ahead (0-3 frames).
              The line under the rows says what the settings produce (for example
              "Picture 1792x1344 in the 2560x1600 display mode") and why there are
              borders when there are.
Audio         Volume, latency preset (Low / Normal / Safe); Music (Original or SNES
              Remaster: the row is there when a pack with music is, see below)
Controls      Rebind the eight NES buttons and the rewind and fast-forward keys
              (select a row, press Enter, press the key or pad button)
Achievements  RetroAchievements: log in, hardcore mode, progress (below)
ROM           Choose or change the cartridge dump
Updates       Check for a newer version and install it (below)

Default keys
------------
D-pad: arrow keys    A (confirm): X    B (cancel): Z    Start: Enter    Select: Right Shift
Rewind: hold Backspace (pad: RB)    Fast-forward: hold Tab (pad: RT, the right trigger)
Gamepad: NES A = the A button, NES B = X (the button left of A, as on the NES
pad), Start, Back = Select, D-pad or left stick.
Start + Select pressed together opens the menu (also Guide or LB; on Android and
the Steam Deck also LT + RT together, which closes it again too; on the keyboard
Enter + Right Shift, or Esc). The game never sees that Start or Select: the
program steps back to the moment before the first of the two went down (except
in RetroAchievements hardcore mode). Buttons still held when you leave the menu
reach the game only after you let go of them.
Keys and pad buttons can be rebound on the Controls tab: select a row, press
Enter, then press the new key or pad button. "Reset to defaults" restores them.

Other keys: P pause, F5 save state, F8 load state,
F6 / F7 previous / next save state slot, F11 or Alt+Enter fullscreen,
F12 screenshot (screenshots folder), Ctrl+R reset, F9 the game without its patches and
back (Patches, below).

Saving
------
The game saves to its adventure logs as the cartridge did. The logs are kept in
saves/<rom name>.sav, written within a second of the game changing them and
again when you end the game or quit.

Save states and rewind
----------------------
There are nine save state slots per ROM. F5 saves to the current slot, F8 loads
it, F6 / F7 change the slot (a message in the corner shows the slot and what it
holds); the Play tab shows the same.

The Save states tab lists all nine. Each slot shows the chapter the state is in,
the level of that chapter's main character (Ragnar, Alena, Taloon, the sister who
leads, the Hero) and when it was saved; a state from before a game was started
shows "Title screen". Choose a slot to load it or to save the game into it; the
slot you choose becomes the current one.

A state is never overwritten without your consent. Saving into a slot that holds
a state asks first, showing what the slot holds and where the game is now, with
Cancel selected. In the game, F5 on such a slot says what it holds and saves only
if you press F5 again within four seconds.

Hold the rewind key to go back
through the last several minutes of play (two frames of history per frame shown);
release it to continue from there. Went back too far? Hold fast-forward within
1.5 s of letting go of rewind: it goes forward again through the stretch you
rewound, at the same speed, and stops where the rewind began. Anywhere else
fast-forward speeds the game up (8x). Rewind can be switched off in dw4.ini
(rewind_enabled=0). Both are disabled in RetroAchievements hardcore mode.

Run-ahead
---------
The game answers a press one frame after it reads it at the soonest, so run-ahead
is 1 by default: the program plays one frame ahead and shows that one. Walking
moves a whole square at a time on the game's own beat, so a step can still start
up to a quarter of a second after the press, as on the console. Set run-ahead to
Off if the picture ever stutters.

Widescreen
----------
Aspect ratio: 16:9 or 16:10 shows more of the map at both sides of the picture, with
the people standing there, wherever you are walking about: towns, castles, caves, the
overworld. Nothing is stretched. Battles, the title and other screens that are not the
map keep the normal picture between black bars. The extra map comes from your ROM's
own map code as you play.

With Scaling: Integer in fullscreen or on a handheld, the picture is drawn at the largest
whole multiple that fits and the map fills the rest of the screen, above and below as
well (on a 1920x1080 screen: exactly four times, edge to edge).

A widescreen mode also steadies the picture's edges: on the console a row or column at
the edge flickers with wrong tiles or colours while you walk (the game rewrites it as
the screen scrolls); here it is drawn from the map.

Title screen
------------
Outside North America this game is Dragon Quest IV. Graphics > Title screen picks which
name the title shows: the Dragon Quest IV logo (the default) or the stone plaque
engraved with DRAGON WARRIOR IV. Nothing else changes. The choice shows at once, and it
holds with a hack on too (Delocalized brings the Dragon Quest title along; with Dragon
Warrior IV chosen the plaque is shown in its place).

Patches (ROM hacks)
-------------------
The Patches tab applies ROM hacks to the game: IPS patch files, the usual way hacks are
shared. Your ROM file is never changed; the patches are applied in memory, and each can
be switched On or Off at any time, also in the middle of a game.

Four well-known hacks come with the program, ready to switch on. They are the work of
their authors, included unchanged; each row names the hack and its author:

  Delocalized 1.08        by Translation Quest (Chicken Knife, nejimakipiyo,
                          AWildJuneAppeared, dattebayo), 2026. A new English translation
                          made from the Japanese text, with the original names, a Dragon
                          Quest title sequence and several fixes. It already contains the
                          three hacks below. romhacking.net/hacks/9523
  Graphical Uncensoring 1.0   by MetHy, 2024. The graphics changed for the North American
                          release, put back. romhacking.net/hacks/8426
  Manual Tactics 1.1      by karmic, 2025. Command every party member yourself in
                          chapter 5. romhacking.net/hacks/9221
  Seamonsters fix 1.0     by Jegriva, 2017. The sea monsters that a bug kept from
                          appearing. romhacking.net/hacks/3760

The three small ones can be used together in any combination. With Delocalized on they
are left out, because it brings its own versions of them.

Delocalized runs the game on a different cartridge board and sets itself up when the game
starts, so it cannot join a game that is already running: switch it On and it goes in when
you reset the game (Play > Reset game) or start it next. Your adventure logs are kept, so
you continue from your last save. Once the game runs with Delocalized, it can be switched
Off and On again at any moment, like the others.

Comparing: the "Compare" row on the tab (F9 on a keyboard) shows the original game for
the moment, with every patch off but your On/Off choices kept, and brings the patches
back when used again. It is for seeing what a hack changed at the spot you are in. Text
appears changed the next time it is printed (talk to the person again), graphics the next
time the scene
loads. The switch itself takes a moment of the game's choosing, usually none you can
notice; a message says when it has happened.

While a patch is on, RetroAchievements are off (they belong to the original game), and a
save state made under other patches still loads: the game returns to the saved moment and
the patches you have chosen now take over a moment later. (One exception: a state from a
game that was started without Delocalized keeps the patches it was saved with, because
Delocalized cannot join a running game.) The adventure logs
are shared: a log saved under any choice of patches continues under any other, with or
without Delocalized. One caution from the hack
itself: do not switch Manual tactics off while a party is set to the Manual tactic;
choose another tactic first.

Warning: enabling patches could break or corrupt save states. A save state holds the game
in the middle of its work, and a patch changes the program under it. Choose one patch setup
for a playthrough and keep it to the end; the Patches tab shows this warning too.

Shaders
-------
The shader row adds CRT-style effects to the picture: Scanlines (the beam lines
of a CRT), CRT (scanlines, an aperture-grille mask and a soft glow), CRT curved
(the same on a curved tube with a vignette and rounded corners) and Glow alone.
Strength scales all of them. They are drawn by the graphics card on top of the
normal scaling and cost nothing noticeable; input lag does not change.

Pixel Remaster textures
-----------------------
Textures: Pixel Remaster colors and shades every tile of the game as it is drawn: more
colors per tile, light from the upper left, shadows that lean toward blue and lights
toward yellow, deeper water and warm grey stone, while text stays crisp. It is worked
out from your ROM's own tiles while you play.
A hand-made tile pack (remaster.pak) can replace that tile by tile: put it in the
program's folder (Android: Android/data/<package>/files) and it is used whenever
Textures is set to Pixel Remaster. Packs are
drawn over the cartridge's tiles, so none comes with the program; tools/remaster in
the source shows how to make one.

SNES Remaster textures
----------------------
Textures: SNES Remaster draws the map and the people on it with the art of the Super
Famicom Dragon Quest games, and the windows as Dragon Quest III draws its own. That art
does not come with the program: the choice appears when a pack of it, sfc.pak, lies in
the program's folder (Android: Android/data/<package>/files; the same file serves Windows,
Linux, the Steam Deck and Android). The pack is built from your
own dump of Dragon Quest III (Japan, Super Famicom) with the tools in the source
(tools/sfc/README.md). It covers every place of the game and, in battle, the 46 monsters
those games draw too (the slimes, the armours, the lions, Esturk and others), in Dragon
Warrior IV's colours; the other monsters, the title and the few things it has no art for
look as they do with Pixel Remaster.

SNES Remaster music
-------------------
The same pack holds Dragon Quest III's instruments. Audio > Music: SNES Remaster plays
the game's music on them: the same notes the game plays, on strings, flute, harp, oboe,
clarinet and brass instead of the NES's three voices, with the Super Famicom's echo.
Sound effects stay as they are. The choice is separate from the textures and is
Original until you change it. The sound chip that plays the samples is LakeSnes's
(MIT licence, see LICENSE-lakesnes.txt).

RetroAchievements
-----------------
With a free account at retroachievements.org the game's achievements unlock
while you play and pop up in the corner. On the Achievements tab enter your user
name and password and choose Log in (Enter on a row edits it, Enter again keeps
it, Ctrl+V pastes). The password is sent once to the RetroAchievements server to
obtain a session token; only your user name and that token are kept
(retroachievements.txt in the data folder), so you stay logged in next time. Log
out forgets it. Hardcore mode makes unlocks count on the hardcore boards; it
disables loading save states and rewind, and switching it resets the game.
Unlocks earned while offline are sent when the connection returns. The ROM is
identified by its hash the way RetroAchievements does it. This support is built
on the rcheevos library (MIT licence, see LICENSE-rcheevos.txt).

Updates
-------
The Updates tab shows this program's version. Check for updates asks GitHub for
the newest release (only when you press it). When there is a newer one, Install
version X downloads it, checks it against the checksum GitHub lists for it and puts
it in place of this program; your settings, saves, ROM and texture packs stay as they
are, and the previous version is kept as dw4.old (dw4.exe.old on Windows). Restart now
starts the new version at once (as with Quit, play since the game last saved is not
kept); otherwise it starts next time. The program's folder has to be writable for this;
if it is not, download the new version from the release page. On Android the tab opens
the release page instead: download the APK of your edition there and open it, and it
installs over the app.

Files
-----
saves/<rom name>.sav       battery save (the game's adventure logs)
saves/<rom name>.state1-9  save state slots (F5 / F8, F6 / F7 to pick one)
screenshots/               F12 screenshots
dw4.ini                    settings and the remembered ROM path
retroachievements.txt      RetroAchievements session (user name + token), if logged in
remaster.pak               your own hand-made tiles for Textures: Pixel Remaster (optional)
sfc.pak                    the Super Famicom games' art for Textures: SNES Remaster and their
                           instruments for Music: SNES Remaster (optional)
dw4.old                    the previous version, after an update (dw4.exe.old)

These live next to the program when that folder is writable. If it is not (Program
Files, a read-only location, or the program was started from inside the zip
without extracting it), they go to %APPDATA%\DragonWarriorIVPort\DragonWarriorIV\
instead; the ROM you chose is remembered either way. Extract the zip to a normal
folder for portable use.

The ROM is verified by its hash before it is accepted: the ROM tab shows the
SHA-1, MD5 and CRC32 of your file and whether it matches a known Dragon Warrior IV
cartridge dump. Anything else is refused.

Legal
-----
Dragon Warrior and Dragon Quest belong to Square Enix. This port is an unofficial
fan project; it only runs with a cartridge dump you provide. Do not share your
.nes file. The ROM hacks that come with the program belong to their authors, named in
the Patches section above.
