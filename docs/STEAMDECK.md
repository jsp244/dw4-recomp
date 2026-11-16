Dragon Warrior IV native port - Steam Deck edition
==================================================

This build starts in fullscreen at the Deck's own 1280x800, and the menus are driven
with the Deck's controls. It contains no game data: you need your own
Dragon Warrior IV (USA) cartridge dump (.nes).

Install (Desktop Mode)
----------------------
1. Extract this folder somewhere in your home folder, for example ~/Games/DragonWarriorIV.
2. Copy your Dragon Warrior IV (USA) .nes file into that same folder. The program finds
   it there by itself the first time (it checks the file's hash and remembers it).
3. Open Konsole in that folder (Dolphin: right-click > Open Terminal Here) and run
   `python3 add-to-steam.py`. It closes Steam, adds Dragon Warrior IV as a non-Steam game
   with its library artwork from SteamGridDB (portrait and wide capsules, hero, logo,
   icon), and starts Steam again. Running it again replaces the shortcut;
   `python3 add-to-steam.py --remove` takes it out again.
   (By hand instead: Steam > Games > Add a Non-Steam Game to My Library > Browse, switch
   the file type filter to "All Files", pick `dw4`, Add Selected Programs.)
4. Return to Game Mode and start Dragon Warrior IV from your library.

Controls (Steam's default gamepad layout)
-----------------------------------------
D-pad or left stick   move
A                     NES A (confirm, the command menu)
X                     NES B (cancel)
Menu (three lines)    Start
View (two squares)    Select
L2 + R2 together      this port's menu (graphics, audio, controls, save states,
                      RetroAchievements); again to close it. Start + Select works
                      too, and Steam's own menu as usual.
R1 (hold)             rewind
R2 (hold)             fast-forward; within 1.5 s after a rewind it goes forward
                      again to where the rewind began
Every button can be changed on the menu's Controls tab.

Tips
----
- Graphics > Scaling: Fit (the default) fills the screen's height with the 4:3 picture,
  with bars at the sides. Integer keeps exact pixels at 3x.
- Deck OLED: for perfectly even motion set the refresh rate to 60 Hz for this game in
  the quick access menu (Performance). The NES runs at 60 Hz.
- Latency: Graphics > Low-latency sync (on by default) hands each frame to the screen
  without vsync, so no frame waits in a queue. Switch it off for vsync: the smoothest
  scrolling, at up to one frame more latency.
- Graphics > Textures: Remastered recolors and shades the game in the manner of the
  Pixel Remaster games (more colors per tile, soft light from the upper left). A
  hand-made tile pack, remaster.pak, if you have one, goes in the game folder.
- Updates: once the port has published releases, Check for updates on the menu's
  Updates tab, then Install, installs a new one in place (the previous version stays
  as dw4.old); Restart now brings you straight back in, still in the same Steam session.
- Saves (the game's adventure logs and the 9 save state slots), settings and
  screenshots are kept in the game folder.
- RetroAchievements: log in on the menu's Achievements tab. The password is only sent
  to retroachievements.org to obtain a session token; only the token is stored.
- The full player notes are in README.txt.
