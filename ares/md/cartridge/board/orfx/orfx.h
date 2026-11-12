/* orfx.h - OutRun CD32X's renderer core for the Mega EverDrive PRO: its interface and its C model.

   The core draws OutRun's whole picture (Cannonball's video.cpp prepare_frame: road background, tile layers,
   road foreground, sprites, text) line by line into its own SRAM, as 8-bit 32X palette slots with the slots'
   colours beside, and the SH2s copy the finished picture into the 32X frame buffer. The SH2s send each frame's
   video state as reads of a command window (they cannot write the cart).

   This model is the definition: the RTL (fpga/orfx/rtl) and the ares model must give the same bytes. The same
   C runs in the PC host build (port/host_main.cpp checks it against Cannonball's own renderer every frame).

   Based on Cannonball (Copyright Chris White) and MAME's OutRun video (Copyright Aaron Giles): see
   cannonball/docs/license.txt. */
#ifndef ORFX_MODEL_H
#define ORFX_MODEL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the cart (EverDrive addresses, 16 MB; the SH2 sees cart A at 0x22000000 + A, cache-through) ----------- */
#define ORFX_CMD_BASE    0x380000   /* 256 KB: a read at CMD_BASE + T*0x20000 + 2*w sends the word w (A16-A1); T
                                       (A17) alternates word to word, 1 first after the key or a SYNC; the word is
                                       sent scrambled: w ^ k, k += ORFX_KEY_STEP before each word (k = 0 after the key
                                       or a SYNC), so two equal words never give the same address. The same T as
                                       the last word taken: the same word again is a read seen twice (dropped); another
                                       word means one was lost (a gap: the core ignores every word until the next SYNC
                                       and the unit fails). Taken once the address was steady 80 ns. (Not 0x300000: on the owner's console no SH2 read
                                       of cart window 6 ever reached the cart bus - run 8, FINDINGS.md) */
#define ORFX_KEY_STEP    0x9E37u
#define ORFX_SYNC        0x3D3C00   /* a read here (not a command word): a new unit - the core's command decoder
                                       starts afresh (no DATA left, nothing selected, no CHECK pending, no gap, the
                                       unit not failed; the next word's T is 1); sent before every frame */
#define ORFX_PIC_BASE    0x3C0000   /* the picture shown: 320 x 224 bytes (palette slots), then the slots' colours */
#define ORFX_PIC_PAL     0x3D2000   /* 256 words: the 32X colour (BGR555) of each slot, for the picture shown */
#define ORFX_PIC_LT      0x3D2400   /* 228 words, for the picture shown: words 0-223 each line's first word in a
                                       run-length picture (from PIC_BASE, in words), 224 its words in all, 225 its
                                       flags: bit 0 run-length (else 320 x 224 bytes), bit 1 it did not fit in
                                       ORFX_RLE_MAX words (not to be shown), bit 2 (kit 12) words 226-227 are the
                                       picture's sum: its words (224's count; 35,840 when not run-length) two at a
                                       time as 32-bit numbers, the first the high half (an odd last word: the high
                                       half alone), added up modulo 2^32 - 226 the high half, 227 the low. The
                                       slave adds up what it read: a wrong word on the way is counted and the picture
                                       read again, not shown */
#define ORFX_LT_WORDS    224
#define ORFX_LT_FLAGS    225
#define ORFX_LT_SUM      226
#define ORFX_LTF_RLE     1
#define ORFX_LTF_OVF     2
#define ORFX_LTF_SUM     4
#define ORFX_RLE_MAX     35840      /* (the picture's room; 3,600 frames of the game: 11,300 on average, 22,008 the most) */
#define ORFX_STATUS      0x3D3000   /* words: 0 'OR', 1 'FX', 2 status, 3 frames drawn, 4 the last frame's time
                                       (20 us units), 5 slots used, 6 colours without a slot, 7 CHECKs done;
                                       8-15 the core's state, for the debug report: 8 {renderer state, line},
                                       9 {executor, output unit, word fetcher, channels}, 10 queue words,
                                       11 words read twice, 12 wrong CHECKs, 13 the words taken in all,
                                       14 {selected memory, words of DATA left}, 15 the SRAM queue */
