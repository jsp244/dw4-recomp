#!/usr/bin/env python3
"""
recomp.py - 6502 static recompiler for the Dragon Warrior IV native port.

Reads the user's own .nes file, discovers the code in every 16 KiB PRG bank
and emits C source in which every 6502 instruction is a labelled statement
inside a routine function. The emitted code calls the shared hardware runtime
for memory and timing, so it behaves exactly like the interpreter and can be
cosimulated against it.

How the code is found:
  * recursive descent from the reset and interrupt vectors of the two fixed
    banks (15 and 31: the cartridge is an MMC1 with 512 KiB of PRG, and each
    256 KiB half has its own bank at $C000);
  * the game's far calls. A routine in another bank is called with
        BRK, <routine>, <bank code>
    The interrupt handler reads the two bytes, switches bank and jumps through
    the table of routine addresses every code bank starts with at $8000, so a
    far call names its target (see brk_info) and each bank's table lists the
    routines other banks may call. Execution resumes after the two bytes, or
    one byte later for the routines that take a parameter byte there;
  * recomp/symbols.txt: the few facts the code itself does not spell out (the
    NMI vector points into RAM, where the game keeps a JMP to the handler);
  * recomp/entries.txt: entry points reached only through jump tables, and the
    far routines that take a parameter byte, both learnt from execution
    coverage recorded by the headless harness (--coverage) and written by
    this tool (--write-entries). Addresses only, no game data.
Whatever is still missed runs through the interpreter at run time, with the
same results.

Nothing from the ROM is written anywhere but the --out directory, which
is ignored by git. Run with your own cartridge dump.

usage: recomp.py --rom FILE --out DIR [--coverage FILE ...] [--symbols FILE]
                 [--entries FILE] [--write-entries FILE] [--max-routine N] [--listing]
"""
import argparse
import collections
import hashlib
import os
import sys

# ---------------------------------------------------------------------------
# Opcode table: (mnemonic, mode, base cycles) for all 256 opcodes
# ---------------------------------------------------------------------------
_T = """
BRK imp 7|ORA izx 6|KIL imp 2|SLO izx 8|NOP zp 3|ORA zp 3|ASL zp 5|SLO zp 5|PHP imp 3|ORA imm 2|ASL acc 2|ANC imm 2|NOP abs 4|ORA abs 4|ASL abs 6|SLO abs 6
BPL rel 2|ORA izy 5|KIL imp 2|SLO izy 8|NOP zpx 4|ORA zpx 4|ASL zpx 6|SLO zpx 6|CLC imp 2|ORA aby 4|NOP imp 2|SLO aby 7|NOP abx 4|ORA abx 4|ASL abx 7|SLO abx 7
JSR abs 6|AND izx 6|KIL imp 2|RLA izx 8|BIT zp 3|AND zp 3|ROL zp 5|RLA zp 5|PLP imp 4|AND imm 2|ROL acc 2|ANC imm 2|BIT abs 4|AND abs 4|ROL abs 6|RLA abs 6
BMI rel 2|AND izy 5|KIL imp 2|RLA izy 8|NOP zpx 4|AND zpx 4|ROL zpx 6|RLA zpx 6|SEC imp 2|AND aby 4|NOP imp 2|RLA aby 7|NOP abx 4|AND abx 4|ROL abx 7|RLA abx 7
RTI imp 6|EOR izx 6|KIL imp 2|SRE izx 8|NOP zp 3|EOR zp 3|LSR zp 5|SRE zp 5|PHA imp 3|EOR imm 2|LSR acc 2|ALR imm 2|JMP abs 3|EOR abs 4|LSR abs 6|SRE abs 6
BVC rel 2|EOR izy 5|KIL imp 2|SRE izy 8|NOP zpx 4|EOR zpx 4|LSR zpx 6|SRE zpx 6|CLI imp 2|EOR aby 4|NOP imp 2|SRE aby 7|NOP abx 4|EOR abx 4|LSR abx 7|SRE abx 7
RTS imp 6|ADC izx 6|KIL imp 2|RRA izx 8|NOP zp 3|ADC zp 3|ROR zp 5|RRA zp 5|PLA imp 4|ADC imm 2|ROR acc 2|ARR imm 2|JMP ind 5|ADC abs 4|ROR abs 6|RRA abs 6
BVS rel 2|ADC izy 5|KIL imp 2|RRA izy 8|NOP zpx 4|ADC zpx 4|ROR zpx 6|RRA zpx 6|SEI imp 2|ADC aby 4|NOP imp 2|RRA aby 7|NOP abx 4|ADC abx 4|ROR abx 7|RRA abx 7
NOP imm 2|STA izx 6|NOP imm 2|SAX izx 6|STY zp 3|STA zp 3|STX zp 3|SAX zp 3|DEY imp 2|NOP imm 2|TXA imp 2|XAA imm 2|STY abs 4|STA abs 4|STX abs 4|SAX abs 4
BCC rel 2|STA izy 6|KIL imp 2|SHA izy 6|STY zpx 4|STA zpx 4|STX zpy 4|SAX zpy 4|TYA imp 2|STA aby 5|TXS imp 2|TAS aby 5|SHY abx 5|STA abx 5|SHX aby 5|SHA aby 5
LDY imm 2|LDA izx 6|LDX imm 2|LAX izx 6|LDY zp 3|LDA zp 3|LDX zp 3|LAX zp 3|TAY imp 2|LDA imm 2|TAX imp 2|LXA imm 2|LDY abs 4|LDA abs 4|LDX abs 4|LAX abs 4
BCS rel 2|LDA izy 5|KIL imp 2|LAX izy 5|LDY zpx 4|LDA zpx 4|LDX zpy 4|LAX zpy 4|CLV imp 2|LDA aby 4|TSX imp 2|LAS aby 4|LDY abx 4|LDA abx 4|LDX aby 4|LAX aby 4
CPY imm 2|CMP izx 6|NOP imm 2|DCP izx 8|CPY zp 3|CMP zp 3|DEC zp 5|DCP zp 5|INY imp 2|CMP imm 2|DEX imp 2|AXS imm 2|CPY abs 4|CMP abs 4|DEC abs 6|DCP abs 6
BNE rel 2|CMP izy 5|KIL imp 2|DCP izy 8|NOP zpx 4|CMP zpx 4|DEC zpx 6|DCP zpx 6|CLD imp 2|CMP aby 4|NOP imp 2|DCP aby 7|NOP abx 4|CMP abx 4|DEC abx 7|DCP abx 7
CPX imm 2|SBC izx 6|NOP imm 2|ISC izx 8|CPX zp 3|SBC zp 3|INC zp 5|ISC zp 5|INX imp 2|SBC imm 2|NOP imp 2|SBC imm 2|CPX abs 4|SBC abs 4|INC abs 6|ISC abs 6
BEQ rel 2|SBC izy 5|KIL imp 2|ISC izy 8|NOP zpx 4|SBC zpx 4|INC zpx 6|ISC zpx 6|SED imp 2|SBC aby 4|NOP imp 2|ISC aby 7|NOP abx 4|SBC abx 4|INC abx 7|ISC abx 7
"""
OPCODES = []
for _row in _T.strip().splitlines():
    for _cell in _row.split("|"):
        _m, _mode, _c = _cell.split()
        OPCODES.append((_m, _mode, int(_c)))
