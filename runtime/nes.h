/*
 * nes.h - shared hardware state for the Dragon Warrior IV native port runtime.
 *
 * Everything the recompiled game code and the fallback interpreter need
 * lives here: CPU registers, RAM, PPU, APU, mapper and cartridge state,
 * plus the inline helpers that implement 6502 instruction semantics so
 * that both execution paths behave identically.
 *
 * No game data is stored in this repository: the PRG/CHR ROM images are
 * loaded at run time from the user's own .nes file (see cart.c).
 */
#ifndef NES_H
#define NES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Cartridge                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t *prg;           /* PRG ROM image (owned) */
    uint32_t prg_size;
    uint8_t *chr;           /* CHR ROM image (owned), or NULL when CHR RAM */
    uint32_t chr_size;
    uint8_t  chr_ram[8192];
    uint8_t  wram[8192];    /* PRG RAM (battery backed: the adventure logs) */
    int      mapper;
    int      submapper;
    bool     nes2;
    bool     battery;
    bool     four_screen;
    int      mirroring;     /* 0 = horizontal, 1 = vertical (header value) */
    uint8_t  sha1[20];      /* SHA-1 of the ROM image without the header */
    char     sha1_hex[41];
    uint8_t  md5[16];       /* MD5 of the same data, for display and cross-checking against databases */
    char     md5_hex[33];
    uint32_t crc32;         /* CRC32 of the ROM image without the header */
    char     path[1024];
} cart_t;

/* ------------------------------------------------------------------ */
/* CPU                                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  a, x, y, sp;
    uint16_t pc;
    uint8_t  c, z, i, d, v, n;   /* flags, each 0 or 1 */
    uint64_t cycles;             /* CPU cycles since power-on */
    uint8_t  nmi_pending;        /* latched NMI edge, cleared when taken */
    uint8_t  nmi_line;           /* current NMI input level */
    uint8_t  irq_line;           /* bitmask of active IRQ sources */
    uint8_t  jammed;
    uint8_t  open_bus;
    uint8_t  irq_delay_i;        /* I flag value to use for the next interrupt poll (CLI/SEI/PLP delay) */
    uint8_t  irq_delay_active;
    uint8_t  int_pending;        /* result of the last interrupt poll */
    uint64_t nmi_edge_cycle;     /* CPU cycle at which the NMI edge was detected */
    uint64_t irq_edge_cycle;     /* CPU cycle at which the IRQ line last rose */
} cpu_t;

#define IRQ_MAPPER     0x01
#define IRQ_APU_FRAME  0x02
#define IRQ_APU_DMC    0x04

