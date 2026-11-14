/* cartfx.h - the CD32X cart compositor on the Mega EverDrive PRO's FPGA: the reference model (C)
 *
 * One engine for Hurrican CD32X, Jazz Jackrabbit CD32X REV and SuperTux CD32X REV, grown from Rayman CD32X's
 * compositor (its window, its key, its command encoding, its 16 x 8 cells).
 * This C is the specification: the Verilog (cartfx/rtl) is checked against it word for word in Verilator
 * (cartfx/sim), and the same C runs in a game's PC build and in ares. cartfx/README.md says why; this file says
 * what, to the bit.
 *
 * THE WINDOW. The SH2s cannot write the cart; the engine sees the address of every read. The last 128 KB of
 * every 2 MB page is kept free of game data by each game's packer (Rayman's rule), so once the engine is on it
 * answers there, whatever page window 1 shows:
 *
 *   cart 0x3E0000-0x3EFBFF  the shown picture: PW x PH bytes, row after row (PW = 16 x cols, PH = 8 x rows,
 *                           at most 336 x 192), only the composed cells defined
 *   cart 0x3EFC00-0x3EFC5F  the shown picture's cell map: 24 rows x 2 words; word 2r bit k = cell (k, r) for
 *                           k 0-15, word 2r + 1 bit k = cell (16 + k, r) for k 0-4 (1 = composed in that frame)
 *   cart 0x3F0000-0x3FFFFF  the command port: a 16-bit read at 0x3F0000 + 2w hands the engine the 15-bit word w
 *                           and returns the status; w = 1-15 returns a counter instead and hands nothing over
 *
 * Off at power-on and after a reset: the cart is krikzz's SSF mapper exactly, those addresses its memory. Three
 * reads of the command port's words KEY0, KEY1 (the build's), KEY2, in that order, turn it on (the third read
 * already answers with the status). OP_OFF turns it off.
 *
 * WORDS. A word whose bit 14 is 1 starts a command: bits 13-10 the operation, 9-0 its first argument. The words
 * after it are its other arguments, each 0x2000 | value (13 bits). A command runs when its last word comes, in the
 * order they came. Words 1-0x1FFF are counter reads (nothing queued). Pixels are the game's 8-bit colours.
 * Addresses in PSRAM are byte addresses in the cart's 16 MB (bank = bits 23-19: banks 0-15 the first chip, 16-30
 * the second), sent as two arguments: hi = a >> 13, lo = a & 0x1FFF. SRAM addresses are word addresses (18 bits),
 * hi = a >> 13, lo = a & 0x1FFF. Words in PSRAM are big-endian (the even byte is the high half).
 *
 *   OP_BEGIN  (1)  a new frame for the buffer not shown: no sprites, no cells marked. arg bit 0: FULL (every cell)
 *   OP_SPRITE (9)  arg bit 0 h-flip, bit 1 v-flip, bit 2 blend; w1 x, w2 y (13-bit two's complement, picture
 *                  pixels), w3 width, w4 height (1-511; 0: nothing), w5 stride (bytes a row), w6-7 the image's
 *                  first byte (PSRAM), w8 colour offset (8 bits). Pixel (i, j) of the image (byte src + j * stride
 *                  + i) lands at (x + i, y + j), mirrored by the flips (i -> w - 1 - i, j -> h - 1 - j); a byte 0
 *                  is nothing; any other byte v is drawn as (v + offset) & 255, or with blend as the byte of the
 *                  blend table at ((v + offset) & 255) * 256 + the pixel under it. The cells its clipped rectangle
 *                  touches are marked. At most 256 sprites a frame (more are dropped, the error bit set).
 *   OP_LAYER  (11) arg bits 1-0 the layer (0-3, drawn in that order), bit 2 on, bit 3 tile map (else bitmap),
 *                  bit 4 over the sprites, bit 5 opaque (its 0s drawn too; never for an over layer), bit 6 4 bits a
 *                  pixel, bit 7 map in SRAM (else PSRAM), bit 8 shade, bit 9 h-flip bit in the map;
 *                  w1-2 pixels (bitmap) / tiles (tile map) in PSRAM, w3 width, w4 height (bitmap: pixels, the
 *                  width even; tile map: cells), w5 bits 7-0 colour offset, bit 8 broken bits, w6 tile size (tile
 *                  map, 2-32 pixels), w7-8 the map (PSRAM byte or SRAM word address), w9 x, w10 y scroll
 *   OP_SCROLL (3)  arg the layer; w1 x, w2 y. Picture pixel (px, py) shows the layer's (px + x, py + y), wrapped
 *                  at its size (bitmap: width x height pixels; tile map: (width x tile) x (height x tile))
 *   OP_CELLS  (3)  arg the cell row (0-23); w1 bit k = cell k (0-12), w2 bit k = cell 13 + k (13-20): marked
 *   OP_POKE   (5)  w1-2 an SRAM word address, w3 the value's high byte, w4 its low byte: written (maps, broken bits)
 *   OP_UPLOAD (6)  w1-2 PSRAM (even), w3-4 SRAM word address, w5 n (1-8191): n words copied
 *   OP_LUT    (3)  w1-2 PSRAM: the shade table (16 x 256 bytes: light l, colour c -> byte l * 256 + c) loaded
 *   OP_SETUP  (7)  w1 cols (1-21), w2 rows (1-24), w3-4 the blend table (PSRAM, 64 KB), w5-6 the broken bits
 *                  (SRAM word address)
 *   OP_SHOW   (1)  arg bit 0: the window shows that buffer from now on
 *   OP_GO     (1)  the frame's cells composed into the buffer not shown (see below), then its cell map written
 *   OP_OFF    (1)  the engine off
 *
 * A LAYER'S PIXEL at picture (px, py): (lx, ly) = (px + x, py + y) wrapped.
 *  - bitmap: the byte (8 bits) at pixels + ly * W + lx, or the nibble (4 bits: high nibble = even lx) at
 *    pixels + (ly * W + lx) / 2.
 *  - tile map, tile size t: cell (mx, my) = (lx / t, ly / t), in it (u, v) = (lx mod t, ly mod t). Its entry e
 *    (16 bits, at map + 2 (my W + mx) in PSRAM or map + my W + mx in SRAM): tile e & 0xFFF (0: nothing there);
 *    with the h-flip bit on, e bit 12 mirrors it (u -> t - 1 - u); with broken bits on, the bit my W + mx of the
 *    broken bits (word base + n / 16, bit n % 16) set means nothing there. The pixel: the byte (or nibble) number
 *    tile x t x t + v x t + u from the tiles.
 *  - a pixel 0 is nothing (unless opaque, and not over); any other value c is drawn as (c + offset) & 255, and
 *    with shade (a tile map) as the shade table's byte (e >> 12) * 256 + that.
 *
 * GO: every marked cell (FULL: all of cols x rows), row by row, is composed afresh: 0s; the layers that are on and
 * not over, 0-3; the sprites in the order they came; the over layers, 0-3. Each is written whole into the buffer
 * not shown; then its cell map (the cells composed this frame) at 0xFC00. The window keeps showing the other
 * buffer until OP_SHOW. Cells outside cols x rows are never composed.
 *
 * Status (every command port read but counter reads): bit 0 busy (commands not all done, or memory writes not all
 * out: the picture is not to be read yet), bit 1 full (the queue is nearly full: the SH2 waits with status reads),
 * bit 2 the buffer shown, bit 3 error (a command dropped, an unknown operation, a sprite past 256, since BEGIN),
 * bits 8-15 the build's ID. Counters (w = n): 1 frames composed, 2 cells in the last frame, 3 sprites in it, 4 its
 * engine time in 64-clock units (1.28 us), 5 PSRAM accesses done again, 6 SRAM accesses done again, 7 words lost
 * (queue full), 8 KEY2 (the version), 9 the build's PSRAM read in clocks (5 or 8: psram_t below). The model is never
 * busy nor full; its counters 4-6 stay 0.
 */
