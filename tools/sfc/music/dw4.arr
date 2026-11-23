# The SNES Remaster's music (tools/sfc/music.py reads this; runtime/sfcmusic.c plays it): Dragon Quest III's
# instruments, and which of them takes which of the NES's three melodic channels in each tune of the game.
# The scoring follows Dragon Quest III's own: what it plays its castle tune with plays this game's castles.
# The tunes' numbers are the game's own; the names say where the game plays them (its table of places and tunes
# at $8124 of bank 25, the overworlds' at $8116, and the "play" calls in its code).

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
# and the low brass of its battle tune's dump (it plays the harmony of its battles and the bass of its marches)
inst brass    battle 1E 571.350 a0afceda

# A sample sounds at most two octaves above its own note, so the double basses reach the C above middle C:
# where a bass line climbs over that, the next section takes those notes ("below" and "from").

# ---- castles (tune 3, the minuet): strings alone, as Dragon Quest III scores its castle ----
tune 3 castle echo BA 2D
p1  violins FAE6 30 16 follow 45        # the melody
p2  violas  FCE6 13 20 follow 45        # the second voice
tri basses  FBE6 22 22 below C5         # the bass
tri violas  FCE6 18 18 from C5

# ---- the title and the first menu (tune 2): brass over strings, as Dragon Quest III plays its marches ----
tune 2 title echo B0 30
p1  trumpet FFEA 26 26 follow 40
p1  violins FAE6 14  8 follow 40
p2  violas  FCE6 12 18 follow 40
tri basses  FBE6 22 22 below C5
tri violas  FCE6 18 18 from C5

# ---- the tune of a chapter's opening and of naming the hero (tune 31): light, a flute over a harp ----
tune 31 opening echo AB 37
p1  flute    FCE8 14 16 follow 40
p2  clarinet 8AE8  6  9 follow 40
tri harp     FFE1 14  9 ring

# ---- the overworlds ----
# chapter 1 (tune 5): its melody is the triangle's; a flute sings it, a harp plays the accompaniment's bass
# and chords, a clarinet holds the inner notes
tune 5 field1 echo AB 37
tri flute    FCE8 22 26
p1  harp     FFE1 22 14 follow 50 top 11 ring
p2  clarinet 8AE8  8 12 follow 50 top 9

# chapter 2 (tune 22), a march: brass, as Dragon Quest III plays its overworld
tune 22 field2 echo B0 30
p1  trumpet FFEA 30 30 follow 40
p2  trumpet FFEA 16 21 follow 40
tri basses  FBE6 26 26

# chapter 3 (tune 23), the merchant: the tune is the triangle's short notes, a clarinet; a bassoon walks under
# it, a harp has the chords above
tune 23 field3 echo AB 30
tri clarinet 8AE8 13 17 below C6
tri flute    FCE8 12 14 from C6
p1  bassoon  8CE8 13 13 follow 50 top 14
p2  harp     FFE1 14 10 follow 50 top 9 ring

# chapter 4 (tune 11), the sisters: an ornamented melody high up, a flute with an oboe an octave under it;
# a harp's broken chords, plucked basses
tune 11 field4 echo AB 37
p1  flute   FCE8 18 20 follow 40 top 15
p1  oboe    FCE8 10 12 octave -1 follow 40 top 15
p2  harp    FFE1 20 13 follow 50 top 12 ring
tri basses  FBE6 20 20

# chapter 5, the hero alone (tune 28): an oboe over a harp
tune 28 homeland echo AB 37
p1  oboe    FCE8 22 24 follow 40 top 13
p2  harp    FFE1 20 13 follow 50 top 12 ring
tri violas  FCE6 12 16

# chapter 5, the hero leading the party (tune 29), a march: trumpet and violins, the low brass under them
tune 29 wagon echo B0 30
p1  trumpet FFEA 26 26 follow 40 top 13
p1  violins FAE6 14  8 follow 40 top 13
p2  brass   FFE0 14 18 follow 40
tri basses  FBE6 24 24

# the ship (tune 9): the melody is the triangle's, held long: violins with a flute; a harp's waves under it
tune 9 ship echo AB 37
tri violins FAE6 24 14
tri flute   FCE8 10 13
p1  harp    FFE1 22 14 follow 50 top 13 ring
p2  violas  FCE6 12 18 follow 50 top 11

# the balloon (tune 19), a quiet waltz: a flute, a harp, strings between them
tune 19 balloon echo AB 37
p1  flute   FCE8 16 18 follow 40 top 8 from C4
p1  violas  FCE6 10 14 follow 40 top 8 below C4
p2  violas  FCE6 10 14 follow 40 top 8 below C6
p2  violins FAE6 12  8 follow 40 top 8 from C6
tri harp    FFE1 14  9 ring