#define ORFX_KEY0        0x3D3E00   /* the key: reads of KEY0 + 2k, k = 0x0F, 0x58, 0xE1 in that order, after a
                                       console reset: until then the core is krikzz's map_ssf only */

/* the private copy of the graphics (tools/or_sdpack.py, banks 16+ = the second PSRAM, never read by the game) */
#define ORFX_GFX_SPRITES 0x800000   /* 1 MB: hwsprites' 32-bit words, big-endian (OUTRUN.PAK section 4) */
#define ORFX_GFX_TILES   0x900000   /* 256 KB: hwtiles' 32-bit words, 8 pixels x 4 bits (section 2) */
#define ORFX_GFX_ROAD    0x940000   /* 64 KB: 512 road rows x 512 pixels x 2 bits, 8 pixels a word, px 0 in 15:14 */

/* ---- the command stream (16-bit words) ---------------------------------------------------------------------- */
#define ORFX_OP_SELECT   0x1000     /* 0x1m00 | m: the next word is the address in memory m; writes go on from it */
#define ORFX_OP_DATA     0x2000     /* 0x2nnn: the next nnn words (1-4095) are data for the selected memory */
#define ORFX_OP_GO       0x3000     /* 0x300b: draw the frame into picture buffer b (0/1). + 2 (0x3002 | b): as a
                                       run-length picture - the 32X's run length mode: a word is (pixels - 1) << 8 |
                                       slot, a run ends where the slot changes, at 256 pixels and at the line's end;
                                       the lines one after the other from PIC_BASE, each line's start in ORFX_PIC_LT.
                                       A third of the words to copy (the SH2s share one bus: copying 35,840 words a
                                       frame held the game at 15 FPS, run 11) */
#define ORFX_OP_SHOW     0x4000     /* 0x400b: the console reads picture buffer b from now on (only as the first
                                       word after a SYNC: a data word out of step is never taken as one) */
#define ORFX_OP_LOAD     0x5000     /* the tile graphics from the PSRAM copy (once, at start; first after a SYNC) */
#define ORFX_OP_CHECK    0x6000     /* the next two words: the 16-bit sum, then the count, of the SELECT and DATA
                                       words, their address and data words, since the last CHECK or SYNC (GO, SHOW,
                                       LOAD, NOP not counted). A GO runs only straight after a right CHECK, in a unit
                                       that has not failed. Wrong (or a gap): the unit fails - status bit 9 until the
                                       next SYNC, the wrong-unit count +1, the GO not run. The game reads the status
                                       after each frame (orfx_verdict, port/orfx/orfx_send.cpp) and sends again */
#define ORFX_OP_NOP      0x0000
#define ORFX_PEND        256        /* a unit's writes (DATA words) at most: they wait in the core until its right
                                       CHECK, so a unit out of step writes nothing; more: the unit fails */

enum {                              /* the memories (SELECT's m) */
    ORFX_M_TILE = 0,                /* tile RAM, 0x8000 words (Cannonball's tile_ram as big-endian words) */
    ORFX_M_TEXT = 1,                /* text RAM, 0x800 words */
    ORFX_M_SPR  = 2,                /* sprite RAM (hwsprites ramBuff), 0x400 words */
    ORFX_M_ROAD = 3,                /* road RAM (hwroad ramBuff), 0x800 words */
    ORFX_M_PAL  = 4,                /* the 4096 colours, 32X BGR555 (port/video_cd32x.cpp cd32x_palette) */
    ORFX_M_REG  = 5                 /* the registers below */
};
enum {                              /* ORFX_M_REG */
    ORFX_R_ROADCTL = 0,             /* hwroad.road_control */
    ORFX_R_BANK0   = 1,             /* hwtiles tile_banks[0] */
    ORFX_R_BANK1   = 2,             /* hwtiles tile_banks[1] */
    ORFX_R_FLAGS   = 3,             /* bit 0 video enabled, bit 1 the road foreground drawn */
    ORFX_R_XCLAMP  = 4,             /* hwtiles x_clamp (192) */
    ORFX_R_COUNT   = 8
};
enum { ORFX_W = 320, ORFX_H = 224, ORFX_SLOT_FIRST = 1, ORFX_SLOT_LAST = 253 };
/* status (word 2): bit 0 busy (drawing, loading or words queued), bit 2 the key taken, bit 3 the core can draw
   run-length pictures, bit 4 the buffer shown, bit 5 kit 12 or later (every answer from a register that changes
   100 ns or more after the address; the picture's sum in the line table),
   bit 8 the queue overflowed, bit 9 the unit since the last SYNC failed, bit 10 it had a gap (a word lost: the
   core ignored the rest, so nothing wrong was written) */