#ifndef CARTFX_H
#define CARTFX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {   /* (C linkage, so a C++ host can declare it inside its own namespace: ares's board.cpp) */
#endif

#define CFX_PIC_BASE   0x3E0000u
#define CFX_MAP_OFF    0xFC00u
#define CFX_CMD_BASE   0x3F0000u
#define CFX_KEY0       0x4346u          /* 'CF' */
#define CFX_KEY2       0x0C01u          /* version 1 */
#define CFX_KEY1_HUR   0x5848u          /* 'XH' Hurrican */
#define CFX_KEY1_OJ    0x584Au          /* 'XJ' Jazz */
#define CFX_KEY1_STX   0x5853u          /* 'XS' SuperTux */
#define CFX_ID_HUR     0xC1u
#define CFX_ID_OJ      0xC2u
#define CFX_ID_STX     0xC3u

#define CFX_START      0x4000u
#define CFX_OP_BEGIN   1u
#define CFX_OP_SPRITE  2u
#define CFX_OP_LAYER   3u
#define CFX_OP_SCROLL  4u
#define CFX_OP_CELLS   5u
#define CFX_OP_POKE    6u
#define CFX_OP_UPLOAD  7u
#define CFX_OP_LUT     8u
#define CFX_OP_SETUP   9u
#define CFX_OP_SHOW    10u
#define CFX_OP_GO      11u
#define CFX_OP_OFF     15u
#define CFX_WORD(op, arg) ((uint16_t)(CFX_START | (op) << 10 | ((arg) & 0x3FF)))
#define CFX_ARG(v)        ((uint16_t)(0x2000u | ((v) & 0x1FFFu)))
#define CFX_HI(a)         CFX_ARG((uint32_t)(a) >> 13)
#define CFX_LO(a)         CFX_ARG((uint32_t)(a) & 0x1FFF)

