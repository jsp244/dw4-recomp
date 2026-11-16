"""snesdump.py: read lsrun --ram-dump sets (PATH, .vram, .cgram, .oam, .regs) and render layers.

  snesdump.py info DUMP
  snesdump.py layers DUMP OUT_PREFIX      each BG's whole tilemap as PNG (transparent where color 0)
  snesdump.py tiles DUMP OUT.png BPP [PALROW]  the VRAM as a tile sheet
  snesdump.py obj DUMP OUT.png            the sprites as placed
"""
import sys, struct, re
from PIL import Image


class Dump:
    def __init__(self, path):
        self.ram = open(path, "rb").read()
        self.vram = struct.unpack("<32768H", open(path + ".vram", "rb").read())
        self.cg = struct.unpack("<256H", open(path + ".cgram", "rb").read())
        oam = open(path + ".oam", "rb").read()
        self.oam = oam[:512]
        self.hoam = oam[512:544]
        self.regs = {}
        self.bg = [dict() for _ in range(4)]
        self.layer = [dict() for _ in range(5)]
        for ln in open(path + ".regs"):
            t = ln.split()
            if t[0] == "bg":
                b = int(t[1])
                for k, v in zip(t[2::2], t[3::2]):
                    self.bg[b][k] = int(v, 16) if k.endswith("Adr") else int(v)
            elif t[0] == "layer":
                b = int(t[1])
                for k, v in zip(t[2::2], t[3::2]):
                    self.layer[b][k] = int(v)
            else:
                for k, v in zip(t[0::2], t[1::2]):
                    self.regs[k] = int(v, 16) if "Adr" in k else int(v)
        self.pal = [self.rgb(c) for c in self.cg]

    @staticmethod
    def rgb(c):
        r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
        return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))

    def bpp(self, layer):
        m = self.regs["mode"]
        return {0: [2, 2, 2, 2], 1: [4, 4, 2, 0], 2: [4, 4, 0, 0], 3: [8, 4, 0, 0], 4: [8, 2, 0, 0], 5: [4, 2, 0, 0], 6: [4, 0, 0, 0], 7: [8, 0, 0, 0]}[m][layer]

    def tile_pixels(self, base, num, bpp):
        """8x8 list of color indexes of tile num at word address base"""
        a = (base + num * (bpp * 4)) & 0x7FFF
        px = [[0] * 8 for _ in range(8)]
        for y in range(8):
            for p in range(0, bpp, 2):
                w = self.vram[(a + y + (p // 2) * 8) & 0x7FFF]
                lo, hi = w & 0xFF, w >> 8
                for x in range(8):
                    bit = 7 - x
                    px[y][x] |= (((lo >> bit) & 1) << p) | (((hi >> bit) & 1) << (p + 1))
        return px

    def tilemap_entry(self, layer, tx, ty):
        b = self.bg[layer]
        w = 64 if b["wider"] else 32
        h = 64 if b["higher"] else 32
        tx %= w; ty %= h
        a = b["tilemapAdr"]
        if tx >= 32:
            a += 0x400
        if ty >= 32:
            a += 0x800 if b["wider"] else 0x400
        return self.vram[(a + (ty & 31) * 32 + (tx & 31)) & 0x7FFF]

    def layer_image(self, layer):
        b = self.bg[layer]
        bpp = self.bpp(layer)
        w = 64 if b["wider"] else 32
        h = 64 if b["higher"] else 32
        im = Image.new("RGBA", (w * 8, h * 8), (0, 0, 0, 0))
        px = im.load()
        cache = {}
        for ty in range(h):
            for tx in range(w):
                e = self.tilemap_entry(layer, tx, ty)
                num, pal, pri, hf, vf = e & 0x3FF, (e >> 10) & 7, (e >> 13) & 1, (e >> 14) & 1, (e >> 15) & 1
                if num not in cache:
                    cache[num] = self.tile_pixels(b["tileAdr"], num, bpp)
                t = cache[num]
                for y in range(8):
                    for x in range(8):
                        c = t[7 - y if vf else y][7 - x if hf else x]
                        if c:
                            ci = pal * (1 << bpp) + c if bpp < 8 else c
                            if self.regs["mode"] == 0:
                                ci += layer * 32
                            px[tx * 8 + x, ty * 8 + y] = self.pal[ci & 255] + (255,)
        return im


def main():
    cmd, d = sys.argv[1], Dump(sys.argv[2])
    if cmd == "info":
        print(d.regs)
        for i in range(4):
            print("bg", i, d.bg[i], "bpp", d.bpp(i))
        print(d.layer)
    elif cmd == "layers":
        for i in range(4):
            if d.bpp(i):
                d.layer_image(i).save("%s.bg%d.png" % (sys.argv[3], i + 1))
    elif cmd == "tiles":
        bpp = int(sys.argv[4]); prow = int(sys.argv[5]) if len(sys.argv) > 5 else 0
        n = 0x8000 // (bpp * 4)
        im = Image.new("RGB", (32 * 8, (n // 32) * 8))
        px = im.load()
        for t in range(n):
            tp = d.tile_pixels(0, t, bpp)
            for y in range(8):
                for x in range(8):
                    px[(t % 32) * 8 + x, (t // 32) * 8 + y] = d.pal[(prow * 16 + tp[y][x]) & 255]
        im.save(sys.argv[3])


if __name__ == "__main__":
    main()
