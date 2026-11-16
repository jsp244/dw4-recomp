/*
 * mapper.c - MMC1 (1), the game's own mapper; MMC3 (4), which some ROM hacks move the game to
 * (and the test ROMs use); NROM (0) for the test ROMs.
 *
 * Dragon Warrior IV is an SUROM board: an MMC1 with 512 KiB of PRG ROM, 8 KiB of CHR RAM and
 * 8 KiB of battery RAM. The MMC1 itself addresses 256 KiB of PRG, so the board takes the top
 * address line from bit 4 of the CHR bank register: it picks the half of the ROM, and with it
 * which bank is the "fixed" one at $C000 (bank 15 or bank 31).
 */
#include "nes.h"
#include <stdio.h>

mapper_t mapper;

static uint8_t *prg_bank_ptr(uint32_t bank8k) {
    uint32_t count = cart.prg_size / 8192;
    return cart.prg + (bank8k % count) * 8192;
}

uint8_t *mapper_chr_bank_ptr(uint32_t bank1k) {
    if (cart.chr_size == 0) return cart.chr_ram + (bank1k % 8) * 1024;
    uint32_t count = cart.chr_size / 1024;
    return cart.chr + (bank1k % count) * 1024;
}

void mapper_set_mirroring(int mode) {
    static const uint16_t tables[5][4] = {
        { 0x000, 0x000, 0x400, 0x400 },   /* horizontal */
        { 0x000, 0x400, 0x000, 0x400 },   /* vertical */
        { 0x000, 0x000, 0x000, 0x000 },   /* single screen, lower */
        { 0x400, 0x400, 0x400, 0x400 },   /* single screen, upper */
        { 0x000, 0x400, 0x800, 0xC00 },   /* four screen */
    };
    for (int i = 0; i < 4; i++) ppu.nt_offset[i] = tables[mode][i];
}