assert len(OPCODES) == 256

MODE_LEN = {"imp": 1, "acc": 1, "imm": 2, "zp": 2, "zpx": 2, "zpy": 2, "izx": 2, "izy": 2, "rel": 2,
            "abs": 3, "abx": 3, "aby": 3, "ind": 3}

UNIT_SIZE = 0x4000      # one 16 KiB PRG bank
SWITCH_BANK = 0xFF91    # the fixed banks' routine that maps bank A at $8000


def is_fixed(bank):
    """banks 15 and 31 sit at $C000, each for its half of the ROM"""
    return (bank & 15) == 15


def fixed_of(bank):
    return bank | 15


def base_of(bank):
    return 0xC000 if is_fixed(bank) else 0x8000


def brk_info(b1, b2):
    """The far call BRK, b1, b2 as the interrupt handler at $C408 reads it:
    (kind, bank, routine, bytes of the whole sequence). Kinds: "call" (routine b1 of the bank's
    table at $8000), "ptr" (only fetches that table entry into the zero page), "flag" (a bit
    operation on the game's flags with a mask byte following, no call)."""
    low = b2 & 0x0F
    if low == 0x0B:
        if b2 < 0xCB:
            return ("call", 0x16, 4, 3)
        if b2 < 0xFB:
            return ("flag", None, None, 4)
        return ("call", 0x19, 3, 3)
    if low == 0x03:
        hi = b2 >> 4
        if hi < 10:
            return ("call", 0x10, hi, 3)
        if hi < 13:
            return ("call", 0x10, hi + 0x18, 3)
        return ("call", 0x12, hi - 12, 3)
    bank = ((b2 & 0x08) << 1) | (b2 >> 4)
    if b1 & 0x80:
        return ("ptr", bank, b1 & 0x3F, 3)
    return ("call", bank, b1, 3)

BRANCHES = {"BPL": "!cpu.n", "BMI": "cpu.n", "BVC": "!cpu.v", "BVS": "cpu.v",
            "BCC": "!cpu.c", "BCS": "cpu.c", "BNE": "!cpu.z", "BEQ": "cpu.z"}
LOADS = {"LDA": "A", "LDX": "X", "LDY": "Y"}
ALU_A = {"ORA": "A |= {m}; set_nz(A);", "AND": "A &= {m}; set_nz(A);", "EOR": "A ^= {m}; set_nz(A);",
         "ADC": "op_adc({m});", "SBC": "op_sbc({m});", "CMP": "op_cmp(A, {m});",
         "CPX": "op_cmp(X, {m});", "CPY": "op_cmp(Y, {m});", "BIT": "op_bit({m});",
         "LAX": "A = X = {m}; set_nz(A);", "LAS": "op_las({m});",
         "ANC": "op_anc({m});", "ALR": "op_alr({m});", "ARR": "op_arr({m});", "AXS": "op_axs({m});",
         "XAA": "op_xaa({m});", "LXA": "op_lxa({m});"}
RMW_FN = {"ASL": "op_asl", "LSR": "op_lsr", "ROL": "op_rol", "ROR": "op_ror", "INC": "op_inc", "DEC": "op_dec",
          "SLO": "fn_slo", "RLA": "fn_rla", "SRE": "fn_sre", "RRA": "fn_rra", "DCP": "fn_dcp", "ISC": "fn_isc"}
