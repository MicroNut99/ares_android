/* tomba.h - the Tomba CD32X engine on the Mega EverDrive PRO's FPGA: its model (C), for ares
 *
 * MicroNut99/Tomba_CD32X fpga/tomba_core.sv (build 10, on the console 21 Nov 2026: TBTEST's frames right under the
 * slave SH2's reads) is a PS1 drawing machine beside krikzz's map_ssf: a GTE, the game's model drawing loops (its
 * "model unit"), DrawOTag's walk, the PS1 GPU's command decoder and a rasteriser that draws Tomba's frames into two
 * 8-bit pictures of 320 x 224 the SH2 copies out through a window. This file is what the console sees of it, to the
 * bit where the Verilog was checked bit for bit against the same C (Tomba_CD32X tools/m1: tgte.c, tmodel.c,
 * tgpu.c --fixed 1 --pal, and tools/m1/rtl/tb_core.cpp: frames, model draws, uploads through the console bus).
 *
 * THE WINDOW. Off at power-on and after a reset: the cart is map_ssf, those addresses its memory. Reads of the
 * command port at KEY0, KEY1, KEY2 in that order turn it on (the third read already answers with the status).
 * Then the engine answers cart 0x3E0000-0x3FFFFF:
 *   0x3E0000-0x3EFFFF  the shown picture from line W x 2048 / 320 (64 KB of its 71,680 bytes): SRAM words, the even
 *                      byte the high half (pixel x even); W = op 16's index
 *   0x3F0000 + 2w      the command port. w with bit 14 or 13 set goes into the queue (1,024 words; FULL from 768);
 *                      w = 1, 2 the read latch's low / high half; 3 words lost; 4-13 the build's diagnostics (here:
 *                      6 = words taken, the rest 0); any other w answers the status {ID, 0000, 0, shown, full, busy}
 *
 * WORDS. 01 dddddddddddddd: 13 bits into D (D = D << 13 | d, 39 bits). 10 iiiiii ooooooo: op o with index i:
 *   0  A[i] = D (A0 faces, A1 the model's ordering table, A2 vertex stream, A3 A4 the FT4 box, A5 the G4 colours)
 *   1  GTE register i = D (0-31 DR, 32-63 CR; 63 FLAG)     2  GTE operation D     8  GTE register i -> the latch
 *   3  GP0 word D (after the walk)   4  palette[D 23-16] = D 15-0   5  colour table[D 19-8] = D 7-0   6  (cache)
 *   7  shown = i bit 0   9  page table[D 24-16] = D 12-0 (bit 12 SRAM, else PSRAM; the 4 KB page number)
 *   10 i 0: a packet starts at NEXT_PRIM; 1: its next word D; 2: AddPrim(bucket D, the packet), NEXT_PRIM after it;
 *      3: NEXT_PRIM = D     11 a model draw, i = {G4 override, FT4 override, lit, mode 2}
 *   12 ClearOTagR(D 20-0, n = D 32-21); CURRENT_OT = D 20-0     13 DrawOTag(D 20-0)
 *   14 NEXT_PRIM -> the latch    15 {nodes walked, 14 x 0, a lit batch had faces, 0} -> the latch
 *   16 the window from i x 2 KB of the shown picture
 *
 * MEMORY. The PS1 RAM (the low 21 bits of an address) is 512 pages of 4 KB through the page table: SRAM page e & 0x7F
 * (words (e & 0x7F) << 11 ...) or PSRAM page e & 0xFFF (16-bit words (e & 0xFFF) << 11 ...); a 32-bit word is its low
 * halfword first. VRAM is PSRAM words 0x600000 + y x 1024 + x (byte 0xC00000 on: the second chip's second half). The
 * pictures are SRAM bytes: buffer 0 the PS1's x 384-703, buffer 1 x 704-1023, y 256-479, 320 bytes a line, buffer 1
 * from byte 0x12000. The PSRAM is the cart's 16 MB as the console sees it (16-bit words, the even byte high).
 *
 * TIME. The model does a command at once; the host holds the words back while the engine would be busy. est (50 MHz
 * clocks) is the model's estimate of what the last command took on the cart, fitted to tb_core.cpp's runs of the
 * Verilog: DrawOTag 1 a pixel + 4.5 a texel + 60 a triangle + 14 a PS1 RAM word (AREA14 / AREA00 frame 4029: +3 % / +1 %
 * of the Verilog's 23.2 / 33.3 ms); a model draw 21 a PS1 RAM word + 269 a face (1,290 draws: 0.4 %). Without the
 * console's reads cutting in (they cost the cart 2-4 % a frame at TBTEST's heaviest). The console decides. Not modelled: the build's diagnostic words 4-13 (but 6), the cache.
 *
 * BUILD 12 (ID C6, docs/15): ops 0, 10, 11, 14 (the model unit's) are gone; the PS1's CPU (tmips.c: fpga/tmips.sv with
 * EXC = 0, stopped from reset at 0x80010000) runs from the PS1 RAM through the page table, tgeo its COP2 (the console's
 * ops 1 / 2 / 8 while it is stopped; the control registers as written kept apart for CFC2 and op 8, CTC2's sign
 * extension). 17: its register i (0-31, 32 HI, 33 LO) into the latch; 18: register i = D; 19 i: 0 on after the stopped
 * instruction, 1 on at D, 2 stop, 3 empty its instruction cache, 4 / 5 / 6 the stopped instruction's address / word /
 * load-store address into the latch; 20: the PS1 RAM word at XA into the latch, XA + 4; 21: D into it, XA + 4; 22: XA = D.
 * Status bit 3: the CPU stopped. Diagnostic word 14: {stopped, why (1 SYSCALL / BREAK, 2 a GTE command tgeo lacks, 3 I/O,
 * 4 asked / reset, 5 code outside the PS1 RAM), 0}; word 4 bit 12 stopped (the host adds bits 0 / 1 / 15: the queue, a
 * command running, the walk). Its time: tmips.c's clocks, the PS1 RAM 16 clocks a word and 14 each further one (the
 * drawing's turns not counted).
 *
 * MIT licence (as krikzz's mega-ed-pub, beside which the engine sits); Tomba CD32X, 2026.
 */
