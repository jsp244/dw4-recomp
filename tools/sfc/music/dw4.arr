# The SNES Remaster's music (tools/sfc/music.py reads this; runtime/sfcmusic.c plays it): Dragon Quest III's
# instruments, and which of them takes which of the NES's three melodic channels in each tune of the game.
# The scoring follows Dragon Quest III's own: what it plays its castle tune with plays this game's castles.

# The sound chip as Dragon Quest III sets it: master volume, the echo's filter. One echo delay for all tunes.
chip gain 120 delay 0B fir 0C 21 2B 2B 13 FE F3 F9 master 78

# The instruments: samples of the dump taken while Dragon Quest III plays its town tune (extract.py: DQ3_MUSIC),
# which loads eleven of them. Pitches measured from each sample's own sound through the chip.
inst flute    town 17 1052.499 7db62e04
inst oboe     town 18  533.361 97e3632c
inst clarinet town 19  266.658 19d9a10d
inst bassoon  town 1A  285.722 6729adb1
inst trumpet  town 1B  533.393 a72997c3
inst timpani  town 1C  222.246 1faa28a2
inst violins  town 1D  589.049 c05ca399
inst violas   town 1E  261.350 0b5c5efb
inst basses   town 1F  130.540 4f9650cf
inst harp     town 20  793.598 cae303c5

# ---- castles (tune 3, the minuet): strings alone, as Dragon Quest III scores its castle ----
tune 3 castle echo BA 2D
p1  violins FAE6 30 16 follow 45        # the melody
p2  violas  FCE6 13 20 follow 45        # the second voice
tri basses  FBE6 22 22                  # the bass

# ---- chapter 1's overworld (tune 5): its melody is the triangle's; a flute sings it, a harp plays the
# accompaniment's bass and chords, a clarinet holds the inner notes ----
tune 5 field1 echo AB 37
tri flute    FCE8 22 26
p1  harp     FFE1 22 14 follow 50 top 11 ring
p2  clarinet 8AE8  8 12 follow 50 top 9

# ---- the title and the first menu (tune 2): brass over strings, as Dragon Quest III plays its marches ----
tune 2 title echo B0 30
p1  trumpet FFEA 26 26 follow 40
p1  violins FAE6 14  8 follow 40
p2  violas  FCE6 12 18 follow 40
tri basses  FBE6 22 22

# ---- the tune of a chapter's opening and of naming the hero (tune 31): light, a flute over a harp ----
tune 31 opening echo AB 37
p1  flute    FCE8 14 16 follow 40
p2  clarinet 8AE8  6  9 follow 40
tri harp     FFE1 14  9 ring

# ---- towns (tune 12): Dragon Quest III's town instruments: flute, clarinet, harp ----
tune 12 town echo AB 37
p1  flute    FCE8 14 16 follow 40
p2  clarinet 8AE8  6  9 follow 40
tri harp     FFE1 14  9 ring

# ---- the ruined town's tune (tune 14): the triangle's melody on an oboe, strings under it ----
tune 14 sadtown echo AB 37
tri oboe    FCE8 24 27
p2  violas  FCE6 12 18 follow 50 top 9
p1  basses  FBE6 22 22 follow 50 top 11

# ---- caves and mines (tune 6): strings over a harp's broken chords ----
tune 6 dungeon echo B0 2E
p1  violins FAE6 26 14 follow 45 top 10
p2  harp    FFE1 18 12 follow 50 top 10 ring
tri basses  FBE6 22 22

# ---- towers (tune 7): an oboe, a harp, low strings (Dragon Quest III's tower has harp and oboe) ----
tune 7 tower echo B0 20
p1  oboe    FCE4 22 20 follow 40
tri harp    FFEE 22 14 ring
p2  violas  FCE6 12 18 follow 40

# ---- shrines (tune 8): a choir of strings; the melody is the triangle's. Its hiss is the NES's noise ----
tune 8 shrine echo BA 2D
tri violins FAE6 28 16
p1  violas  FCE6 12 18 follow 45 top 10
p2  violas  FCE6 16 10 follow 45 top 10

# ---- the castle in the sky (tune 10): the castles' strings with a flute on the melody ----
tune 10 skycastle echo BA 2D
p1  violins FAE6 26 14 follow 45
p1  flute   FCE8 12 16 follow 45
p2  violas  FCE6 13 20 follow 45 top 10
tri basses  FBE6 22 22

# ---- battles (tune 13): strings and brass ----
tune 13 battle echo B0 2D
p1  violins FBEA 28 16 follow 40
p1  trumpet FFE6 16 18 follow 40
p2  violas  FCE6 14 20 follow 40
tri basses  FEE6 24 24

# ---- chapter 2's overworld (tune 22), a march: brass, as Dragon Quest III plays its overworld ----
tune 22 field2 echo B0 30
p1  trumpet FFEA 30 30 follow 40
p2  trumpet FFEA 16 21 follow 40
tri basses  FBE6 26 26

# ---- every tune without an arrangement of its own: the strings ----
tune any strings echo BA 2D
p1  violins FAE6 30 16 follow 45
p2  violas  FCE6 13 20 follow 45
tri basses  FBE6 22 22

# ---- sound effects the game makes as tunes, and a few sequences not looked into yet: the NES's own ----
tune 17 nes
tune 18 nes
tune 20 nes
tune 24 nes
tune 33 rumble nes
tune 34 thud nes
tune 35 nes
tune 41 nes
tune 42 nes
tune 48 nes
tune 52 nes