STORES = {"STA": "A", "STX": "X", "STY": "Y", "SAX": "(A & X)"}
IMPLIED = {
    "CLC": "cpu.c = 0;", "SEC": "cpu.c = 1;", "CLV": "cpu.v = 0;", "CLD": "cpu.d = 0;", "SED": "cpu.d = 1;",
    "CLI": "cpu.irq_delay_i = cpu.i; cpu.irq_delay_active = 1; cpu.i = 0;",
    "SEI": "cpu.irq_delay_i = cpu.i; cpu.irq_delay_active = 1; cpu.i = 1;",
    "TAX": "X = A; set_nz(X);", "TAY": "Y = A; set_nz(Y);", "TXA": "A = X; set_nz(A);", "TYA": "A = Y; set_nz(A);",
    "TSX": "X = SP; set_nz(X);", "TXS": "SP = X;",
    "INX": "X++; set_nz(X);", "DEX": "X--; set_nz(X);", "INY": "Y++; set_nz(Y);", "DEY": "Y--; set_nz(Y);",
    "PHA": "push8(A);", "PHP": "push8(cpu_get_p(1));", "PLA": "A = pop8(); set_nz(A);",
    "PLP": "{ uint8_t old_i = cpu.i; cpu_set_p(pop8()); cpu.irq_delay_i = old_i; cpu.irq_delay_active = 1; }",
    "NOP": "",
}


class Insn:
    __slots__ = ("addr", "op", "mn", "mode", "length", "cycles", "operand", "raw", "target", "far")

    def __init__(self, addr, op, mn, mode, length, cycles, operand, raw):
        self.addr, self.op, self.mn, self.mode, self.length, self.cycles = addr, op, mn, mode, length, cycles
        self.operand, self.raw = operand, raw
        self.target = None
        self.far = None     # BRK: (kind, bank, routine)
        if mode == "rel":
            self.target = (addr + 2 + ((operand ^ 0x80) - 0x80)) & 0xFFFF
        elif mode == "abs" and mn in ("JMP", "JSR"):
            self.target = operand

    @property
    def next(self):
        return (self.addr + self.length) & 0xFFFF

    def falls_through(self):
        return self.mn not in ("JMP", "RTS", "RTI", "KIL")

    def text(self):
        v, m = self.operand, self.mode
        if self.far:
            kind, bank, idx = self.far
            if kind == "flag":
                return f"BRK flags ${self.raw[1]:02X},${self.raw[2]:02X},${self.raw[3]:02X}"
            return f"BRK {kind} bank {bank} routine ${idx:02X}" + (" +byte" if self.length == 4 else "")
        if m == "rel":
            f = f"${self.target:04X}"
        else:
            f = {"imp": "", "acc": "A", "imm": f"#${v:02X}", "zp": f"${v:02X}", "zpx": f"${v:02X},X",
                 "zpy": f"${v:02X},Y", "izx": f"(${v:02X},X)", "izy": f"(${v:02X}),Y", "abs": f"${v:04X}",
                 "abx": f"${v:04X},X", "aby": f"${v:04X},Y", "ind": f"(${v:04X})"}[m]
        return f"{self.mn} {f}".strip()


class Unit:
    """Code of one 16 KiB PRG bank: at $8000, or at $C000 for the fixed banks."""

    def __init__(self, bank, data):
        self.bank, self.base, self.data = bank, base_of(bank), data
        self.end = self.base + UNIT_SIZE
        self.insns = {}
        self.roots = set()
        self.worklist = []
        self.xrefs = collections.defaultdict(set)   # target -> set of source addrs
        self.comp = {}      # addr -> component id
        self.comps = []     # list of sorted address lists

    @property
    def name(self):
        return f"b{self.bank:02d}_{self.base:04X}"

    def contains(self, a):
        return self.base <= a < self.end

    def word(self, a):
        off = a - self.base
        return self.data[off] | (self.data[off + 1] << 8)

    def decode(self, a, far_skip):
        off = a - self.base
        op = self.data[off]
        mn, mode, cyc = OPCODES[op]
        if op == 0x00:      # BRK: the far call and the bytes that follow it
            if off + 3 > UNIT_SIZE:
                return None
            kind, bank, idx, ln = brk_info(self.data[off + 1], self.data[off + 2])
            if kind == "call" and (bank, idx) in far_skip:
                ln = 4
            if off + ln > UNIT_SIZE:
                return None
            ins = Insn(a, op, mn, mode, ln, cyc, 0, bytes(self.data[off:off + ln]))
            ins.far = (kind, bank, idx)
            return ins
        ln = MODE_LEN[mode]
        if off + ln > UNIT_SIZE:
            return None
        raw = self.data[off:off + ln]
        operand = 0
        if ln == 2:
            operand = raw[1]
        elif ln == 3:
            operand = raw[1] | (raw[2] << 8)
        return Insn(a, op, mn, mode, ln, cyc, operand, bytes(raw))