void mapper_update_pointers(void) {
    uint32_t prg8k = cart.prg_size / 8192;
    mapper.chr_writable = (cart.chr_size == 0);
    switch (cart.mapper) {
    case 0: {
        mapper.prg_bank[0] = 0; mapper.prg_bank[1] = 1;
        mapper.prg_bank[2] = (uint16_t)((prg8k >= 4) ? 2 : 0);
        mapper.prg_bank[3] = (uint16_t)((prg8k >= 4) ? 3 : 1);
        for (int i = 0; i < 8; i++) mapper.chr_bank[i] = (uint16_t)i;
        break; }
    case 1: {
        uint32_t prg16k = cart.prg_size / 16384;
        uint8_t mode = (mapper.mmc1_ctrl >> 2) & 3;
        uint32_t bank = mapper.mmc1_prg & 0x0F;
        /* 512 KiB boards (SUROM): the CHR register's bit 4 is PRG A18. With 4 KiB CHR banks the
           line follows whichever CHR register the PPU is using; the last one written stands in */
        uint32_t outer = 0, inner = prg16k < 16 ? prg16k : 16;
        if (prg16k > 16) {
            uint8_t reg = ((mapper.mmc1_ctrl & 0x10) && mapper.mmc1_last_chr) ? mapper.mmc1_chr1 : mapper.mmc1_chr0;
            outer = (reg & 0x10) ? 16 : 0;
        }
        uint32_t lo, hi;
        if (mode <= 1) { lo = (bank & ~1u) % inner; hi = (lo + 1) % inner; }
        else if (mode == 2) { lo = 0; hi = bank % inner; }
        else { lo = bank % inner; hi = inner - 1; }
        lo += outer; hi += outer;
        mapper.prg_bank[0] = (uint16_t)(lo * 2); mapper.prg_bank[1] = (uint16_t)(lo * 2 + 1);
        mapper.prg_bank[2] = (uint16_t)(hi * 2); mapper.prg_bank[3] = (uint16_t)(hi * 2 + 1);
        if (mapper.mmc1_ctrl & 0x10) {
            for (int i = 0; i < 4; i++) mapper.chr_bank[i] = (uint16_t)(mapper.mmc1_chr0 * 4 + i);
            for (int i = 0; i < 4; i++) mapper.chr_bank[4 + i] = (uint16_t)(mapper.mmc1_chr1 * 4 + i);
        } else {
            for (int i = 0; i < 8; i++) mapper.chr_bank[i] = (uint16_t)((mapper.mmc1_chr0 & ~1) * 4 + i);
        }
        switch (mapper.mmc1_ctrl & 3) {
        case 0: mapper_set_mirroring(2); break;
        case 1: mapper_set_mirroring(3); break;
        case 2: mapper_set_mirroring(1); break;
        default: mapper_set_mirroring(0); break;
        }
        break; }
    case 4: {
        uint8_t *r = mapper.regs;
        bool prg_mode = (mapper.bank_select & 0x40) != 0;
        bool chr_mode = (mapper.bank_select & 0x80) != 0;
        uint16_t last = (uint16_t)(prg8k - 1), second_last = (uint16_t)(prg8k - 2);
        if (!prg_mode) {
            mapper.prg_bank[0] = r[6]; mapper.prg_bank[1] = r[7];
            mapper.prg_bank[2] = second_last; mapper.prg_bank[3] = last;
        } else {
            mapper.prg_bank[0] = second_last; mapper.prg_bank[1] = r[7];
            mapper.prg_bank[2] = r[6]; mapper.prg_bank[3] = last;
        }
        uint16_t c2k0 = (uint16_t)(r[0] & 0xFE), c2k1 = (uint16_t)(r[1] & 0xFE);
        if (!chr_mode) {
            mapper.chr_bank[0] = c2k0; mapper.chr_bank[1] = (uint16_t)(c2k0 + 1);
            mapper.chr_bank[2] = c2k1; mapper.chr_bank[3] = (uint16_t)(c2k1 + 1);
            mapper.chr_bank[4] = r[2]; mapper.chr_bank[5] = r[3];
            mapper.chr_bank[6] = r[4]; mapper.chr_bank[7] = r[5];
        } else {
            mapper.chr_bank[0] = r[2]; mapper.chr_bank[1] = r[3];
            mapper.chr_bank[2] = r[4]; mapper.chr_bank[3] = r[5];
            mapper.chr_bank[4] = c2k0; mapper.chr_bank[5] = (uint16_t)(c2k0 + 1);
            mapper.chr_bank[6] = c2k1; mapper.chr_bank[7] = (uint16_t)(c2k1 + 1);
        }
        if (cart.four_screen) mapper_set_mirroring(4);
        else mapper_set_mirroring((mapper.mirroring & 1) ? 0 : 1);
        break; }
    default:
        for (int i = 0; i < 4; i++) mapper.prg_bank[i] = (uint16_t)(i % prg8k);
        for (int i = 0; i < 8; i++) mapper.chr_bank[i] = (uint16_t)i;
        break;
    }
    for (int i = 0; i < 4; i++) mapper.prg_ptr[i] = prg_bank_ptr(mapper.prg_bank[i]);
    for (int i = 0; i < 8; i++) mapper.chr_ptr[i] = mapper_chr_bank_ptr(mapper.chr_bank[i]);
}

void mapper_init(void) {
    memset(&mapper, 0, sizeof mapper);
    mapper_reset();
}

void mapper_reset(void) {
    mapper.bank_select = 0;
    for (int i = 0; i < 8; i++) mapper.regs[i] = 0;
    mapper.regs[0] = 0; mapper.regs[1] = 2; mapper.regs[2] = 4; mapper.regs[3] = 5;
    mapper.regs[4] = 6; mapper.regs[5] = 7; mapper.regs[6] = 0; mapper.regs[7] = 1;
    mapper.mirroring = cart.mirroring ? 0 : 1;   /* $A000 bit0: 0 = vertical, 1 = horizontal */
    mapper.wram_ctrl = 0x80;   /* MMC3: RAM enabled at power-on for compatibility */
    mapper.irq_latch = mapper.irq_counter = 0;
    mapper.irq_reload = mapper.irq_enabled = mapper.irq_asserted = false;
    mapper.a12_low = false;
    mapper.a12_low_since = 0;
    mapper.mmc1_shift = 0; mapper.mmc1_count = 0;
    mapper.mmc1_ctrl = 0x0C; mapper.mmc1_chr0 = mapper.mmc1_chr1 = mapper.mmc1_prg = 0;
    mapper.mmc1_last_chr = 0; mapper.mmc1_last_write = 0;
    cpu.irq_line &= (uint8_t)~IRQ_MAPPER;
    if (cart.mapper == 0 || cart.mapper == 1) mapper_set_mirroring(cart.four_screen ? 4 : (cart.mirroring ? 1 : 0));
    mapper_update_pointers();
}