# ---- towns ----
# towns (tune 12): Dragon Quest III's town instruments: flute, clarinet, harp
tune 12 town echo AB 37
p1  flute    FCE8 14 16 follow 40
p2  clarinet 8AE8  6  9 follow 40
tri harp     FFE1 14  9 ring

# the ruined town's tune (tune 14): the triangle's melody on an oboe, strings under it
tune 14 sadtown echo AB 37
tri oboe    FCE8 24 27
p2  violas  FCE6 12 18 follow 50 top 9
p1  basses  FBE6 22 22 follow 50 top 11

# the stage (tune 4: the tavern with a stage in Endor, the theater of Monbaraba in chapter 5): a clarinet and
# a flute, a bassoon's jumping bass
tune 4 stage echo AB 30
p1  clarinet 8AE8 12 16 follow 40 below C6
p1  flute    FCE8 10 12 follow 40
p2  violas   FCE6  9 12 follow 40 top 10
tri bassoon  8CE8 15 15

# the casino (tune 21: two voices, which the game plays very quietly; tune 25 is the same piece with a third,
# at the other tunes' loudness): a clarinet's tune over a harp
tune 21 casino echo AB 30
p2  clarinet 8AE8  6  7 below C6
p2  flute    FCE8  5  6 from C6
p1  harp     FFE1  6  4 ring

tune 25 casino3 echo AB 30
tri clarinet 8AE8 18 22 below C6
tri flute    FCE8 16 18 from C6
p1  harp     FFE1 16 11 ring
p2  harp     FFE1 10 15 ring

# the colosseum (tune 27): a trumpet's tune (the triangle's) over the strings' repeated chords
tune 27 colosseum echo B0 30
tri trumpet FFEA 35 35
p1  violins FBEA 27 15
p2  violas  FCE6 15 22

# ---- caves, towers, shrines ----
# caves and mines (tune 6): strings over a harp's broken chords
tune 6 dungeon echo B0 2E
p1  violins FAE6 26 14 follow 45 top 10
p2  harp    FFE1 18 12 follow 50 top 10 ring
tri basses  FBE6 22 22 below C5
tri violas  FCE6 18 18 from C5

# towers (tune 7): an oboe, a harp, low strings (Dragon Quest III's tower has harp and oboe)
tune 7 tower echo B0 20
p1  oboe    FCE4 22 20 follow 40
tri harp    FFEE 22 14 ring
p2  violas  FCE6 12 18 follow 40

# shrines (tune 8): a choir of strings; the melody is the triangle's. Its hiss is the NES's noise
tune 8 shrine echo BA 2D
tri violins FAE6 28 16
p1  violas  FCE6 12 18 follow 45 top 10
p2  violas  FCE6 16 10 follow 45 top 10

# the castle in the sky (tune 10): the castles' strings with a flute on the melody
tune 10 skycastle echo BA 2D
p1  violins FAE6 26 14 follow 45
p1  flute   FCE8 12 16 follow 45
p2  violas  FCE6 13 20 follow 45 top 10
tri basses  FBE6 22 22

# ---- battles ----
# battles (tune 13): strings and brass
tune 13 battle echo B0 2D
p1  violins FBEA 28 16 follow 40
p1  trumpet FFE6 16 18 follow 40
p2  violas  FCE6 14 20 follow 40 below C6
p2  violins FBEA 14 20 follow 40 from C6
tri basses  FEE6 24 24 below C5
tri violas  FEE6 20 20 from C5

# chapter 4's battles, and the dance on Monbaraba's stage in chapters 1 to 4 (tune 32): fiddles
tune 32 dance echo B0 2D
p1  violins FBEA 30 18 follow 40
p2  violas  FCE6 14 20 follow 40
tri basses  FEE6 24 24 below C5
tri violas  FEE6 20 20 from C5

# the great battles (tune 30: Esturk, Necrosaro): the battles' strings and trumpet, the low brass in the
# second voice; the triangle's ground note is the basses' and the timpani's
tune 30 evil echo B0 2D
p1  violins FBEA 26 16 follow 40 top 14
p1  trumpet FFE6 16 18 follow 40 top 14
p2  brass   FFE0 16 20 follow 40 top 14
p2  violas  FCE6 12 16 follow 40 top 14
tri basses  FEE6 24 24 below C5
tri violas  FEE6 20 20 from C5
tri timpani FFF6 28 28 below C3 ring

# Necrosaro's last form (tune 53): the melody is the triangle's: violins and trumpet; basses and low brass
# on the first pulse's ground
tune 53 final echo B0 2D
tri violins FAE6 28 18
tri trumpet FFE6 14 16
p1  basses  FEE6 22 22 follow 40 top 14 below C5
p1  brass   FFE0 14 16 follow 40 top 14
p1  violins FBEA 16 10 follow 40 top 14 from C5
p2  violas  FCE6 14 20 follow 40 top 14 below C6
p2  violins FBEA 12 18 follow 40 top 14 from C6