/* ------------------------------------------------------------------ */
/* PPU                                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  ctrl, mask, status, oam_addr;
    uint16_t v, t;
    uint8_t  x, w;
    uint8_t  read_buffer;
    uint8_t  open_bus;
    uint8_t  oam[256];
    uint8_t  soam[256];         /* secondary OAM: 8 sprites, or up to 64 with ppu_no_sprite_limit */
    uint8_t  palette[32];
    uint8_t  vram[4096];
    uint16_t nt_offset[4];      /* nametable slot -> offset into vram */
    int      scanline;          /* 0..261 (261 = pre-render) */
    int      dot;               /* 0..340 */
    uint64_t dots;              /* total PPU dots since power-on */
    uint64_t frame;
    bool     odd_frame;
    bool     frame_done;        /* set when scanline 240 begins */
    bool     suppress_vbl;      /* $2002 read raced the flag: skip setting it this frame */
    /* background pipeline */
    uint16_t bg_shift_lo, bg_shift_hi;
    uint16_t at_shift_lo, at_shift_hi;
    uint8_t  nt_latch, at_latch, pt_lo_latch, pt_hi_latch;
    /* sprites for the line being rendered */
    uint8_t  spr_count;
    uint8_t  spr_pat_lo[64], spr_pat_hi[64], spr_attr[64], spr_x[64];
    bool     spr0_line;         /* sprite 0 is among this line's sprites */
    /* sprites evaluated for the next line */
    uint8_t  spr_count_next;
    bool     spr0_next;
    uint8_t  line_emph[240];    /* $2001 (mask) at the start of each visible line: emphasis, grayscale, enables */
    /* scroll (loopy t and fine x), control register and CHR banks at the start of the
       visible frame, for tools that re-render the frame's background */
    uint16_t frame_t;
    uint8_t  frame_x, frame_ctrl;
    uint16_t frame_chr_bank[8];
    uint8_t  fb[256 * 240];     /* 6-bit palette index per pixel */
    /* Enhanced graphics (remaster.c): with ppu_hd_capture set, the source of every pixel of each layer
       (HD_* below). Not machine state: outside the save states, like fb. */
    uint32_t hd_bg[256 * 240], hd_spr[256 * 240];
    uint32_t hd_latch, hd_tile_a, hd_tile_b;   /* the fetched background tile, the two in the shifters */
    uint8_t  hd_shifts;                        /* shifts since the last reload */
    uint32_t hd_spr_tile[64];                  /* each sprite unit of the line: tile, row, flips */
    uint8_t  hd_spr_col[64];                   /* its pixels shifted out so far */
    /* the palettes the frame was drawn with (a game may change them between lines): a snapshot whenever a line starts with a palette unlike
       the frame's earlier ones, up to four; every pixel's source names its own (HD_PALSET) */
    uint8_t  hd_palettes[4][32];
    uint8_t  hd_palette_count, hd_palette_cur;
    /* and for the 16-bit textures (sfc.c), which draw whole map squares: the name table cell of each
       pixel's background tile (HD_CELL: the low 12 bits of its address; 0 where there is no tile) */
    uint16_t hd_cell[256 * 240];
    uint16_t hd_cell_latch, hd_cell_a, hd_cell_b;
} ppu_t;

/* A pixel's source, for the enhanced graphics: the 16-byte tile (its slot in the CHR RAM; with
   CHR ROM its number in the ROM; HD_TILE_NONE where it is neither), its row and column in the tile (as stored,
   before flipping), the palette and the 2-bit pixel value, and for sprites the flips and the
   priority. 0 = no pixel from this layer. */
#define HD_TILE(i)     ((i) & 0x3FFF)
#define HD_ROW(i)      (((i) >> 14) & 7)
#define HD_COL(i)      (((i) >> 17) & 7)
#define HD_PAL(i)      (((i) >> 20) & 3)
#define HD_VALUE(i)    (((i) >> 22) & 3)
#define HD_HFLIP       (1u << 24)
#define HD_VFLIP       (1u << 25)
#define HD_BEHIND      (1u << 26)
#define HD_PRESENT     (1u << 27)
#define HD_PALSET(i)   (((i) >> 28) & 3)   /* the palette snapshot it was drawn with (ppu.hd_palettes) */
#define HD_TILE_NONE   0x3FFF
/* A background pixel's place for the 16-bit textures: a name table cell (bits 0-4 its column, 5-9
   its row, 10-11 the name table), or a square of the widescreen canvas's map around the picture
   (bits 0-9 its number, see widescreen_square; 10-11 which quarter of it). */
#define HD_CELL        0x8000u
#define HD_CELL_BAND   0x4000u
#define HD_PACK(tile, row, col, pal, value) \
    (HD_PRESENT | ((uint32_t)(tile) & 0x3FFF) | ((uint32_t)(row) << 14) | ((uint32_t)(col) << 17) | \
     ((uint32_t)(pal) << 20) | ((uint32_t)(value) << 22))

/* ------------------------------------------------------------------ */
/* APU                                                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    bool     enabled;
    uint8_t  duty;
    bool     length_halt;       /* also envelope loop */
    bool     const_volume;
    uint8_t  volume;            /* envelope period / constant volume */
    bool     env_start;
    uint8_t  env_divider, env_decay;
    bool     sweep_enabled, sweep_negate, sweep_reload;
    uint8_t  sweep_period, sweep_shift, sweep_divider;
    uint16_t timer_period, timer;
    uint8_t  seq_pos;
    uint8_t  length;
} apu_pulse_t;

