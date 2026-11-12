/* rrfx.h - Road Rash CD32X's renderer core for the Mega EverDrive PRO: its interface and its C model.

   The core draws the race's whole view (horizon, terrain, road, scenery, vehicles, riders) into its own SRAM
   and gives it to the console as a picture in the 32X's run length mode; the SH2s run the game, send each
   frame's state as reads of a command window (they cannot write the cart) and copy the finished picture.
   It does what OutRun CD32X's core does (MicroNut99/CannonballCD32X, fpga/orfx), and its bus side is that
   core's as proven on the owner's console: the V6 command stream, the units with their CHECK, the key.

   This model is the definition: the RTL (fpga/rrfx/rtl) and an ares model must give the same bytes.  The
   same C runs in the PC bench (rrash/host: the model's picture against the SH2 renderer's, every frame).
   What is drawn, and how, is rrash/src/scene.c's and road.c's: this file states it again over the cart's
   memory, in integers only. */
#ifndef RRFX_MODEL_H
#define RRFX_MODEL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the cart window (cart window 7: 0x380000-0x3FFFFF; the SH2 at 0x22000000 + A, cache-through) ---------- */
#define RRFX_CMD_BASE    0x380000   /* a read at CMD_BASE + T*0x20000 + 2*a sends a word: a = w ^ k (A16-A1), k +=
                                       RRFX_KEY_STEP before each word (0 after the key or a SYNC); T (A17) alternates,
                                       1 first.  The same T and address again = the read seen twice (dropped); the
                                       same T and another address = a word was lost (a gap: the unit fails) */
#define RRFX_KEY_STEP    0x9E37u
#define RRFX_SYNC        0x3D3C00   /* a read here: a new unit */
#define RRFX_PIC_BASE    0x3C0000   /* the picture shown: run-length words, lines one after the other */
#define RRFX_PIC_LT      0x3D2400   /* words 0-175: each line's first word (from PIC_BASE, in words); 224: the words
                                       in all; 225: flags - bit 0 run-length, bit 1 it did not fit (not to be shown) */
#define RRFX_RLE_MAX     28160      /* the picture's room, in words (320 x 176 / 2) */
#define RRFX_STATUS      0x3D3000   /* words: 0 'RR', 1 'FX', 2 status, 3 frames drawn, 4 the last frame's time
                                       (20 us units), 7 CHECKs done, 11 words read twice, 12 wrong units, 13 words taken */
#define RRFX_KEY0        0x3D3E00   /* the key: reads of KEY0 + 2k, k = 0x52, 0x46, 0x58 ('R' 'F' 'X') in that order */

/* ---- the command stream (16-bit words; as orfx V6) ---------------------------------------------------------- */
#define RRFX_OP_SELECT   0x1000     /* 0x100m: the next word is the address in memory m */
#define RRFX_OP_DATA     0x2000     /* 0x2nnn: the next nnn words are data for the selected memory */
#define RRFX_OP_GO       0x3000     /* 0x300b: draw the frame into picture buffer b (only after a right CHECK) */
#define RRFX_OP_SHOW     0x4000     /* 0x400b: the console reads picture buffer b from now on (first after a SYNC) */
#define RRFX_OP_CHECK    0x6000     /* the next two words: the sum, then the count, of the unit's SELECT / DATA words */
#define RRFX_OP_NOP      0x0000
#define RRFX_PEND        256        /* a unit's writes at most: they wait for its right CHECK */