typedef struct {
    uint16_t tile[0x8000], text[0x800], spr[0x400], road[0x800], pal[0x1000], reg[ORFX_R_COUNT];
    uint8_t  pic[2][ORFX_W * ORFX_H];       /* as the console reads it: slots, or a run-length picture's words */
    uint16_t lt[2][256];                    /* ORFX_PIC_LT */
    uint8_t  rawpic[ORFX_W * ORFX_H];       /* the last frame's slots (for the benches' check of a run-length one) */
    uint16_t slotpal[2][256];
    int      slots[2], overflow[2];
    /* the graphics, as the core reads them (big-endian bytes) */
    const uint8_t *gfx_sprites, *gfx_tiles, *gfx_road;
    /* the stream decoder */
    int sel_m, data_left, frames;
    int chk_pend, chk_sok, bad, gap, poison, fresh, go_ok;  /* CHECK: words of it to come (2 sum, 1 count), the sum
                                               right; the unit failed; it had a gap; ignoring words; the first word
                                               after a SYNC; a GO may run */
    uint16_t sum, cnt, n_bad, n_chk, n_dup, n_taken;
    int q_tog; uint16_t q_last, q_key;      /* the bus: the last address taken (A16-A1), its A17; the scramble */
    int shown, load_req;                    /* SHOW's buffer; a LOAD was asked (the bus side does it) */
    uint16_t* line13;                       /* GO draws its 13-bit pixels here too (the PC bench), or 0 */
    int n_pend;                             /* the unit's writes, waiting for its right CHECK (ORFX_PEND at most) */
    int8_t pend_m[256]; uint16_t pend_a[256], pend_d[256];
    uint32_t sel_a;
    /* counts for the time estimate (orfx_cycles), the last frame */
    struct { uint32_t road_words, tile_words, map_words, spr_scan, spr_lines, spr_words, spr_nibbles, spr_pixels,
             new_colours, hash_probes, spr_line_max; } n;   /* spr_line_max: the most words one sprite line read */
} orfx_t;

void     orfx_init(orfx_t* f, const uint8_t* sprites, const uint8_t* tiles, const uint8_t* road);
int      orfx_cmd(orfx_t* f, int a17, uint16_t a);            /* a read of the command window (a = A16-A1, the
                                                                 word scrambled): 0 dropped (read twice), 1 taken,
                                                                 2 a gap (the unit fails) */
void     orfx_word(orfx_t* f, uint16_t w);                    /* one word of the stream, in step */
void     orfx_sync(orfx_t* f);                                /* a read of ORFX_SYNC */
#define  ORFX_SYNC_WORD 0x10000u   /* the sender's put() gets this for a SYNC (its words are 32-bit for that) */
void     orfx_draw(orfx_t* f, int buf, uint16_t* line13);     /* the frame (buf: 0/1, + 2 run-length; line13: the
                                                                 13-bit pixels, or 0) */
uint32_t orfx_cycles(const orfx_t* f);                        /* the RTL's clocks for the last frame (estimate) */
void     orfx_road_pack(const uint8_t* roads512, uint8_t* out); /* OUTRUN.PAK's decoded road -> ORFX_GFX_ROAD */

#ifdef __cplusplus
}
#endif
#endif
