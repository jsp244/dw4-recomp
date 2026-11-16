# Put your own cartridge dump here

This folder is where `make recomp` and the test harness look for your
Dragon Warrior IV (USA) `.nes` file. The file is yours: dump it from a cartridge
you own. Nothing in this folder is tracked by git (see `.gitignore`), and the
project does not contain, download, or write any game data.

Expected dump (ROM image without the 16-byte header):

| Region | SHA-1 | MD5 | CRC32 |
|---|---|---|---|
| USA | `1b3dc265ba0d7e4b2cef9b3f8b0be0847df67c3c` | `d8a1d610c93b96ad98e55e09dfcc7533` | `506e259d` |

512 KiB of PRG ROM, no CHR ROM (the cartridge has 8 KiB of CHR RAM), mapper 1
(MMC1, SUROM board) with battery RAM. The loader recognises the dump by its
hash, whatever the header says.
