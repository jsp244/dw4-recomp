/*
 * sys.c - system glue: power/reset, frame loop, controllers, save states.
 */
#include "nes.h"
#include "patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

int nes_load(const char *rom_path) {
    if (cart_load(rom_path) != 0) return -1;
    nes_power();
    return 0;
}

void nes_power(void) {
    /* the stack annotations are part of the machine's state: a power-on must give the same machine every time */
    memset(stk_kind, 0, sizeof stk_kind);
    memset(stk_bank, 0, sizeof stk_bank);
    memset(ram, 0, sizeof ram);
    memset(cart.chr_ram, 0, sizeof cart.chr_ram);   /* the tiles of the last session are gone with the power */
    sys.pad_shift[0] = sys.pad_shift[1] = 0;
    sys.strobe = false;
    mapper_init();
    ppu_reset();
    apu_reset();
    cpu_power();
    sys_sync();
    patch_powered();        /* the image the game starts under */
}

void nes_reset(void) {
    /* RESET: mapper and PPU registers restart; APU keeps its timers, CPU keeps RAM */
    mapper_reset();
    ppu_reset();
    apu_write(0x4015, 0);
    apu_write(0x4017, apu.frame_reset_value);
    cpu_reset();
    sys_sync();
    patch_powered();        /* the start-up code runs again, under the image that is in now */
}

void nes_set_pad(int pad, uint8_t buttons) {
    sys.pad[pad & 1] = buttons;
}

void nes_run_frame(void) {
    sys.frames_run++;
    ppu.frame_done = false;
    while (!ppu.frame_done) {
        /* a patch switch waiting for its moment (patch.h): tried after every call and return,
           the same points whichever way the code runs */
        if (sys.xfer) { int k = sys.xfer; sys.xfer = 0; if (patch_live_wait) patch_try(k); }
        if (cpu.int_pending) cpu_take_interrupt();
        if (sys.use_recomp) {
            if (!recomp_run()) { cpu_step(); sys.fallback_steps++; }
        } else {
            cpu_step();
        }
    }
}

/* ---- the sandbox (nes.h) ---- */
static struct {
    cpu_t cpu;
    uint8_t ram[2048], wram[8192];
    mapper_t mapper;
    uint64_t instructions;
    void (*trace_hook)(void);
    uint8_t *cov;
    uint8_t xfer, stk_kind[256], stk_bank[256];
    uint64_t ret_kind[2][5];
} sandbox;

void nes_sandbox_begin(void) {
    sandbox.cpu = cpu;
    memcpy(sandbox.ram, ram, sizeof ram);
    memcpy(sandbox.wram, cart.wram, sizeof cart.wram);
    sandbox.mapper = mapper;
    sandbox.instructions = sys.instructions;
    sandbox.xfer = sys.xfer;
    memcpy(sandbox.stk_kind, stk_kind, sizeof stk_kind); memcpy(sandbox.stk_bank, stk_bank, sizeof stk_bank);
    memcpy(sandbox.ret_kind, sys.ret_kind, sizeof sys.ret_kind);
    sandbox.trace_hook = sys.trace_hook; sandbox.cov = sys.cov;
    sys.trace_hook = NULL; sys.cov = NULL;
    sys.sandbox = true;
    cpu.nmi_pending = 0; cpu.int_pending = 0; cpu.irq_line = 0; cpu.jammed = 0; cpu.irq_delay_active = 0;
}

int nes_sandbox_call(uint16_t addr, int max_steps) {
    uint8_t sp = cpu.sp;
    push16(0xFFFF);                 /* the routine's RTS lands on $0000, where no code of the game runs */
    cpu.pc = addr;
    while (max_steps-- > 0 && !cpu.jammed) {
        cpu_step();
        if (cpu.pc == 0x0000 && cpu.sp == sp) return 1;
    }
    return 0;
}

void nes_sandbox_end(void) {
    cpu = sandbox.cpu;
    memcpy(ram, sandbox.ram, sizeof ram);
    memcpy(cart.wram, sandbox.wram, sizeof cart.wram);
    mapper = sandbox.mapper;
    mapper_update_pointers();
    sys.instructions = sandbox.instructions;
    sys.xfer = sandbox.xfer;      /* (what the side run pushed and returned through is not the game's) */
    memcpy(stk_kind, sandbox.stk_kind, sizeof stk_kind); memcpy(stk_bank, sandbox.stk_bank, sizeof stk_bank);
    memcpy(sys.ret_kind, sandbox.ret_kind, sizeof sys.ret_kind);
    sys.trace_hook = sandbox.trace_hook; sys.cov = sandbox.cov;
    sys.sandbox = false;
}

/* ---- save states ----
   Version 2: the PPU frame buffer is left out (every frame rewrites all of it, and
   the frontend always runs a frame after a load) and the audio sample ring lives
   outside the APU state, so a state is about 24 KB; rewind keeps thousands. */
