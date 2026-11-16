Dragon Warrior IV native port - Android (handhelds and phones)
==============================================================

There are two APKs. Pick the one for your device:

- dw4-android-handheld.apk: for gaming handhelds with built-in controls (AYANEO,
  Retroid, AYN Odin and the like). The controls are the device's own buttons; nothing is
  drawn over the picture.
- dw4-android-phone.apk: for phones and tablets. A translucent on-screen controller
  (d-pad, B, A, Select, Start, MENU) is drawn over the picture. It installs as its own app,
  "Dragon Warrior IV Touch", so it can sit next to the handheld one; their saves are
  separate.

Both run on Android 7 or newer (arm64; they also carry an x86_64 build for emulators and
Chromebooks). They contain no game data: you need your own Dragon Warrior IV (USA)
cartridge dump (.nes).

Install
-------
Copy the APK to the device and open it (allow installing from that app when Android asks),
or from a PC: `adb install dw4-android-handheld.apk` (or -phone.apk).

First start
-----------
The menu opens on the ROM tab. Choose "Choose ROM file..." and pick your .nes file in the
system file picker (for example /sdcard/ROMs/nes on a Retroid). The app copies it into its
own storage, checks its hash and remembers it, so this happens once.
Alternatively put the .nes file in Android/data/org.dw4.nativeport/files/ on the shared
storage (org.dw4.nativeport.touch for the phone app); the app finds it there by itself.

The picture fills the screen's height in the 4:3 shape, with bars at the sides on wider
screens. Scaling and aspect can be changed on the Graphics tab.

Controls on a phone
-------------------
The on-screen controller: d-pad at the bottom left (slide your thumb around it; the
diagonals are narrower than the four directions), B and A at the bottom right (a thumb
between them presses both, and it can slide from one to the other), Select and Start at
the bottom, MENU at the top for this port's menu (the Back gesture opens it too). In the
menu, tap a tab or a row; tap the arrows of a row to change it. A controller can be used as
well: the on-screen controls step aside while you play with it and come back when you touch
the screen.

Controls on a handheld
----------------------
The built-in controller works as a gamepad. Default buttons (Xbox labels; on a Nintendo-
style layout such as the Retroid's, change them on the Controls tab if they feel wrong):
A = NES A (confirm), X = NES B (cancel), Start, Select. LT + RT (L2 + R2) together opens
the menu and closes it again; Start + Select together and the Android back button open it
too. R1 (hold) rewinds, R2 (hold) fast-forwards; within 1.5 s after a rewind it goes
forward again to where the rewind began.

Notes
-----
- The adventure logs, save states (9 slots) and settings live in the app's private
  storage; Android's app backup includes them. Uninstalling the app deletes them.
- RetroAchievements: log in on the Achievements tab (an on-screen or physical keyboard
  appears for the user name and password). Only the session token is stored.
- Graphics > Textures: Remastered recolors and shades the game in the manner of the Pixel
  Remaster games. A hand-made tile pack, remaster.pak, if you have one, goes in
  Android/data/org.dw4.nativeport/files/ (…nativeport.touch for the phone app).
- Updates: once the port has published releases, the Updates tab checks GitHub for a newer
  version and opens its release page; download the APK of your edition there and open it,
  and it installs over the app with its saves and settings kept (Android asks once to
  allow installs from the browser).
- A log of the last run is written to Android/data/org.dw4.nativeport/files/dw4-log.txt
  (.touch for the phone app; handy with `adb pull` when something goes wrong).
- Smooth motion: on a 120 Hz screen the display runs at 120 Hz while the app is in front,
  and every frame is shown for exactly two refreshes, locked to the compositor's
  deadlines (Android 13 and newer report them).
- Building the APK yourself: see the README (android/build_apk.py).