/* ---- PRG RAM ---- */
uint8_t mapper_read_wram(uint16_t a) {
    if (a < 0x6000) return cpu.open_bus;
    if (cart.mapper == 4) {
        if (!(mapper.wram_ctrl & 0x80)) return cpu.open_bus;
        return cart.wram[a & 0x1FFF];
    }
    if (cart.mapper == 1) {
        if (mapper.mmc1_prg & 0x10) return cpu.open_bus;
        return cart.wram[a & 0x1FFF];
    }
    return cart.wram[a & 0x1FFF];
}

void mapper_write_wram(uint16_t a, uint8_t v) {
    if (a < 0x6000) return;
    if (cart.mapper == 4) {
        if ((mapper.wram_ctrl & 0xC0) != 0x80) return;
        cart.wram[a & 0x1FFF] = v;
        return;
    }
    if (cart.mapper == 1) {
        if (mapper.mmc1_prg & 0x10) return;
        cart.wram[a & 0x1FFF] = v;
        return;
    }
    cart.wram[a & 0x1FFF] = v;
}

/* ---- mapper registers ---- */
static void mmc3_write(uint16_t a, uint8_t v) {
    if (sys.debug_log)
        fprintf((FILE *)sys.debug_log, "frame %llu sl %d dot %d: mapper write $%04X = $%02X (counter %d latch %d en %d)\n",
                (unsigned long long)ppu.frame, ppu.scanline, ppu.dot, a, v, mapper.irq_counter, mapper.irq_latch, mapper.irq_enabled);
    switch (a & 0xE001) {
    case 0x8000:
        mapper.bank_select = v;
        mapper_update_pointers();
        break;
    case 0x8001:
        mapper.regs[mapper.bank_select & 7] = v;
        mapper_update_pointers();
        break;
    case 0xA000:
        mapper.mirroring = v;
        mapper_update_pointers();
        break;
    case 0xA001:
        mapper.wram_ctrl = v;
        break;
    case 0xC000:
        mapper.irq_latch = v;
        break;
    case 0xC001:
        mapper.irq_reload = true;
        break;
    case 0xE000:
        mapper.irq_enabled = false;
        mapper.irq_asserted = false;
        cpu.irq_line &= (uint8_t)~IRQ_MAPPER;
        break;
    case 0xE001:
        mapper.irq_enabled = true;
        break;
    }
}

static void mmc1_write(uint16_t a, uint8_t v) {
    /* The MMC1 ignores a write that follows another on the very next CPU cycle: the second write
       of a read-modify-write instruction (the game resets the chip with INC $FFDF). Both writes of
       one instruction happen at the same count of cpu.cycles here, and no two instructions do. */
    if (mapper.mmc1_last_write == cpu.cycles) return;
    mapper.mmc1_last_write = cpu.cycles;
    if (sys.debug_log)
        fprintf((FILE *)sys.debug_log, "frame %llu sl %d: mmc1 write $%04X = $%02X (ctrl %02X chr %02X %02X prg %02X)\n",
                (unsigned long long)ppu.frame, ppu.scanline, a, v, mapper.mmc1_ctrl, mapper.mmc1_chr0, mapper.mmc1_chr1, mapper.mmc1_prg);
    if (v & 0x80) {
        mapper.mmc1_shift = 0; mapper.mmc1_count = 0;
        mapper.mmc1_ctrl |= 0x0C;
        mapper_update_pointers();
        return;
    }
    mapper.mmc1_shift |= (uint8_t)((v & 1) << mapper.mmc1_count);
    mapper.mmc1_count++;
    if (mapper.mmc1_count == 5) {
        uint8_t val = mapper.mmc1_shift & 0x1F;
        switch ((a >> 13) & 3) {
        case 0: mapper.mmc1_ctrl = val; break;
        case 1: mapper.mmc1_chr0 = val; mapper.mmc1_last_chr = 0; break;
        case 2: mapper.mmc1_chr1 = val; mapper.mmc1_last_chr = 1; break;
        case 3: mapper.mmc1_prg = val; break;
        }
        mapper.mmc1_shift = 0; mapper.mmc1_count = 0;
        mapper_update_pointers();
    }
}

void mapper_write_rom(uint16_t a, uint8_t v) {
    switch (cart.mapper) {
    case 1: mmc1_write(a, v); break;
    case 4: mmc3_write(a, v); break;
    default: break;
    }
}

/* ---- for code that calls into the game, and for patches that change the board ---- */