#define STATE_MAGIC 0x53545331u  /* "STS1" */
#define STATE_VERSION 4       /* 3: what pushed each stack slot, and the patched image the state belongs to; 4: the image the machine was powered on under */

typedef struct {
    uint32_t magic;
    uint32_t version;
    cpu_t    cpu;
    uint8_t  ram[2048];
    uint8_t  ppu[offsetof(ppu_t, fb)];   /* ppu_t up to the frame buffer; it and the HD capture come last */
    apu_t    apu;
    mapper_t mapper;
    uint8_t  wram[8192];
    uint8_t  chr_ram[8192];
    uint8_t  pad[2];
    uint8_t  pad_shift[2];
    uint8_t  strobe;
    /* since version 3 */
    uint8_t  stk_kind[256];
    uint8_t  stk_bank[256];
    uint32_t image;         /* patch_signature() of the image it was saved from */
    /* since version 4 */
    uint32_t boot_image;    /* patch_boot_image(): the image the machine was powered on under */
} state_t;

#define STATE_V2_SIZE offsetof(state_t, stk_kind)
#define STATE_V3_SIZE offsetof(state_t, boot_image)

uint8_t stk_kind[256], stk_bank[256];
#ifdef STK_AUDIT
uint32_t stk_seq[256], stk_xfers;
uint64_t stk_hand_returns, stk_hand_returns_late;
#endif

size_t nes_state_size(void) { return sizeof(state_t); }

int nes_state_save(void *buf, size_t size) {
    if (size < sizeof(state_t)) return -1;
    state_t *s = (state_t *)buf;
    memset(s, 0, sizeof *s);
    s->magic = STATE_MAGIC; s->version = STATE_VERSION;
    s->cpu = cpu; memcpy(s->ram, ram, sizeof ram);
    memcpy(s->ppu, &ppu, sizeof s->ppu); s->apu = apu; s->mapper = mapper;
    memcpy(s->wram, cart.wram, sizeof s->wram);
    memcpy(s->chr_ram, cart.chr_ram, sizeof s->chr_ram);
    s->pad[0] = sys.pad[0]; s->pad[1] = sys.pad[1];
    s->pad_shift[0] = sys.pad_shift[0]; s->pad_shift[1] = sys.pad_shift[1];
    s->strobe = sys.strobe;
    memcpy(s->stk_kind, stk_kind, sizeof stk_kind);
    memcpy(s->stk_bank, stk_bank, sizeof stk_bank);
    s->image = patch_signature();
    s->boot_image = patch_boot_image();
    return 0;
}

int nes_state_image(const void *buf, size_t size, uint32_t *image) {
    const state_t *s = (const state_t *)buf;
    if (size < STATE_V2_SIZE || s->magic != STATE_MAGIC) return 0;
    if (s->version == 2) { *image = 0; return 1; }      /* from before patches existed */
    if (s->version == 3 ? size < STATE_V3_SIZE : (s->version != STATE_VERSION || size < sizeof(state_t))) return 0;
    *image = s->image;
    return 1;
}

int nes_state_load(const void *buf, size_t size) {
    if (size < STATE_V2_SIZE) return -1;
    const state_t *s = (const state_t *)buf;
    if (s->magic != STATE_MAGIC) return -1;
    int old = s->version == 2;      /* saved before patches existed: from the unpatched game */
    int v3 = s->version == 3;       /* without the boot image: taken to be the image it was saved from */
    if (!old && (v3 ? size < STATE_V3_SIZE : (s->version != STATE_VERSION || size < sizeof(state_t)))) return -1;
    if ((old ? 0 : s->image) != patch_signature()) return -2;
    patch_set_boot_image(old ? 0 : v3 ? s->image : s->boot_image);
    if (old) memset(stk_kind, STK_UNKNOWN, sizeof stk_kind);
    else { memcpy(stk_kind, s->stk_kind, sizeof stk_kind); memcpy(stk_bank, s->stk_bank, sizeof stk_bank); }
    cpu = s->cpu; memcpy(ram, s->ram, sizeof ram);
    memcpy(&ppu, s->ppu, sizeof s->ppu); apu = s->apu; mapper = s->mapper;
    memcpy(cart.wram, s->wram, sizeof cart.wram);
    memcpy(cart.chr_ram, s->chr_ram, sizeof cart.chr_ram);
    sys.pad[0] = s->pad[0]; sys.pad[1] = s->pad[1];
    sys.pad_shift[0] = s->pad_shift[0]; sys.pad_shift[1] = s->pad_shift[1];
    sys.strobe = s->strobe != 0;
    /* the audio ring indices are restored with the APU state, so a run-ahead load
       discards exactly the samples produced after the save; a load from a file or the
       rewind history should be followed by apu_flush_samples() */
    mapper_update_pointers();
    return 0;
}