typedef struct {
    bool     enabled;
    bool     control;           /* length halt / linear control */
    uint8_t  linear_load, linear;
    bool     linear_reload;
    uint16_t timer_period, timer;
    uint8_t  seq_pos;
    uint8_t  length;
} apu_tri_t;

typedef struct {
    bool     enabled;
    bool     length_halt;
    bool     const_volume;
    uint8_t  volume;
    bool     env_start;
    uint8_t  env_divider, env_decay;
    bool     mode;
    uint16_t timer_period, timer;
    uint16_t shift;
    uint8_t  length;
} apu_noise_t;

typedef struct {
    bool     enabled;
    bool     irq_enable, loop;
    uint8_t  rate_index;
    uint16_t timer_period, timer;
    uint8_t  output;
    uint16_t sample_addr, sample_len;
    uint16_t cur_addr, bytes_remaining;
    uint8_t  sample_buffer;
    bool     sample_buffer_empty;
    uint8_t  shift, bits_remaining;
    bool     silence;
    bool     irq_flag;
} apu_dmc_t;

#define APU_RING_SIZE 65536

typedef struct {
    apu_pulse_t pulse[2];
    apu_tri_t   tri;
    apu_noise_t noise;
    apu_dmc_t   dmc;
    uint64_t cycles;            /* CPU cycles processed */
    bool     odd_cycle;
    /* frame counter */
    bool     mode5;
    bool     irq_inhibit;
    bool     frame_irq;
    uint32_t frame_counter;     /* CPU cycles into the frame sequence */
    int      frame_reset_delay; /* cycles until a $4017 write takes effect, 0 = none */
    uint8_t  frame_reset_value;
    /* output */
    uint32_t sample_rate;
    uint64_t sample_phase;      /* fixed point accumulator */
    double   acc;
    uint32_t acc_n;
    double   hp90_prev_in, hp90_prev_out;
    double   hp440_prev_in, hp440_prev_out;
    double   lp_prev;
    /* the sample ring itself lives outside the struct (apu.c) so save states and rewind
       snapshots stay small; the indices are state, so a restore discards later samples */
    uint32_t ring_write, ring_read;
    uint64_t samples_produced;
} apu_t;

/* ------------------------------------------------------------------ */
/* Mapper (MMC1; MMC3 for ROM hacks that move to it; NROM for tests)   */
/* ------------------------------------------------------------------ */
typedef struct {
    /* MMC3 */
    uint8_t  bank_select;
    uint8_t  regs[8];
    uint8_t  mirroring;         /* $A000 value */
    uint8_t  wram_ctrl;         /* $A001 value */
    uint8_t  irq_latch, irq_counter;
    bool     irq_reload, irq_enabled;
    bool     irq_asserted;
    bool     a12_low;
    uint64_t a12_low_since;
    /* MMC1 */
    uint8_t  mmc1_shift, mmc1_count;
    uint8_t  mmc1_ctrl, mmc1_chr0, mmc1_chr1, mmc1_prg;
    uint8_t  mmc1_last_chr;     /* the CHR register written last (0 or 1): PRG A18 on 512 KiB boards */
    uint64_t mmc1_last_write;   /* cpu.cycles of the last register write (consecutive writes are ignored) */
    /* resolved mapping */
    uint8_t *prg_ptr[4];        /* 8 KiB slots at $8000/$A000/$C000/$E000 */
    uint8_t *chr_ptr[8];        /* 1 KiB slots at $0000..$1C00 */
    uint16_t prg_bank[4];       /* 8 KiB bank index per slot */
    uint16_t chr_bank[8];       /* 1 KiB bank index per slot */
    bool     chr_writable;
} mapper_t;