/* The 16 KiB bank at $8000, as the game's own bank switch leaves the mapper. */
void mapper_select_bank16(int bank) {
    if (cart.mapper == 4) {
        mapper.bank_select &= (uint8_t)~0x40;
        mapper.regs[6] = (uint8_t)(bank * 2);
        mapper.regs[7] = (uint8_t)(bank * 2 + 1);
    } else {
        mapper.mmc1_prg = (uint8_t)((mapper.mmc1_prg & 0x10) | (bank & 0x0F));
        mapper.mmc1_chr0 = (uint8_t)((mapper.mmc1_chr0 & ~0x10) | (bank & 0x10));
        mapper.mmc1_last_chr = 0;
        mapper.mmc1_ctrl |= 0x0C;
        mapper.mmc1_shift = mapper.mmc1_count = 0;
    }
    mapper_update_pointers();
}

/* Can the other board show the PRG mapping in force right now? An MMC1 has 16 KiB banks and a
   fixed one per 256 KiB half; an MMC3 has 8 KiB banks and fixes the last two. after[] gets the
   8 KiB bank per slot the other board would then have (the same ones). */
int mapper_convertible(int to, uint16_t after[4]) {
    uint32_t n8 = cart.prg_size / 8192;
    uint16_t b[4];
    for (int i = 0; i < 4; i++) b[i] = after[i] = (uint16_t)(mapper.prg_bank[i] % n8);
    if (to == cart.mapper) return 1;
    if (cart.mapper == 1 && to == 4)
        return b[2] == n8 - 2 && b[3] == n8 - 1 && (mapper.mmc1_ctrl & 2);       /* no single-screen mirroring on an MMC3 */
    if (cart.mapper == 4 && to == 1)
        return !(b[0] & 1) && b[1] == b[0] + 1 && b[2] == n8 - 2 && b[3] == n8 - 1 &&
               (b[0] >> 5) == ((n8 - 2) >> 5);                                    /* same 256 KiB half as the fixed bank */
    return 0;
}

/* Become the other board with the same memory map: PRG banks, mirroring, RAM enable. (CHR is
   8 KiB of RAM on both, mapped straight.) Only when mapper_convertible says so. */
void mapper_convert(int to) {
    if (to == cart.mapper) return;
    uint16_t lo = mapper.prg_bank[0], hi = mapper.prg_bank[1];
    if (cart.mapper == 1 && to == 4) {
        int horizontal = (mapper.mmc1_ctrl & 3) == 3, ram_on = !(mapper.mmc1_prg & 0x10);
        cart.mapper = 4;
        mapper_reset();
        mapper.regs[6] = (uint8_t)lo; mapper.regs[7] = (uint8_t)hi;
        mapper.mirroring = (uint8_t)horizontal;
        mapper.wram_ctrl = ram_on ? 0x80 : 0x00;
    } else if (cart.mapper == 4 && to == 1) {
        int horizontal = mapper.mirroring & 1, ram_on = (mapper.wram_ctrl & 0x80) != 0;
        int bank = lo >> 1;
        cart.mapper = 1;
        mapper_reset();
        mapper.mmc1_ctrl = (uint8_t)(0x0C | (horizontal ? 3 : 2));
        mapper.mmc1_chr0 = (uint8_t)(bank & 0x10);                 /* bit 4: the half of the ROM */
        mapper.mmc1_chr1 = (uint8_t)((bank & 0x10) | 1);
        mapper.mmc1_prg = (uint8_t)((bank & 0x0F) | (ram_on ? 0 : 0x10));
    } else return;
    mapper_update_pointers();
}

/* ---- MMC3 scanline counter: PPU A12 rising edge detection ---- */
static void mmc3_clock_irq(void) {
    sys.a12_clocks++;
    if (mapper.irq_counter == 0 || mapper.irq_reload) {
        mapper.irq_counter = mapper.irq_latch;
        mapper.irq_reload = false;
    } else {
        mapper.irq_counter--;
    }
    if (mapper.irq_counter == 0 && mapper.irq_enabled) {
        mapper.irq_asserted = true;
        cpu_raise_irq(IRQ_MAPPER, ppu.dots / 3);
    }
}

void mapper_ppu_addr(uint16_t addr) {
    if (cart.mapper != 4) return;
    if (addr & 0x1000) {
        if (mapper.a12_low) {
            if (ppu.dots - mapper.a12_low_since >= 10) mmc3_clock_irq();
            mapper.a12_low = false;
        }
    } else if (!mapper.a12_low) {
        mapper.a12_low = true;
        mapper.a12_low_since = ppu.dots;
    }
}