class Recompiler:
    def __init__(self, prg, max_routine, symbols, far_skip):
        self.prg = prg
        self.nbanks = len(prg) // UNIT_SIZE
        self.max_routine = max_routine
        self.symbols = symbols          # (bank, addr) -> name
        self.far_skip = far_skip        # {(bank, routine)}: far routines followed by a parameter byte
        self.units = {b: Unit(b, prg[b * UNIT_SIZE:(b + 1) * UNIT_SIZE]) for b in range(self.nbanks)}
        self.dirty = set()
        self.unresolved = set()         # (fixed bank, source, target): calls into $8000-$BFFF with the bank unknown
        self.far_calls = 0

    # ---- analysis ----
    def add_root(self, unit, a, src=None):
        if not unit.contains(a):
            return
        if src is not None:
            unit.xrefs[a].add(src)
        if a not in unit.insns:
            unit.worklist.append(a)
            self.dirty.add(unit.bank)
        unit.roots.add(a)

    def add_target(self, src_unit, src, target, is_call):
        """control transfer from src (in src_unit) to target anywhere in the address space"""
        if src_unit.contains(target):
            src_unit.xrefs[target].add(src)
            if target not in src_unit.insns:
                src_unit.worklist.append(target)
            if is_call:
                src_unit.roots.add(target)
            return
        if target < 0x8000:
            return      # RAM: the game's JMP stubs there run through the interpreter
        if target >= 0xC000:
            self.add_root(self.units[fixed_of(src_unit.bank)], target, src)
        # a fixed bank reaching into $8000-$BFFF: the bank is whatever was switched in (bank_calls)

    def add_far(self, unit, ins):
        kind, bank, idx = ins.far
        if kind != "call" or bank >= self.nbanks:
            return
        self.far_calls += 1
        table = self.units[bank]
        if is_fixed(bank) or 2 * idx + 1 >= UNIT_SIZE:
            return
        t = table.word(0x8000 + 2 * idx)
        if 0x8000 <= t < 0xC000:
            self.add_root(table, t, ins.addr)
        elif t >= 0xC000:
            self.add_root(self.units[fixed_of(bank)], t, ins.addr)

    def descend(self, unit):
        while unit.worklist:
            a = unit.worklist.pop()
            if a in unit.insns or not unit.contains(a):
                continue
            ins = unit.decode(a, self.far_skip)
            if ins is None:
                continue
            unit.insns[a] = ins
            if ins.mn in BRANCHES:
                self.add_target(unit, a, ins.target, False)
                unit.worklist.append(ins.next)
            elif ins.mn == "JMP" and ins.mode == "abs":
                self.add_target(unit, a, ins.target, False)
            elif ins.mn == "JSR":
                self.add_target(unit, a, ins.target, True)
                unit.worklist.append(ins.next)
            elif ins.mn == "BRK":
                self.add_far(unit, ins)
                unit.worklist.append(ins.next)
                unit.roots.add(ins.next)
            elif ins.falls_through():
                unit.worklist.append(ins.next)

    def bank_calls(self, unit):
        """A fixed bank calling into $8000-$BFFF: the bank is the one the code switched in just
        before (LDA #bank / JSR $FF91), followed in address order through each routine."""
        a_const = cur = None
        for a in sorted(unit.insns):
            ins = unit.insns[a]
            if a in unit.roots:
                a_const = cur = None
            if ins.mn in ("JSR", "JMP") and ins.mode == "abs":
                t = ins.target
                if ins.mn == "JSR" and t == SWITCH_BANK:
                    cur = a_const
                elif 0x8000 <= t < 0xC000:
                    if cur is not None and (cur | 15) == unit.bank and not is_fixed(cur):
                        self.add_root(self.units[cur], t, a)
                        self.unresolved.discard((unit.bank, a, t))
                    else:
                        self.unresolved.add((unit.bank, a, t))
                a_const = None
                if ins.mn == "JMP":
                    cur = None
            elif ins.mn == "LDA":
                a_const = ins.operand if ins.mode == "imm" else None
            elif ins.mn in ("RTS", "RTI"):
                a_const = cur = None
            elif ins.mn in ("PLA", "TXA", "TYA", "ADC", "SBC", "AND", "ORA", "EOR", "LAX", "BRK") or ins.mode == "acc":
                a_const = None

    def fixpoint(self):
        while True:
            while self.dirty:
                self.descend(self.units[self.dirty.pop()])
            for b in range(self.nbanks):
                if is_fixed(b):
                    self.bank_calls(self.units[b])
            if not self.dirty:
                break

    def table_entries(self, unit):
        """the routine addresses a code bank starts with (empty slots are 0); none in a data bank"""
        out, lo, n = [], 0xC000, 0
        while 0x8000 + 2 * n < lo:
            w = unit.word(0x8000 + 2 * n)
            if 0x8000 <= w < 0xC000:
                lo = min(lo, w)
                out.append(w)
            elif w != 0:
                return []
            n += 1
        return out if len(out) >= 3 else []

    def analyse(self, coverage, entries, code_ptrs):
        for fb in range(self.nbanks):
            if not is_fixed(fb):
                continue
            u = self.units[fb]
            for vec in (0xFFFA, 0xFFFC, 0xFFFE):
                self.add_root(u, u.word(vec))       # ignored when it points into RAM (the NMI's)
        for (bank, a) in code_ptrs:                 # symbols.txt: a pointer to code kept in the ROM
            u = self.units[bank]
            self.add_root(self.units[bank if u.contains(u.word(a)) else fixed_of(bank)], u.word(a))
        for b, u in self.units.items():             # far-call tables
            if not is_fixed(b):
                for t in self.table_entries(u):
                    self.add_root(u, t)
        for (bank, a) in entries:
            self.add_root(self.units[bank], a)
        if coverage is not None:
            for b, u in self.units.items():
                seg = coverage[b * UNIT_SIZE:(b + 1) * UNIT_SIZE]
                want = 0x03 if u.base == 0x8000 else 0x0C     # executed in the slots the unit covers
                for off in range(UNIT_SIZE):
                    if seg[off] & want:
                        self.add_root(u, u.base + off)
        self.fixpoint()

    # ---- routine partition ----
    def partition(self, unit):
        addrs = sorted(unit.insns)
        parent = {a: a for a in addrs}
        size = {a: 1 for a in addrs}

        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a

        def union(a, b):
            ra, rb = find(a), find(b)
            if ra == rb:
                return True
            if size[ra] + size[rb] > self.max_routine:
                return False
            if size[ra] < size[rb]:
                ra, rb = rb, ra
            parent[rb] = ra
            size[ra] += size[rb]
            return True

        # fallthrough and branch edges first (keep loops together), then JMPs
        edges_first, edges_jmp = [], []
        for a, ins in unit.insns.items():
            if ins.mn in BRANCHES:
                if ins.target in unit.insns:
                    edges_first.append((a, ins.target))
                if ins.next in unit.insns:
                    edges_first.append((a, ins.next))
            elif ins.mn == "JMP" and ins.mode == "abs":
                if ins.target in unit.insns:
                    edges_jmp.append((a, ins.target))
            elif ins.falls_through() and ins.mn not in ("JSR", "BRK"):
                if ins.next in unit.insns:
                    edges_first.append((a, ins.next))
            elif ins.mn in ("JSR", "BRK") and ins.next in unit.insns:
                edges_first.append((a, ins.next))
        for a, b in edges_first:
            union(a, b)
        for a, b in edges_jmp:
            union(a, b)
        groups = collections.defaultdict(list)
        for a in addrs:
            groups[find(a)].append(a)
        unit.comps = sorted(groups.values(), key=lambda g: g[0])
        unit.comp = {}
        for i, g in enumerate(unit.comps):
            for a in g:
                unit.comp[a] = i

    # ---- emission ----
    def read_expr(self, ins):
        v, m = ins.operand, ins.mode
        if m == "imm":
            return f"0x{v:02X}"
        if m == "zp":
            return f"ram[0x{v:02X}]"
        if m == "zpx":
            return f"ram[(uint8_t)(0x{v:02X} + X)]"
        if m == "zpy":
            return f"ram[(uint8_t)(0x{v:02X} + Y)]"
        if m == "abs":
            if v < 0x2000:
                return f"ram[0x{v & 0x7FF:03X}]"
            return f"mem_read(0x{v:04X})"
        if m in ("abx", "aby"):
            idx = "X" if m == "abx" else "Y"
            if v + 255 < 0x2000:
                return f"rd_idx_ram(0x{v:04X}, {idx})"
            return f"rd_idx(0x{v:04X}, {idx})"
        if m == "izx":
            return f"mem_read(zp_read16((uint8_t)(0x{v:02X} + X)))"
        if m == "izy":
            return f"rd_idx(zp_read16(0x{v:02X}), Y)"
        raise ValueError(m)

    def write_stmt(self, ins, val, nxt):
        """statements performing a store; includes the bank-switch exit when ROM may be written"""
        v, m = ins.operand, ins.mode
        ex = f"EXIT(0x{nxt:04X});"
        if m == "zp":
            return f"ram[0x{v:02X}] = {val};"
        if m == "zpx":
            return f"ram[(uint8_t)(0x{v:02X} + X)] = {val};"
        if m == "zpy":
            return f"ram[(uint8_t)(0x{v:02X} + Y)] = {val};"
        if m == "abs":
            if v < 0x2000:
                return f"ram[0x{v & 0x7FF:03X}] = {val};"
            s = f"mem_write(0x{v:04X}, {val});"
            return s + (" " + ex if v >= 0x8000 else "")
        if m in ("abx", "aby"):
            idx = "X" if m == "abx" else "Y"
            if v + 255 < 0x2000:
                return f"ram[(uint16_t)(0x{v:04X} + {idx}) & 0x7FF] = {val};"
            return f"{{ uint16_t a_ = addr_idx_wr(0x{v:04X}, {idx}); mem_write(a_, {val}); if (a_ >= 0x8000) {ex} }}"
        if m == "izx":
            return f"{{ uint16_t a_ = zp_read16((uint8_t)(0x{v:02X} + X)); mem_write(a_, {val}); if (a_ >= 0x8000) {ex} }}"
        if m == "izy":
            return f"{{ uint16_t a_ = addr_idx_wr(zp_read16(0x{v:02X}), Y); mem_write(a_, {val}); if (a_ >= 0x8000) {ex} }}"
        raise ValueError(m)

    def rmw_stmt(self, ins, fn, nxt):
        v, m = ins.operand, ins.mode
        ex = f"EXIT(0x{nxt:04X});"
        if m == "acc":
            return f"A = {fn}(A);"
        if m == "zp":
            return f"ram[0x{v:02X}] = {fn}(ram[0x{v:02X}]);"
        if m in ("zpx", "zpy"):
            idx = "X" if m == "zpx" else "Y"
            return f"{{ uint8_t a_ = (uint8_t)(0x{v:02X} + {idx}); ram[a_] = {fn}(ram[a_]); }}"
        if m == "abs":
            if v < 0x2000:
                return f"ram[0x{v & 0x7FF:03X}] = {fn}(ram[0x{v & 0x7FF:03X}]);"
            return f"RMW_AT(0x{v:04X}, {fn});" + (" " + ex if v >= 0x8000 else "")
        if m in ("abx", "aby"):
            idx = "X" if m == "abx" else "Y"
            if v + 255 < 0x2000:
                return f"{{ uint16_t a_ = (uint16_t)(0x{v:04X} + {idx}) & 0x7FF; ram[a_] = {fn}(ram[a_]); }}"
            return f"{{ uint16_t a_ = addr_idx_wr(0x{v:04X}, {idx}); RMW_AT(a_, {fn}); if (a_ >= 0x8000) {ex} }}"
        if m == "izx":
            return f"{{ uint16_t a_ = zp_read16((uint8_t)(0x{v:02X} + X)); RMW_AT(a_, {fn}); if (a_ >= 0x8000) {ex} }}"
        if m == "izy":
            return f"{{ uint16_t a_ = addr_idx_wr(zp_read16(0x{v:02X}), Y); RMW_AT(a_, {fn}); if (a_ >= 0x8000) {ex} }}"
        raise ValueError(m)

    def emit_insn(self, unit, ins, comp_set, out):
        a, mn, nxt = ins.addr, ins.mn, ins.next
        sym = self.symbols.get((unit.bank, a))
        hexb = " ".join(f"{b:02X}" for b in ins.raw)
        label = f" <{sym}>" if sym else ""
        out.append(f"    case 0x{a:04X}: L_{a:04X}: INSN(0x{a:04X}); /* {hexb:<8} {ins.text()}{label} */")
        body = []
        cyc = f"CYC({ins.cycles});"
        sync = f"SYNC(0x{nxt:04X});"

        if mn in BRANCHES:
            cond = BRANCHES[mn]
            t = ins.target
            cross = 1 if ((t ^ nxt) & 0x100) else 0
            if t in comp_set:
                taken = f"cpu.cycles += {1 + cross}; SYNC(0x{t:04X}); goto L_{t:04X};"
            else:
                taken = f"cpu.cycles += {1 + cross}; EXIT(0x{t:04X});"
            body.append(f"{cyc} if ({cond}) {{ {taken} }} {sync}")
        elif mn in LOADS:
            r = LOADS[mn]
            body.append(f"{cyc} {r} = {self.read_expr(ins)}; set_nz({r}); {sync}")
        elif mn in ALU_A:
            body.append(f"{cyc} {ALU_A[mn].format(m=self.read_expr(ins))} {sync}")
        elif mn in STORES:
            body.append(f"{cyc} {self.write_stmt(ins, STORES[mn], nxt)} {sync}")
        elif mn in RMW_FN:
            body.append(f"{cyc} {self.rmw_stmt(ins, RMW_FN[mn], nxt)} {sync}")
        elif mn in IMPLIED:
            if mn == "NOP" and ins.mode not in ("imp", "imm"):
                body.append(f"{cyc} (void){self.read_expr(ins)}; {sync}")
            elif mn == "NOP":
                body.append(f"{cyc} {sync}")
            else:
                body.append(f"{cyc} {IMPLIED[mn]} {sync}")
        elif mn == "JMP":
            if ins.mode == "abs":
                t = ins.target
                if t in comp_set:
                    body.append(f"{cyc} SYNC(0x{t:04X}); goto L_{t:04X};")
                else:
                    body.append(f"{cyc} EXIT(0x{t:04X});")
            else:
                body.append(f"{cyc} {{ uint16_t t_ = jmp_ind(0x{ins.operand:04X}); EXIT(t_); }}")
        elif mn == "JSR":
            body.append(f"{cyc} push16(0x{(nxt - 1) & 0xFFFF:04X}); EXIT(0x{ins.target:04X});")
        elif mn == "RTS":
            body.append(f"{cyc} {{ uint16_t t_ = (uint16_t)(pop16() + 1); EXIT(t_); }}")
        elif mn == "RTI":
            body.append(f"{cyc} cpu_set_p(pop8()); {{ uint16_t t_ = pop16(); EXIT(t_); }}")
        elif mn == "BRK":
            body.append(f"{cyc} push16(0x{(a + 2) & 0xFFFF:04X}); push8(cpu_get_p(1)); cpu.i = 1; "
                        f"{{ uint16_t t_ = mem_read16(cpu.nmi_pending ? 0xFFFA : 0xFFFE); cpu.nmi_pending = 0; EXIT(t_); }}")
        elif mn == "KIL":
            body.append(f"{cyc} cpu.jammed = 1; EXIT(0x{a:04X});")
        elif mn in ("SHY", "SHX", "SHA", "TAS"):
            v = ins.operand
            if ins.mode == "izy":
                base = f"zp_read16(0x{v:02X})"
            else:
                base = f"0x{v:04X}"
            idx = "X" if ins.mode == "abx" else "Y"
            val = {"SHY": "Y", "SHX": "X", "SHA": "(A & X)", "TAS": "SP"}[mn]
            pre = "SP = A & X; " if mn == "TAS" else ""
            body.append(f"{cyc} {pre}op_unstable_store({base}, {idx}, {val}); EXIT(0x{nxt:04X});")
        else:
            raise ValueError(f"unhandled mnemonic {mn}")
        out.append("        " + " ".join(body))
        return nxt

    def emit_unit(self, unit, outdir):
        self.partition(unit)
        lines = []
        lines.append(f"/* {unit.name}: 16 KiB PRG bank {unit.bank} at ${unit.base:04X}-${unit.end - 1:04X}.")
        lines.append(" * Generated by recomp/recomp.py from the user's ROM - do not commit. */")
        lines.append('#include "recomp_rt.h"')
        lines.append("")
        func_names = []
        for ci, comp in enumerate(unit.comps):
            root = comp[0]
            sym = self.symbols.get((unit.bank, root))
            fname = f"r_{unit.name}_{root:04X}" + (f"_{sym}" if sym else "")
            func_names.append(fname)
            comp_set = set(comp)
            lines.append(f"/* routine at ${root:04X}, {len(comp)} instructions */")
            lines.append(f"static void {fname}(void) {{")
            lines.append("    switch (cpu.pc) {")
            out = []
            for i, a in enumerate(comp):
                ins = unit.insns[a]
                nxt = self.emit_insn(unit, ins, comp_set, out)
                if ins.falls_through() and ins.mn not in ("JSR", "BRK"):
                    following = comp[i + 1] if i + 1 < len(comp) else None
                    if nxt != following:
                        if nxt in comp_set:
                            out.append(f"        goto L_{nxt:04X};")
                        else:
                            out.append(f"        EXIT(0x{nxt:04X});")
            lines.extend(out)
            lines.append("    default: return;")
            lines.append("    }")
            lines.append("}")
            lines.append("")
        # entry table
        lines.append(f"static const uint16_t entry_{unit.name}[0x{UNIT_SIZE:X}] = {{")
        row = []
        for off in range(UNIT_SIZE):
            a = unit.base + off
            idx = unit.comp.get(a, -1) + 1
            row.append(str(idx))
            if len(row) == 32:
                lines.append("    " + ",".join(row) + ",")
                row = []
        lines.append("};")
        lines.append(f"static const recomp_fn funcs_{unit.name}[] = {{")
        for f in func_names:
            lines.append(f"    {f},")
        lines.append("};")
        lines.append(f"const recomp_unit_t unit_{unit.name} = {{ entry_{unit.name}, funcs_{unit.name} }};")
        with open(os.path.join(outdir, f"{unit.name}.c"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        return func_names

    def emit_main(self, outdir, sha1):
        lines = ['#include "recomp_rt.h"', "", f'const char *recomp_rom_sha1 = "{sha1}";', ""]
        for u in self.units.values():
            lines.append(f"extern const recomp_unit_t unit_{u.name};")
        lines.append("")
        lines.append("/* per 16 KiB bank: its code at $8000 and at $C000 (a bank has one or the other) */")
        lines.append(f"static const recomp_unit_t *recomp_units[{self.nbanks}][2] = {{")
        for bank in range(self.nbanks):
            u = self.units.get(bank)
            cell = f"&unit_{u.name}" if u else "NULL"
            lines.append("    { " + (f"NULL, {cell}" if is_fixed(bank) else f"{cell}, NULL") + " },")
        lines.append("};")
        lines.append("")
        lines.append("int recomp_run(void) {")
        lines.append("    uint16_t pc = cpu.pc;")
        lines.append("    if (pc < 0x8000 || cpu.jammed) return 0;")
        lines.append("    int slot = (pc >> 14) & 1;")
        lines.append(f"    unsigned bank = mapper.prg_bank[slot << 1] >> 1;   /* 8 KiB bank numbers: two to a bank */")
        lines.append(f"    if (bank >= {self.nbanks}) return 0;")
        lines.append("    const recomp_unit_t *u = recomp_units[bank][slot];")
        lines.append("    if (!u) return 0;")
        lines.append(f"    uint16_t idx = u->entry[pc & 0x{UNIT_SIZE - 1:X}];")
        lines.append("    if (!idx) return 0;")
        lines.append("    sys.native_blocks++;")
        lines.append("    u->funcs[idx - 1]();")
        lines.append("    return 1;")
        lines.append("}")
        with open(os.path.join(outdir, "recomp_main.c"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")

    def emit_listing(self, unit, outdir):
        os.makedirs(os.path.join(outdir, "listing"), exist_ok=True)
        with open(os.path.join(outdir, "listing", f"{unit.name}.lst"), "w", newline="\n") as f:
            f.write(f"; PRG bank {unit.bank} at ${unit.base:04X}  ({len(unit.insns)} instructions, {len(unit.comps)} routines)\n")
            for a in sorted(unit.insns):
                ins = unit.insns[a]
                sym = self.symbols.get((unit.bank, a))
                if a in unit.roots or sym:
                    f.write(f"\n{sym or f'sub_{a:04X}'}:\n")
                refs = unit.xrefs.get(a)
                xr = ("  ; <- " + ",".join(f"{s:04X}" for s in sorted(refs))) if refs else ""
                hexb = " ".join(f"{b:02X}" for b in ins.raw)
                f.write(f"  {a:04X}  {hexb:<9} {ins.text():<16}{xr}\n")


def load_coverage(paths, prg_size):
    cov = bytearray(prg_size)
    for p in paths:
        data = open(p, "rb").read()
        if len(data) < prg_size:
            raise SystemExit(f"coverage file {p} is too small for this ROM")
        for i in range(prg_size):
            cov[i] |= data[i]
    return cov


def parse_place(text):
    bank, _, addr = text.partition(":")
    return int(bank), int(addr, 16)


def load_symbols(path):
    """symbols.txt: 'BB:AAAA Name' names an address of a bank (decimal bank, hex address);
    'codeptr BB:AAAA' marks a 16-bit pointer to code stored in the ROM"""
    syms, ptrs = {}, []
    if not path or not os.path.exists(path):
        return syms, ptrs
    for line in open(path):
        parts = line.split(";")[0].split()
        if len(parts) < 2:
            continue
        if parts[0] == "codeptr":
            ptrs.append(parse_place(parts[1]))
        else:
            syms[parse_place(parts[0])] = parts[1]
    return syms, ptrs


def load_entries(path):
    """entries.txt: 'entry BB:AAAA' = an instruction reached only indirectly;
    'skip BB:RR' = far routine RR (hex) of bank BB takes a parameter byte after the call"""
    entries, skip = [], set()
    if not path or not os.path.exists(path):
        return entries, skip
    for line in open(path):
        parts = line.split("#")[0].split()
        if len(parts) < 2:
            continue
        if parts[0] == "entry":
            entries.append(parse_place(parts[1]))
        elif parts[0] == "skip":
            skip.add(parse_place(parts[1]))
    return entries, skip


def covered(coverage, bank, a):
    return coverage[bank * UNIT_SIZE + (a & (UNIT_SIZE - 1))] & (0x03 if a < 0xC000 else 0x0C) != 0


def learn_far_skip(prg, coverage):
    """far routines that take a parameter byte: where a far call was executed, the instruction
    three bytes on never was, and the one four bytes on was"""
    votes = collections.defaultdict(collections.Counter)
    for bank in range(len(prg) // UNIT_SIZE):
        base = base_of(bank)
        seg = prg[bank * UNIT_SIZE:(bank + 1) * UNIT_SIZE]
        for off in range(UNIT_SIZE - 4):
            if seg[off] != 0x00 or not covered(coverage, bank, base + off):
                continue
            kind, fbank, idx, ln = brk_info(seg[off + 1], seg[off + 2])
            if kind != "call":
                continue
            if covered(coverage, bank, base + off + 3):
                votes[(fbank, idx)][3] += 1
            elif covered(coverage, bank, base + off + 4):
                votes[(fbank, idx)][4] += 1
    skip = set()
    for key, v in votes.items():
        if v[4] and v[3]:
            print(f"  far routine {key[0]}:{key[1]:02X}: resumes after 3 bytes at {v[3]} calls and after 4 at {v[4]}; left at 3")
        elif v[4]:
            skip.add(key)
    return skip


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(__file__)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--coverage", action="append", default=[])
    ap.add_argument("--symbols", default=os.path.join(here, "symbols.txt"))
    ap.add_argument("--entries", default=os.path.join(here, "entries.txt"))
    ap.add_argument("--write-entries", metavar="FILE",
                    help="with --coverage: write the entry points and parameter-byte routines the coverage adds")
    ap.add_argument("--max-routine", type=int, default=1200)
    ap.add_argument("--listing", action="store_true", help="also write annotated disassembly listings")
    args = ap.parse_args()

    data = open(args.rom, "rb").read()
    if data[:4] != b"NES\x1a":
        raise SystemExit("not an iNES file")
    prg_units, chr_units = data[4], data[5]
    nes2 = (data[7] & 0x0C) == 0x08
    if nes2:
        prg_units |= (data[9] & 0x0F) << 8
        chr_units |= (data[9] & 0xF0) << 4
    off = 16 + (512 if data[6] & 4 else 0)
    prg_size = prg_units * 16384
    prg = data[off:off + prg_size]
    sha1 = hashlib.sha1(data[off:off + prg_size + chr_units * 8192]).hexdigest()
    if prg_size != 32 * UNIT_SIZE:
        raise SystemExit("not Dragon Warrior IV: the game has 512 KiB of PRG ROM")

    coverage = load_coverage(args.coverage, prg_size) if args.coverage else None
    symbols, code_ptrs = load_symbols(args.symbols)
    entries, far_skip = load_entries(args.entries)
    if coverage is not None:
        far_skip |= learn_far_skip(prg, coverage)

    if args.write_entries:
        if coverage is None:
            raise SystemExit("--write-entries needs --coverage")
        # what the code and the hints already find, then each executed instruction they miss
        rc = Recompiler(prg, args.max_routine, symbols, far_skip)
        rc.analyse(None, entries, code_ptrs)
        added = []
        for b, u in rc.units.items():
            for o in range(UNIT_SIZE):
                a = u.base + o
                if covered(coverage, b, a) and a not in u.insns:
                    added.append((b, a))
                    rc.add_root(u, a)
                    rc.fixpoint()
        entries = sorted(set(entries) | set(added))
        with open(args.write_entries, "w", newline="\n") as f:
            f.write("# Entry points the code does not name (jump tables, addresses pushed for RTS) and far\n"
                    "# routines that take a parameter byte after the call. Learnt from execution coverage\n"
                    "# (nes-headless --coverage) and written by recomp.py --write-entries: addresses only.\n")
            for bank, idx in sorted(far_skip):
                f.write(f"skip {bank}:{idx:02X}\n")
            for bank, a in entries:
                f.write(f"entry {bank}:{a:04X}\n")
        print(f"{args.write_entries}: {len(entries)} entry points ({len(added)} new), {len(far_skip)} parameter-byte routines")

    rc = Recompiler(prg, args.max_routine, symbols, far_skip)
    rc.analyse(coverage, entries, code_ptrs)

    os.makedirs(args.out, exist_ok=True)
    for f in os.listdir(args.out):
        if f.endswith(".c"):
            os.remove(os.path.join(args.out, f))
    total_insns = total_routines = 0
    for b in sorted(rc.units):
        u = rc.units[b]
        if not u.insns:
            continue
        funcs = rc.emit_unit(u, args.out)
        if args.listing:
            rc.emit_listing(u, args.out)
        total_insns += len(u.insns)
        total_routines += len(funcs)
        biggest = max(len(c) for c in u.comps)
        print(f"  {u.name}: {len(u.insns):5d} instructions, {len(funcs):4d} routines (largest {biggest})")
    rc.units = {k: v for k, v in rc.units.items() if v.insns}
    rc.emit_main(args.out, sha1)
    with open(os.path.join(args.out, ".gitignore"), "w") as f:
        f.write("*\n!.gitignore\n")
    print(f"{len(rc.units)} banks with code, {total_insns} instructions, {total_routines} routines, {rc.far_calls} far calls")
    if rc.unresolved:
        print(f"{len(rc.unresolved)} calls from the fixed banks into $8000-$BFFF with the bank unknown (interpreted unless found otherwise)")
    if coverage is not None:
        hit = miss = 0
        for b, u in rc.units.items():
            for o in range(UNIT_SIZE):
                if covered(coverage, b, u.base + o):
                    hit += 1
        print(f"coverage: {hit} executed instructions, all translated")
    print(f"ROM sha1 {sha1}")


if __name__ == "__main__":
    main()
