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
#define ORFX_CMD_BASE    0x380000   /* 256 KB: a read at CMD_BASE + T*0x20000 + 2*w sends the word w (A1-A16); T
                                       (A17) alternates from word to word, so two equal words are two accesses */
#define ORFX_PIC_BASE    0x3C0000   /* the picture shown: 320 x 224 bytes (palette slots), then the slots' colours */
#define ORFX_PIC_PAL     0x3D2000   /* 256 words: the 32X colour (BGR555) of each slot, for the picture shown */
#define ORFX_STATUS      0x3D3000   /* words: 0 'OR', 1 'FX', 2 status, 3 frames drawn, 4 the last frame's time
                                       (20 us units), 5 slots used, 6 colours without a slot, 7 version */
#define ORFX_KEY0        0x3D3F00   /* the key: reads of KEY0 + 2k, k = 0x0F, 0x58, 0xE1 in that order, after a
                                       console reset: until then the core is krikzz's map_ssf only */

/* the private copy of the graphics (tools/or_sdpack.py, banks 16+ = the second PSRAM, never read by the game) */
#define ORFX_GFX_SPRITES 0x800000   /* 1 MB: hwsprites' 32-bit words, big-endian (OUTRUN.PAK section 4) */
#define ORFX_GFX_TILES   0x900000   /* 256 KB: hwtiles' 32-bit words, 8 pixels x 4 bits (section 2) */
#define ORFX_GFX_ROAD    0x940000   /* 64 KB: 512 road rows x 512 pixels x 2 bits, 8 pixels a word, px 0 in 15:14 */

/* ---- the command stream (16-bit words) ---------------------------------------------------------------------- */
#define ORFX_OP_SELECT   0x1000     /* 0x1m00 | m: the next word is the address in memory m; writes go on from it */
#define ORFX_OP_DATA     0x2000     /* 0x2nnn: the next nnn words (1-4095) are data for the selected memory */
#define ORFX_OP_GO       0x3000     /* 0x300b: draw the frame into picture buffer b (0/1) */
#define ORFX_OP_SHOW     0x4000     /* 0x400b: the console reads picture buffer b from now on */
#define ORFX_OP_LOAD     0x5000     /* copy the tile graphics from the PSRAM copy into the SRAM (once, at start) */
#define ORFX_OP_NOP      0x0000

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
/* status (word 2): bit 0 drawing, bit 1 loading, bit 2 the key taken, bits 4: the buffer shown, 8-15 errors */

typedef struct {
    uint16_t tile[0x8000], text[0x800], spr[0x400], road[0x800], pal[0x1000], reg[ORFX_R_COUNT];
    uint8_t  pic[2][ORFX_W * ORFX_H];
    uint16_t slotpal[2][256];
    int      slots[2], overflow[2];
    /* the graphics, as the core reads them (big-endian bytes) */
    const uint8_t *gfx_sprites, *gfx_tiles, *gfx_road;
    /* the stream decoder */
    int sel_m, data_left, frames;
    uint32_t sel_a;
    /* counts for the time estimate (orfx_cycles), the last frame */
    struct { uint32_t road_words, tile_words, map_words, spr_scan, spr_lines, spr_words, spr_nibbles, spr_pixels,
             new_colours, hash_probes; } n;
} orfx_t;

void     orfx_init(orfx_t* f, const uint8_t* sprites, const uint8_t* tiles, const uint8_t* road);
void     orfx_word(orfx_t* f, uint16_t w);                    /* one word of the stream */
void     orfx_draw(orfx_t* f, int buf, uint16_t* line13);     /* the frame (line13: the 13-bit pixels, or 0) */
uint32_t orfx_cycles(const orfx_t* f);                        /* the RTL's clocks for the last frame (estimate) */
void     orfx_road_pack(const uint8_t* roads512, uint8_t* out); /* OUTRUN.PAK's decoded road -> ORFX_GFX_ROAD */

#ifdef __cplusplus
}
#endif
#endif