enum { RRFX_M_REG = 0, RRFX_M_THING = 1 };      /* the memories */
enum {                                          /* RRFX_M_REG: the frame (16-bit words; addresses are bytes in the
                                                   cart's 16 MB, high word then low) */
    RRFX_R_POS_H, RRFX_R_POS_L,                 /* the camera along the route, Q8 samples */
    RRFX_R_CAM_X, RRFX_R_CAM_H,                 /* across (world units), its height above the road */
    RRFX_R_SCROLL,                              /* the horizon's column at the screen's left, 0-359, even */
    RRFX_R_SAMPLES,                             /* the course's samples */
    RRFX_R_ROUTE_H, RRFX_R_ROUTE_L,             /* sample 0 of ROUTE (8 bytes each) */
    RRFX_R_SCENE_H, RRFX_R_SCENE_L,             /* sample 0 of SCENE (52 bytes each) */
    RRFX_R_OBJ_H, RRFX_R_OBJ_L,                 /* object 0 of OBJ (8 bytes each) */
    RRFX_R_IMG_H, RRFX_R_IMG_L,                 /* IMG ('RRI1': its table at + 8) */
    RRFX_R_HORIZON_H, RRFX_R_HORIZON_L,         /* the horizon picture, 360 x 160 bytes */
    RRFX_R_DEPTH_H, RRFX_R_DEPTH_L,             /* the depth table, 4,352 x 32 bits */
    RRFX_R_REMAP_H, RRFX_R_REMAP_L,             /* the riders' 15 colour tables, 256 bytes each */
    RRFX_R_COL_ROAD,                            /* road colour of odd bands << 8 | of even bands */
    RRFX_R_COL_LINE,                            /* white line << 8 | yellow */
    RRFX_R_COL_GRASS,                           /* level ground << 8 | far ground under the horizon picture */
    RRFX_R_COL_SIDE,                            /* shoulder << 8 | guard rail */
    RRFX_R_THINGS,                              /* how many of RRFX_M_THING are used */
    RRFX_R_COUNT = 32
};
/* RRFX_M_THING: what stands on the road (riders, vehicles), far to near, 8 words each:
   0 strip << 8 | flags (bit 0 mirrored, bit 1 through a rider's colour table, bits 4-7 which table)
   1 x, 2 y of its top left on the screen (s16); 3, 4 the size it is drawn (pixels); 5 its picture's width << 8 | height
   6, 7 its picture's bytes in the cart (high, low) */
#define RRFX_THINGS      32

enum { RRFX_W = 320, RRFX_H = 176, RRFX_SLICES = 33, RRFX_HORIZON = 80, RRFX_FAR = 14,
       RRFX_HZ_W = 360, RRFX_HZ_ROW0 = 10, RRFX_HZ_ROWS = 92 };
/* status (word 2): bit 0 busy, bit 2 the key taken, bit 3 run-length pictures, bit 4 the buffer shown,
   bit 9 the unit since the last SYNC failed, bit 10 it had a gap */

typedef struct {
    uint16_t reg[RRFX_R_COUNT], thing[RRFX_THINGS * 8];
    const uint8_t *mem;                         /* the cart's memory (16 MB, as the EverDrive holds it) */
    uint8_t  raw[RRFX_W * RRFX_H];              /* the frame as drawn: a palette number a pixel */
    uint8_t  pic[2][RRFX_RLE_MAX * 2];          /* as the console reads it: run-length words, big-endian */
    uint16_t lt[2][256];                        /* RRFX_PIC_LT */
    int frames, shown, drawn;                   /* frames drawn; the buffer shown; the buffer the last frame went into */
    /* the stream decoder (orfx V6) */
    int sel_m, data_left;
    int chk_pend, chk_sok, bad, gap, poison, fresh, go_ok;
    uint16_t sum, cnt, n_bad, n_chk, n_dup, n_taken;
    int q_tog; uint16_t q_last, q_key;
    int n_pend; int8_t pend_m[RRFX_PEND]; uint16_t pend_a[RRFX_PEND], pend_d[RRFX_PEND];
    uint32_t sel_a;
    struct { uint32_t pieces, runs, fill_words, pictures, picture_pixels, mem_reads, rle_words; } n;   /* the last frame's work */
} rrfx_t;

void rrfx_init(rrfx_t *f, const uint8_t *cart);
int  rrfx_cmd(rrfx_t *f, int a17, uint16_t a);  /* a read of the command window: 0 dropped (seen twice), 1 taken, 2 a gap */
void rrfx_word(rrfx_t *f, uint16_t w);          /* one word of the stream, in step */
void rrfx_sync(rrfx_t *f);
void rrfx_draw(rrfx_t *f, int buf);             /* the frame into picture buffer buf */

#ifdef __cplusplus
}
#endif
#endif
