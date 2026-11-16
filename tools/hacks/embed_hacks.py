"""Build the ROM hacks listed in tools/hacks/hacks.txt into the program.

usage: python tools/hacks/embed_hacks.py [folder with the .ips files]     (default: rom/hacks)

Writes generated/hacks.h: each patch file as bytes, with its name, version, author, year and
page. Like everything in generated/ it is not committed: the patches belong to their authors.
Every file is checked against the SHA-1 in the list, so a build holds exactly the versions that
were tested; a file that is missing or different stops the tool. A build without generated/hacks.h
has no built-in hacks, and its Patches tab links to the hacks' pages instead.
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KINDS = {"whole": "PATCH_WHOLE", "uncensor": "PATCH_UNCENSOR", "manual": "PATCH_MANUAL", "sea": "PATCH_SEA", "option": "PATCH_OTHER"}


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "rom", "hacks")
    rows = []
    for line in open(os.path.join(ROOT, "tools", "hacks", "hacks.txt"), encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = [x.strip() for x in line.split("|")]
        if len(f) != 10 or f[1] not in KINDS:
            raise SystemExit(f"hacks.txt: cannot read: {line}")
        rows.append(f)
    ids = [r[0] for r in rows]
    out = ["/* The built-in ROM hacks (tools/hacks/embed_hacks.py from tools/hacks/hacks.txt). Generated - do not commit:",
           "   the patches belong to their authors. */"]
    table = []
    for n, (hid, kind, name, version, author, year, page, fname, sha1, parent) in enumerate(rows):
        path = os.path.join(folder, fname)
        if not os.path.exists(path):
            raise SystemExit(f"missing: {path}\n(download it from {page or 'the page of ' + parent} and put it there)")
        data = open(path, "rb").read()
        if hashlib.sha1(data).hexdigest() != sha1:
            raise SystemExit(f"{fname}: not the version in hacks.txt (SHA-1 {hashlib.sha1(data).hexdigest()}, expected {sha1})")
        if parent and parent not in ids:
            raise SystemExit(f"hacks.txt: {hid} names an unknown parent {parent}")
        out.append(f"static const unsigned char hack_data_{n}[] = {{")
        for k in range(0, len(data), 32):
            out.append("    " + ",".join(str(b) for b in data[k:k + 32]) + ",")
        out.append("};")
        table.append(f"    {{ {cstr(hid)}, {cstr(name)}, {cstr(version)}, {cstr(author)}, {cstr(year)}, {cstr(page)}, {cstr(parent)}, "
                     f"{KINDS[kind]}, hack_data_{n}, {len(data)} }},")
    out.append("static const hack_t builtin_hacks[] = {")
    out += table
    out.append("};")
    path = os.path.join(ROOT, "generated", "hacks.h")
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {path}: {len(rows)} patches, {sum(os.path.getsize(os.path.join(folder, r[7])) for r in rows)} bytes")


if __name__ == "__main__":
    main()