/* ------------------------------------------------------------------ */
/* System glue                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  pad[2];            /* current button state: A B Sel Start U D L R = bits 0..7 */
    uint8_t  pad_shift[2];
    bool     strobe;
    bool     use_recomp;        /* run recompiled code (with interpreter fallback) */
    uint64_t fallback_steps;    /* instructions executed by the interpreter in recomp mode */
    uint64_t native_blocks;     /* routine entries executed natively */
    uint64_t instructions;
    bool     trace;             /* print a trace line per instruction (interpreter) */
    void   (*trace_hook)(void);
    bool     coverage;
    uint8_t *cov;               /* per (bank, offset) executed flag, prg_size bytes, plus RAM region */
    void    *debug_log;         /* FILE*: when set, mapper/PPU control writes are logged */
    uint64_t a12_clocks;        /* MMC3 IRQ counter clocks observed (statistics) */
    uint64_t irq_count;         /* IRQs serviced */
    uint64_t frames_run;        /* nes_run_frame calls, run-ahead's included */
    bool     sandbox;           /* a routine of the game is being run on the side (nes_sandbox_*): no PPU/APU time, no hardware access */
    uint8_t  xfer;              /* just executed: 1 a JSR, 2 an RTS, 3 an RTI (interpreter and translated code alike) */
    uint64_t ret_kind[2][5];    /* statistics (interpreter): RTS / RTI by what had pushed the address they took (STK_*, 4 = not known) */
} sys_t;

/* What pushed the address whose low byte a stack slot holds, and the 8 KiB PRG bank that address
   was in at that moment. patch.c uses it to tell whether the game can return into code that a
   patch switch would change. */
enum { STK_DATA = 0, STK_JSR = 1, STK_BRK = 2, STK_IRQ = 3, STK_UNKNOWN = 0xFF };
extern uint8_t stk_kind[256];
extern uint8_t stk_bank[256];   /* 0xFF: not in ROM */
#ifdef STK_AUDIT
/* Audit build (interpreter): count the returns through an address pushed by hand (PHA) that had a
   call or return between the push and the return - the one case patch.c's check takes for data. */
extern uint32_t stk_seq[256], stk_xfers;
extern uint64_t stk_hand_returns, stk_hand_returns_late;
#endif

/* Global hardware state */
extern cpu_t    cpu;
extern ppu_t    ppu;
extern apu_t    apu;
extern mapper_t mapper;
extern cart_t   cart;
extern sys_t    sys;
extern uint8_t  ram[2048];

/* ------------------------------------------------------------------ */
/* Function prototypes                                                 */
/* ------------------------------------------------------------------ */
/* cart.c */
int  cart_load(const char *path);
void cart_free(void);
int  cart_load_battery(const char *path);
int  cart_save_battery(const char *path);
const char *cart_known_name(void);   /* "Dragon Warrior IV (USA)" for known dumps, else NULL */
size_t cart_battery_size(void);       /* bytes of battery RAM for the loaded cartridge */
#include <stdio.h>
FILE *nes_fopen(const char *path, const char *mode);  /* UTF-8 path aware fopen */

/* mapper.c */
void    mapper_init(void);
void    mapper_reset(void);
uint8_t mapper_read_wram(uint16_t a);
void    mapper_write_wram(uint16_t a, uint8_t v);
void    mapper_write_rom(uint16_t a, uint8_t v);
void    mapper_ppu_addr(uint16_t addr);
void    mapper_update_pointers(void);
void    mapper_set_mirroring(int mode);    /* 0=H 1=V 2=single lo 3=single hi 4=four */
uint8_t *mapper_chr_bank_ptr(uint32_t bank1k); /* data of a 1 KiB CHR bank (ROM or RAM) */
void    mapper_select_bank16(int bank);        /* the 16 KiB bank at $8000, as the game's bank switch leaves it */
int     mapper_convertible(int to, uint16_t after[4]);  /* MMC1 <-> MMC3 with the same memory map: possible now? */
void    mapper_convert(int to);