# ---- the ending (tune 49): the strings, a flute over the first voice, the low brass in the second. Its
# voices change places (the triangle is the bass at first, the melody later), so each is scored by register ----
tune 49 ending echo BA 2D
tri violas  FCE6 20 24 below C6
tri violins FAE6 14 10 from G3
p1  violins FAE6 22 12 follow 45 top 10 from G3
p1  violas  FCE6 16 20 follow 45 top 10 below G3
p1  flute   FCE8 10 14 follow 45 top 10 from C4
p2  brass   FFE0 12 16 follow 45 top 10
p2  violas  FCE6 10 14 follow 45 top 10 below C6

# ---- the short pieces ----
# fanfares of two voices (1, 15, 43; 46: a small win at the casino): trumpets, violins on the upper one
tune 1,15,43,46 fanfare echo B0 30
p1  trumpet FFEA 28 28 follow 40 top 15
p1  violins FBEA 14  8 follow 40 top 15
p2  trumpet FFEA 16 21 follow 40 top 15

# fanfares with a bass (44; 47: a larger win at the casino): the same, basses under them
tune 44,47 flourish echo B0 30
p1  trumpet FFEA 35 35 follow 40 top 15
p2  violins FBEA 22 27 follow 40 top 15
tri basses  FBE6 30 30

# the casino's great win (48), a fanfare of its own: runs of trumpet and violins, a flute's trill (the
# triangle's, high up), and timpani on the bass's two notes and on the last strokes
tune 48 jackpot echo B0 30
p1  trumpet FFEA 35 35 follow 40 top 15 from E3
p1  timpani FFF6 32 32 below E3 ring
p2  violins FBEA 22 27 follow 40 top 15
tri basses  FBE6 30 30 below C5
tri timpani FFF6 25 25 below C4 ring
tri flute   FCE8 20 22 from C5

# the night at the inn (16): a flute, strings, a harp
tune 16 inn echo AB 37
p1  flute   FCE8 16 18 follow 40
p2  violas  FCE6 12 16 follow 40 top 14
tri harp    FFE1 16 11 ring

# the slow pieces in chords (26, 51): the strings
tune 26,51 hymn echo BA 2D
p1  violins FAE6 30 16 follow 45 top 15
p2  violas  FCE6 13 20 follow 45 top 15
tri basses  FBE6 22 22

# a quick one of two voices (45): flute and clarinet, as in the towns
tune 45 cheer echo AB 37
p1  flute    FCE8 18 21 follow 40 top 15
p2  clarinet 8AE8  9 13 follow 40 top 15

# the chapters' titles (37 to 41, chapters 1 to 5), each with its chapter's instruments
tune 37 title1 echo B0 30
p1  trumpet FFEA 40 40 follow 40 top 15
p2  violas  FCE6 20 28 follow 40 top 15
tri basses  FBE6 34 34

tune 38 title2 echo B0 30
p1  violins FAE6 26 14 follow 40
p1  flute   FCE8 12 16 follow 40
p2  violas  FCE6 13 20 follow 40 top 9
tri basses  FBE6 22 22

tune 39 title3 echo AB 30
tri bassoon 8CE8 14 14
p1  harp    FFE1 14 10 follow 50 top 10 ring
p2  harp    FFE1 10 14 follow 50 top 12 ring

tune 40 title4 echo AB 37
p1  oboe     FCE8 27 29 follow 40 top 15
p2  clarinet 8AE8 12 17 follow 40 top 15
tri violas   FCE6 17 22

tune 41 title5 echo AB 37
tri flute   FCE8 22 26

# the party's defeat (50): low strings in unison, the violins holding one note over them
tune 50 defeat echo BA 2D
p1  violas  FCE6 20 26
p2  basses  FBE6 24 24
tri violins FAE6 20 12

# a run of falling and rising notes on both pulses (42): two harps
tune 42 harps echo AB 37
p1  harp    FFE1 20 13 follow 50 top 15 ring
p2  harp    FFE1 12 18 follow 50 top 15 ring

# ---- every tune without an arrangement of its own: the strings ----
tune any strings echo BA 2D
p1  violins FAE6 30 16 follow 45
p2  violas  FCE6 13 20 follow 45
tri basses  FBE6 22 22 below C5
tri violas  FCE6 18 18 from C5

# ---- left to the NES's own sound: sound effects the game makes as tunes (a rumble, a thud, a roll of low
# notes, an instrument played in the story) and the far world's drone with its wind (20, 24) ----
tune 17 nes
tune 18 nes
tune 20 nes
tune 24 nes
tune 33 rumble nes
tune 34 thud nes
tune 35 nes
tune 52 nes