#ifndef TOMBA_H
#define TOMBA_H
#include <stdint.h>
#include "tmips.h"
#ifdef __cplusplus
extern "C" {
#endif

#define TB_WIN_BASE   0x3E0000u
#define TB_CMD_BASE   0x3F0000u
#define TB_KEY0       0x4346u
#define TB_KEY1       0x5442u            /* 'TB' */
#define TB_KEY2       0x0C01u
#define TB_ID         0xC4u
#define TB_ID12       0xC6u            /* build 12: the PS1's CPU in the engine (docs/15) */
#define TB_VBASE      0x600000u          /* VRAM: PSRAM word address */
#define TB_ST_BUSY    1u
#define TB_ST_FULL    2u
#define TB_ST_SHOWN   4u
#define TB_ST_STOP    8u               /* (build 12) the CPU stopped */

typedef struct tomba_gte {
    int16_t rt[3][3];
    int32_t tr[3], ofx, ofy, dqb;
    uint16_t h;
    int16_t dqa, zsf3, zsf4;
    int16_t vx[3], vy[3], vz[3], ir[4];
    int16_t sx[3], sy[3];
    uint16_t sz[4], otz;
    int32_t mac[4];
    uint32_t flag, rgbc, rgb[3], res1;
} tomba_gte_t;

typedef struct tomba {
    /* the cart: PSRAM 16-bit words (23-bit word address), the SRAM (262,144 words, the engine's) */
    uint16_t (*psram_rd)(void* ctx, uint32_t w);
    void     (*psram_wr)(void* ctx, uint32_t w, uint16_t v);
    uint16_t* sram;
    void*    ctx;
    uint16_t key1;
    uint8_t  id;
    int      on, key, shown;
    unsigned win_off;
    /* the command machine */
    uint64_t D;
    uint32_t A[8];
    uint32_t NP, np_hi, W, P0, COT;
    uint32_t latch;
    uint16_t pt[512];
    uint16_t lost, taken;
    int      unsup;
    uint16_t nodes;
    tomba_gte_t g;
    /* the drawing */
    uint16_t pal[256];
    uint8_t  lut[4096];
    int tpx, tpy, abr, tdepth, dither_on, twmx, twmy, twox, twoy;
    int dx0, dy0, dx1, dy1, offx, offy, mask_set, mask_check;
    uint32_t fifo[16];
    int fn, need, in_line;
    int up_w, up_h, up_x, up_y;
    uint32_t up_i, up_n;
    /* the estimate */
    uint64_t est;
    uint32_t nw, ntex;                   /* PS1 RAM words moved, texels read (the estimate's) */
    uint32_t err;                        /* bit 0: a walk past 1,000,000 nodes (stopped), bit 1: an unmodelled MVMVA */
    uint32_t err_ins, err_n;             /* (the last such MVMVA, how many) */
    /* build 12 (ID C6, docs/15): the PS1's CPU (tmips.c) where the model unit was; tgeo its COP2 */
    int      b12;
    tmips_t  cpu;
    uint32_t gsh[32];                    /* the GTE's control registers as written (CFC2 and op 8 read them here) */
    uint32_t xa;                         /* ops 20-22's PS1 RAM address */
    int      clr;                        /* the last op 12's words (its time: the host keeps the next words back) */
    int      cr_last;                    /* (the first fit: the register op 17 addressed before, -1 after a go) */
    int      gte_lag, gte_last;          /* (the first fit, rbf md5 6aedb692: op 8 latches the register addressed before) */
} tomba_t;

void tomba_reset(tomba_t* t, uint16_t key1, uint8_t id);
/* a console read of the cart at byte address addr (0-0x3FFFFF): 1 and *data when the engine answers it. A word for
 * the queue is done at once (the host holds it back while busy: tomba_word below) */
int  tomba_read(tomba_t* t, uint32_t addr, uint16_t* data);
/* the queue's side, for a host that keeps words back: is word w one the queue takes; do it now (est set) */
static inline int tomba_queued(uint16_t w) { return (w & 0x6000u) != 0; }
void tomba_word(tomba_t* t, uint16_t w);
/* the status the engine gives (the host adds busy and full) */
static inline uint16_t tomba_status(const tomba_t* t) { return (uint16_t)(t->id << 8 | (t->shown ? TB_ST_SHOWN : 0) | (t->b12 && t->cpu.stop ? TB_ST_STOP : 0)); }
/* (build 12) the CPU for that many 50 MHz clocks, or until it stops; the clocks it ran */
int64_t tomba_cpu_run(tomba_t* t, int64_t clocks);
/* (build 12) does word w wait for the drawing (a GP0 word, DrawOTag: behind the walk), or go on beside it */
static inline int tomba_waits(uint16_t w) { return (w & 0x6000u) == 0x4000u && ((w & 0x7F) == 3 || (w & 0x7F) == 13); }

#ifdef __cplusplus
}
#endif
#endif