/* ppu.c */
void    ppu_reset(void);
void    ppu_run_to(uint64_t target_dots);
uint8_t ppu_reg_read(int reg);
void    ppu_reg_write(int reg, uint8_t v);
void    ppu_oam_dma(uint8_t page);
void    ppu_update_nmi(void);
extern bool ppu_no_sprite_limit;   /* option, not machine state: draw every sprite on a line instead of the hardware's 8 (no flicker) */
extern bool ppu_hd_capture;        /* option: record ppu.hd_bg / hd_spr for the enhanced graphics */
/* options, not machine state: a function that sees every APU register write, and the
   channels left out of the APU's output (bit 0 pulse 1, 1 pulse 2, 2 triangle, 3 noise) while
   apu_mute_output is on; the APU's state, and so save states, are the same either way */
extern void (*apu_write_hook)(uint16_t addr, uint8_t v);
extern uint8_t apu_mute_mask;
extern bool apu_mute_output;
uint32_t ppu_hd_tile_of(uint16_t addr);   /* the tile (CHR RAM slot) and row (bits 14-16) behind a pattern address now */

/* apu.c */
void    apu_reset(void);
void    apu_run_to(uint64_t target_cycles);
uint8_t apu_read_status(void);
void    apu_write(uint16_t a, uint8_t v);
uint32_t apu_samples_available(void);
uint32_t apu_read_samples(int16_t *out, uint32_t max);
void    apu_set_sample_rate(uint32_t hz);
void    apu_flush_samples(void);   /* drop the samples not yet read (after a state load that is not run-ahead) */

/* mem.c */
uint8_t mem_read_slow(uint16_t a);
void    mem_write_slow(uint16_t a, uint8_t v);
uint8_t nes_peek(uint16_t a);   /* side-effect-free read (RAM, PRG RAM, ROM; 0 for registers) for tools and achievements */

/* cpu.c */
void    cpu_power(void);
void    cpu_reset(void);
void    cpu_step(void);          /* interpret one instruction (interrupts checked first) */
void    cpu_take_interrupt(void);/* service a pending NMI/IRQ if any */
int     cpu_interrupt_pending(void);

/* sys.c */
int     nes_load(const char *rom_path);
void    nes_reset(void);
void    nes_power(void);
void    nes_run_frame(void);
void    nes_set_pad(int pad, uint8_t buttons);
size_t  nes_state_size(void);
int     nes_state_save(void *buf, size_t size);
int     nes_state_load(const void *buf, size_t size);   /* -1 not a state of this program, -2 saved from another patched image */
int     nes_state_image(const void *buf, size_t size, uint32_t *image);   /* 1 and the image (patch_signature) a state was saved from */
/* One of the game's own routines run on the side, to ask it something (widescreen.c): begin saves
   the CPU, the RAM, the battery RAM and the mapper; call runs the routine at addr in the
   interpreter until it returns (1) or max_steps instructions have gone by (0), with the hardware
   frozen: no PPU or APU time passes, their registers read 0 and ignore writes, and bank switches
   work; end puts everything back. Between begin and end the caller may set RAM and the mapper. */
void    nes_sandbox_begin(void);
int     nes_sandbox_call(uint16_t addr, int max_steps);
void    nes_sandbox_end(void);

/* recompiled code entry (generated/ or stubs) */
int     recomp_run(void);        /* run native code from cpu.pc; returns 0 if no native routine for cpu.pc */
extern const char *recomp_rom_sha1; /* sha1 the native code was generated from, or NULL */
/* a patched ROM: dirty has 1 for every PRG byte that differs from the dump the native code was made
   from; the routines holding such bytes are left to the interpreter. NULL = no patch. Returns how many. */
int     recomp_patch(const uint8_t *dirty, uint32_t prg_size);

/* ------------------------------------------------------------------ */
/* Inline memory access                                                */
/* ------------------------------------------------------------------ */
static inline uint8_t mem_read(uint16_t a) {
    if (a < 0x2000) return ram[a & 0x7ff];
    if (a >= 0x8000) return mapper.prg_ptr[(a >> 13) & 3][a & 0x1fff];
    return mem_read_slow(a);
}

static inline void mem_write(uint16_t a, uint8_t v) {
    if (a < 0x2000) { ram[a & 0x7ff] = v; return; }
    mem_write_slow(a, v);
}