#define CFX_ST_BUSY    1u
#define CFX_ST_FULL    2u
#define CFX_ST_SHOWN   4u
#define CFX_ST_ERR     8u

/* layer flags (OP_LAYER's argument) */
#define CFX_L_ON       (1u << 2)
#define CFX_L_TILES    (1u << 3)
#define CFX_L_OVER     (1u << 4)
#define CFX_L_OPAQUE   (1u << 5)
#define CFX_L_4BPP     (1u << 6)
#define CFX_L_MAPSRAM  (1u << 7)
#define CFX_L_SHADE    (1u << 8)
#define CFX_L_HFLIP    (1u << 9)

#define CFX_MAX_SPR    256
#define CFX_COLS       21
#define CFX_ROWS       24

typedef struct {
    uint16_t flags;                     /* OP_LAYER's argument */
    uint32_t pix, map;
    uint16_t w, h, tile;
    uint8_t  off, brk;
    uint16_t x, y;
} cfx_layer_t;

typedef struct {
    uint8_t  flags;                     /* bit 0 h-flip, 1 v-flip, 2 blend */
    int16_t  x, y;
    uint16_t w, h, stride;
    uint32_t src;
    uint8_t  off;
} cfx_sprite_t;

typedef struct cfx {
    /* the cart: PSRAM bytes (24-bit byte address), the SRAM (word address, 18 bits) */
    uint8_t  (*psram)(void* ctx, uint32_t a);
    uint16_t* sram;                     /* 262,144 words, the engine's (the model writes it) */
    void*    ctx;
    uint16_t key1;                      /* the build */
    uint8_t  id;
    int      on, key, shown, err;
    uint16_t cmd[16];
    int      ncmd, need;
    int      cols, rows;
    uint32_t blend, brk;
    cfx_layer_t layer[4];
    cfx_sprite_t spr[CFX_MAX_SPR];
    int      nspr, full;
    uint32_t mark[CFX_ROWS];            /* bit k: cell k of the row */
    uint8_t  shade[4096];
    uint16_t cnt[16];
    uint32_t est;                       /* not the engine's: what cfx_core.sv would take for the last GO, in its
                                           50 MHz clocks - an estimate for ares's busy bit, set against the
                                           Verilator runs (sim/: EST=1); the console decides */
    int      psram_t;                   /* the build's PSRAM read in clocks (cfx_core.sv's PSRAM_T: 5 or 8; a word
                                           every psram_t + 1): counter 9 and the estimate. Set by the host, kept by
                                           cfx_reset (0 there: 5) */
} cfx_t;

void cfx_reset(cfx_t* fx, uint16_t key1, uint8_t id);
/* a console read of the cart at byte address addr (0-0x3FFFFF): 1 and *data when the engine answers it */
int  cfx_read(cfx_t* fx, uint32_t addr, uint16_t* data);
/* the SRAM word of the picture byte (x, y) of buffer b, and the cell map's word */
static inline uint32_t cfx_pic_word(int b, int cols, int x, int y) { return (uint32_t)b * 0x8000u + ((uint32_t)y * 16u * (uint32_t)cols + (uint32_t)x) / 2u; }
#define CFX_MAPWORD(b, w) ((uint32_t)(b) * 0x8000u + 0x7E00u + (uint32_t)(w))

#ifdef __cplusplus
}
#endif
#endif
