#!/usr/bin/env python3
"""tools/sfc/supplement.py - the art of the games a player need not own.

The SNES Remaster's pack is made from the player's own Dragon Quest III. A few of its pictures come from other
games of the series (Dragon Quest I.II's castle brick, its single tree, its barrier floor): the supplement holds
just those, the ones the map files name and nothing else of the libraries they were found in, so that one Super
Famicom ROM is enough to build the pack.

The supplement is locked to the Dragon Quest III ROM: its bytes are combined with a stream made from that ROM's
contents, and without the very same ROM they are noise. It is written by whoever maintains the map files, from all
the games (extract.py --dq12 ... --write-supplement), and read by everyone else (extract.py without --dq12).

usage:
    supplement.py pack OUT.bin DQ3.sfc LIBRARY... -- MAPFILE...   the entries of those libraries that the map files use
    supplement.py unpack IN.bin DQ3.sfc OUTDIR                    write the libraries again (only those entries)
    supplement.py list IN.bin DQ3.sfc                             what it holds
"""
import hashlib
import json
import os
import pickle
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MAGIC, VERSION = b"DW4X", 1
SQUARE_KEYS = ("px", "lo", "hi")                # a map square: its picture and its two layers
FIGURE_KEYS = ("px", "w", "h", "group", "pose")


def rom_key(rom_path):
    d = open(rom_path, "rb").read()
    if len(d) % 0x8000 == 512:                  # (a copier's header is not part of the game)
        d = d[512:]
    return hashlib.sha256(d).digest()


def stream(key, n):
    out = bytearray()
    block = 0
    while len(out) < n:
        out += hashlib.sha256(key + struct.pack("<Q", block)).digest()
        block += 1
    return bytes(out[:n])


def locked(data, key):
    return bytes(a ^ b for a, b in zip(data, stream(key, len(data))))


def plain(e):
    """a library's entry as JSON can hold it: only what the pack is built from"""
    keys = FIGURE_KEYS if "group" in e else SQUARE_KEYS
    return {k: (list(e[k]) if isinstance(e.get(k), (tuple, list)) else e.get(k)) for k in keys}


def entry(d):
    e = {k: (tuple(v) if isinstance(v, list) else v) for k, v in d.items()}
    e["from"] = "supplement"
    if "group" not in e:
        e.update({"at": (0, 0), "count": 0, "sig": ()})
    return e


def pack(out, rom, libraries, maps):
    import pack as packer
    with tempfile.TemporaryDirectory() as tmp:
        m = packer.build(os.path.join(tmp, "unused.pak"), maps, quiet=True)
    held, total = {}, 0
    for path in libraries:
        lib = m.files.get(os.path.abspath(path))
        if lib is None:                         # (no map file names this library: nothing of it is used)
            lib = []
        held[os.path.basename(path)] = [plain(e) for e in lib if id(e) in m.used]
        total += len(held[os.path.basename(path)])
    body = zlib.compress(json.dumps({"libraries": held}, separators=(",", ":"), sort_keys=True).encode("ascii"), 9)
    with open(out, "wb") as f:
        f.write(MAGIC + struct.pack("<II", VERSION, len(body)) + hashlib.sha1(body).digest() + locked(body, rom_key(rom)))
    print("%s: %d pictures of %d libraries (%s), %d bytes" % (
        out, total, len(held), ", ".join("%s %d" % (k, len(v)) for k, v in sorted(held.items())), os.path.getsize(out)))


def read(path, rom):
    d = open(path, "rb").read()
    if d[:4] != MAGIC or struct.unpack_from("<I", d, 4)[0] != VERSION:
        raise SystemExit("%s is not a supplement this tool reads" % path)
    n = struct.unpack_from("<I", d, 8)[0]
    body = locked(d[32:32 + n], rom_key(rom))
    if hashlib.sha1(body).digest() != d[12:32]:
        raise SystemExit("%s does not open with %s: it is locked to the dump of Dragon Quest III (Japan) it was made with "
                         "(tools/sfc/README.md names it)" % (path, rom))
    return json.loads(zlib.decompress(body).decode("ascii"))["libraries"]


def unpack(path, rom, out_dir):
    held = read(path, rom)
    for name, entries in sorted(held.items()):
        with open(os.path.join(out_dir, name), "wb") as f:
            pickle.dump([entry(e) for e in entries], f)
    print("%s: %s" % (path, ", ".join("%s %d" % (k, len(v)) for k, v in sorted(held.items()))))


def main():
    a = sys.argv
    if len(a) >= 6 and a[1] == "pack" and "--" in a:
        cut = a.index("--")
        pack(a[2], a[3], a[4:cut], a[cut + 1:])
    elif len(a) == 5 and a[1] == "unpack":
        unpack(a[2], a[3], a[4])
    elif len(a) == 4 and a[1] == "list":
        for name, entries in sorted(read(a[2], a[3]).items()):
            print("%s: %d" % (name, len(entries)))
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