static inline uint16_t mem_read16(uint16_t a) {
    return (uint16_t)(mem_read(a) | (mem_read((uint16_t)(a + 1)) << 8));
}

/* zero-page pointer read with wraparound inside page zero */
static inline uint16_t zp_read16(uint8_t a) {
    return (uint16_t)(ram[a] | (ram[(uint8_t)(a + 1)] << 8));
}

/* ------------------------------------------------------------------ */
/* Synchronisation: advance PPU/APU to the CPU clock and poll          */
/* interrupts. Returns nonzero when an interrupt must be serviced.     */
/* ------------------------------------------------------------------ */
static inline int sys_sync(void) {
    if (sys.sandbox) return 0;
    ppu_run_to(cpu.cycles * 3);
    apu_run_to(cpu.cycles);
    uint8_t i = cpu.i;
    if (cpu.irq_delay_active) { i = cpu.irq_delay_i; cpu.irq_delay_active = 0; }
    /* an interrupt asserted during the final cycle of an instruction is only seen by the next one */
    uint64_t last = cpu.cycles - 1;
    cpu.int_pending = (cpu.nmi_pending && cpu.nmi_edge_cycle < last) ||
                      (cpu.irq_line && !i && cpu.irq_edge_cycle < last);
    return cpu.int_pending | ppu.frame_done;
}

static inline void cpu_raise_irq(uint8_t mask, uint64_t cycle) {
    if (!cpu.irq_line) cpu.irq_edge_cycle = cycle;
    cpu.irq_line |= mask;
}

/* ------------------------------------------------------------------ */
/* Flag helpers and ALU semantics shared by interpreter + native code  */
/* ------------------------------------------------------------------ */
static inline void set_nz(uint8_t v) { cpu.z = (v == 0); cpu.n = v >> 7; }

static inline uint8_t cpu_get_p(int b) {
    return (uint8_t)((cpu.n << 7) | (cpu.v << 6) | 0x20 | (b << 4) | (cpu.d << 3) | (cpu.i << 2) | (cpu.z << 1) | cpu.c);
}

static inline void cpu_set_p(uint8_t p) {
    cpu.n = (p >> 7) & 1; cpu.v = (p >> 6) & 1; cpu.d = (p >> 3) & 1;
    cpu.i = (p >> 2) & 1; cpu.z = (p >> 1) & 1; cpu.c = p & 1;
}

static inline void push8(uint8_t v) {
    ram[0x100 | cpu.sp] = v; stk_kind[cpu.sp] = STK_DATA;
#ifdef STK_AUDIT
    stk_seq[cpu.sp] = stk_xfers;
#endif
    cpu.sp--;
}
/* a return address, by a JSR, a BRK or an interrupt */
static inline void push_ret(uint16_t v, uint8_t kind) {
    push8((uint8_t)(v >> 8)); push8((uint8_t)v);
    uint8_t i = (uint8_t)(cpu.sp + 1);
    stk_kind[i] = kind;
    stk_bank[i] = v >= 0x8000 ? (uint8_t)mapper.prg_bank[(v >> 13) & 3] : 0xFF;
}
static inline uint8_t pop8(void) { cpu.sp++; return ram[0x100 | cpu.sp]; }
static inline void push16(uint16_t v) { push8((uint8_t)(v >> 8)); push8((uint8_t)v); }
static inline uint16_t pop16(void) { uint8_t lo = pop8(); uint8_t hi = pop8(); return (uint16_t)(lo | (hi << 8)); }

