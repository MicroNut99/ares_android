/* tmips.h - the PS1's CPU of the Tomba CD32X engine (fpga/tmips.sv, build 12) as C, for ares and the benches. MIT.
 *
 * What the console sees of fpga/tmips.sv with EXC = 0 (the cart): a MIPS I (the R3000A's instructions, little-endian)
 * with its branch delay slot and load delay slot (a load's, MFC0's, MFC2's / CFC2's value lands after the next
 * instruction; LWL / LWR see it at once), HI / LO, no MMU, no interrupts. It stops for the SH2 - SYSCALL / BREAK
 * (why 1), a GTE command the engine's GTE lacks (2), a load / store outside the PS1 RAM and the scratchpad (3), asked
 * to (4: and at reset), code outside the PS1 RAM (5) - with the stopped instruction not run; the SH2 reads and writes
 * its registers and sets it going after that instruction or at an address. With exc = 1 it takes the R3000A's
 * exceptions instead (the benches: an interrupt by tmips_irq).
 *
 * TIME: tmips_run(c, clocks) runs until the 50 MHz clocks are spent or it stops; each instruction costs tmips.sv's
 * clocks - 3, an instruction cache miss (16 KB, 16-byte lines) a line's fill, a PS1 RAM load or store the memory's time
 * (t->mem_clk, t->line_clk: set by the host), a scratchpad load 2, MULT / DIV their 3 / 33 in the background, a GTE
 * command the host's gte_clk. Checked against the Verilog's clocks on the same traces (tools/cpu/tc_lock.c).
 *
 * Tomba CD32X, 2026.
 */
#ifndef TMIPS_H
#define TMIPS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct tmips {
    uint32_t r[32], hi, lo, pc, npc;
    int      in_ds;                  /* the instruction at pc is in a branch's delay slot */
    int      ld_r; uint32_t ld_v;    /* the load in flight (0: none) */
    uint32_t sr, cause, epc;
    uint8_t  spad[1024];
    uint32_t itag[1024];             /* the instruction cache's tags (16 KB / 16): line address + 1, 0 empty */
    int      exc;                    /* 1: the R3000A's exceptions (the benches) */
    /* stopped */
    int      stop, why;
    uint32_t h_pc, h_ir, h_ea;
    /* time */
    int64_t  clk;                    /* clocks run (50 MHz) */
    int64_t  md_until;               /* MULT / DIV done at */
    int      mem_clk, line_clk;      /* a PS1 RAM word; a line's further words */
    /* the machine around it */
    void*    ctx;
    uint32_t (*rd32)(void* ctx, uint32_t a);                         /* the PS1 RAM, a word (a physical, word-aligned) */
    uint32_t (*fetch32)(void* ctx, uint32_t a);                      /* (the benches) instruction fetches, if not rd32 */
    void     (*wr32)(void* ctx, uint32_t a, uint32_t v, uint32_t be);  /* its byte lanes in be */
    uint32_t (*gte_rd)(void* ctx, int r);                            /* 0-31 data, 32-63 control (MFC2 / CFC2) */
    void     (*gte_wr)(void* ctx, int r, uint32_t v);
    int      (*gte_op)(void* ctx, uint32_t ins);                     /* the clocks it takes, or -1: not the engine's */
    /* the benches: an I/O load's value / an I/O store, with exc = 1 (else the CPU stops) */
    uint32_t (*io_rd)(void* ctx, uint32_t a, int size);
    void     (*io_wr)(void* ctx, uint32_t a, uint32_t v, int size);
    /* the benches: a retired instruction */
    void     (*retire)(void* ctx, uint32_t pc, uint32_t ins, uint32_t ea, uint32_t val);
} tmips_t;

void     tmips_reset(tmips_t* c, uint32_t pc);         /* stopped (why 4) at pc */
int64_t  tmips_run(tmips_t* c, int64_t clocks);       /* runs until clocks are spent or it stops; the clocks spent */
void     tmips_go(tmips_t* c, int at, uint32_t pc);    /* on again: after the stopped instruction, or at pc */
void     tmips_halt(tmips_t* c);                       /* stop at the next instruction */
void     tmips_irq(tmips_t* c);                        /* (exc = 1) an interrupt before the next instruction */
void     tmips_flush(tmips_t* c);                      /* the instruction cache emptied */
uint32_t tmips_reg(tmips_t* c, int i);                 /* 0-31, 32 HI, 33 LO (a load in flight landed first) */
void     tmips_set(tmips_t* c, int i, uint32_t v);

#ifdef __cplusplus
}
#endif
#endif