static inline void op_adc(uint8_t m) {
    unsigned sum = cpu.a + m + cpu.c;
    cpu.v = ((~(cpu.a ^ m) & (cpu.a ^ sum)) >> 7) & 1;
    cpu.c = sum >> 8;
    cpu.a = (uint8_t)sum;
    set_nz(cpu.a);
}
static inline void op_sbc(uint8_t m) { op_adc((uint8_t)~m); }
static inline void op_cmp(uint8_t r, uint8_t m) { unsigned d = r - m; cpu.c = (r >= m); set_nz((uint8_t)d); }
static inline void op_bit(uint8_t m) { cpu.z = ((cpu.a & m) == 0); cpu.n = m >> 7; cpu.v = (m >> 6) & 1; }
static inline uint8_t op_asl(uint8_t v) { cpu.c = v >> 7; v = (uint8_t)(v << 1); set_nz(v); return v; }
static inline uint8_t op_lsr(uint8_t v) { cpu.c = v & 1; v >>= 1; set_nz(v); return v; }
static inline uint8_t op_rol(uint8_t v) { uint8_t c = cpu.c; cpu.c = v >> 7; v = (uint8_t)((v << 1) | c); set_nz(v); return v; }
static inline uint8_t op_ror(uint8_t v) { uint8_t c = cpu.c; cpu.c = v & 1; v = (uint8_t)((v >> 1) | (c << 7)); set_nz(v); return v; }
static inline uint8_t op_inc(uint8_t v) { v++; set_nz(v); return v; }
static inline uint8_t op_dec(uint8_t v) { v--; set_nz(v); return v; }
/* unofficial */
static inline void op_anc(uint8_t m) { cpu.a &= m; set_nz(cpu.a); cpu.c = cpu.n; }
static inline void op_alr(uint8_t m) { cpu.a &= m; cpu.a = op_lsr(cpu.a); }
static inline void op_arr(uint8_t m) {
    cpu.a &= m;
    cpu.a = (uint8_t)((cpu.a >> 1) | (cpu.c << 7));
    set_nz(cpu.a);
    cpu.c = (cpu.a >> 6) & 1;
    cpu.v = ((cpu.a >> 6) ^ (cpu.a >> 5)) & 1;
}
static inline void op_axs(uint8_t m) { unsigned t = (cpu.a & cpu.x); cpu.c = (t >= m); cpu.x = (uint8_t)(t - m); set_nz(cpu.x); }
static inline void op_xaa(uint8_t m) { cpu.a = (uint8_t)((cpu.a | 0xEE) & cpu.x & m); set_nz(cpu.a); }
static inline void op_lxa(uint8_t m) { cpu.a = cpu.x = m; set_nz(cpu.a); }
/* combined read-modify-write unofficial ops */
static inline uint8_t fn_slo(uint8_t v) { v = op_asl(v); cpu.a |= v; set_nz(cpu.a); return v; }
static inline uint8_t fn_rla(uint8_t v) { v = op_rol(v); cpu.a &= v; set_nz(cpu.a); return v; }
static inline uint8_t fn_sre(uint8_t v) { v = op_lsr(v); cpu.a ^= v; set_nz(cpu.a); return v; }
static inline uint8_t fn_rra(uint8_t v) { v = op_ror(v); op_adc(v); return v; }
static inline uint8_t fn_dcp(uint8_t v) { v--; op_cmp(cpu.a, v); return v; }
static inline uint8_t fn_isc(uint8_t v) { v++; op_sbc(v); return v; }
/* SHA/SHX/SHY/TAS: store (value & (high byte + 1)) with the page-crossing address corruption */
static inline void op_unstable_store(uint16_t b, uint8_t idx, uint8_t val) {
    uint16_t a = (uint16_t)(b + idx);
    uint8_t v = (uint8_t)(val & (uint8_t)((b >> 8) + 1));
    if ((b ^ a) & 0x100) a = (uint16_t)((v << 8) | (a & 0xff));
    mem_write(a, v);
}
static inline void op_las(uint8_t m) { cpu.a = cpu.x = cpu.sp = (uint8_t)(m & cpu.sp); set_nz(cpu.a); }

/* Interrupt entry sequence shared by interpreter and dispatcher */
static inline void cpu_do_interrupt(uint16_t vector, int brk) {
    push_ret(cpu.pc, brk ? STK_BRK : STK_IRQ);
    push8(cpu_get_p(brk));
    cpu.i = 1;
    cpu.pc = mem_read16(vector);
    cpu.cycles += 7;
}

#ifdef __cplusplus
}
#endif
#endif /* NES_H */
