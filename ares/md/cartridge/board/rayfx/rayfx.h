/* rayfx.h - Rayman CD32X's compositor on the Mega EverDrive PRO's FPGA: the reference model (C)
 *
 * The same C is the specification of the Verilog (fpga/rtl), the engine ares runs on its fpga-rayman branch, and the
 * model the game's host build checks itself against. fpga/README.md says why; this file says what, to the bit.
 *
 * The SH2s cannot write the cart; the engine sees the address of every read. In the 14 MB EDCD layout the last
 * 128 KB of every 2 MB page (window 1: cart 0x3E0000-0x3FFFFF) holds no game data (tools/ray_edpack2.py keeps it
 * free), so the engine answers there - once it is on:
 *
 *   cart 0x3E0000-0x3EF9FF   the finished picture, 320 x 200 bytes, row after row (read)
 *   cart 0x3F0000-0x3FFFFF   the command port: a 16-bit read at 0x3F0000 + 2 * w gives the engine the 15-bit word w;
 *                            the data read back is the status (RFX_STATUS_*)
 *
 * Off at power-on (and after a reset): the cart is krikzz's SSF mapper exactly, those addresses its memory. Three
 * reads of the command port's words RFX_KEY0, RFX_KEY1, RFX_KEY2, in that order and nothing else of that area
 * between, turn it on (the third read already answers with the status). RFX_OP_OFF turns it off.
 *
 * Commands: a word whose bit 14 is 1 starts a command (bits 13-10: the operation, 9-0: its first argument); the
 * words after it (bit 14 0) are its other arguments. A command runs when its last word comes. Pixels are the game's
 * 8-bit colours; 0 is "nothing drawn".
 *
 *   RFX_OP_BEGIN    (1 word)   the sprite layer cleared to 0 (the start of a frame's drawing)
 *   RFX_OP_SPRITE   (8 words)  a sprite into the layer, as Rayverse's DrawSprite*Normal(NoClip) draw (lowlevel.c):
 *                     w0 arg: bit 0 flip (DrawSpriteFlip...: the row drawn right to left), bit 1 "256" (DrawSprite...
 *                             Normal256: every byte not 0 is a pixel; otherwise a byte under 160 is a pixel and
 *                             160 + n skips n more), bits 2-9 the colour ORed into every pixel (DrawSpriteColor...)
 *                     w1 x (0-319, after the game's clipping)      w2 y (0-199)
 *                     w3 width (pixels drawn a row, 1-255)          w4 height (rows, 1-200)
 *                     w5 the sprite's row length in its image (its stride, 1-255)
 *                     w6, w7 the first row's first byte: the SH2's cart address (0-0x3FFFFF) bits 21-11, 10-0
 *   RFX_OP_BG       (5 + 2n words) the background: a picture in the cart, w x h bytes
 *                     w0 arg: n, the bands (0-15)
 *                     w1, w2 the picture's first byte, cart address bits 21-11, 10-0     w3 its width (4-2047)
 *                     w4 v: screen line y shows picture row y + v (0-1023)
 *                     then per band: picture rows below b_end show column b_f at x = 0 (rows past the last band: 0)
 *   RFX_OP_COMPOSE  (1 word)   the finished picture: for each pixel, the layer's if not 0, else the picture's
 *                     (row y + v, column ((f & ~1) + x) mod width, f the row's band's column - even, as the 32X's
 *                     line table points); without a background (no RFX_OP_BG since RFX_OP_OFF): 0
 *   RFX_OP_OFF      (1 word)   the engine off (the cart is the SSF mapper again)
 *
 * Status (the data of every command port read, once on): bit 0 busy (a command still running: the finished picture
 * is not to be read yet), bits 8-15 RFX_ID. The model is never busy; the hardware is, while it draws or composes.
 */
#ifndef RAYFX_H
#define RAYFX_H
#include <stdint.h>

#define RFX_PIC_BASE   0x3E0000u
#define RFX_PIC_END    (RFX_PIC_BASE + 320u * 200u)
#define RFX_CMD_BASE   0x3F0000u
#define RFX_CMD_END    0x400000u
#define RFX_KEY0       0x5241u        /* 'RA' */
#define RFX_KEY1       0x5946u        /* 'YF' (both 15-bit) */
#define RFX_KEY2       0x0F01u        /* version 1 */
#define RFX_ID         0xF1u
#define RFX_STATUS_BUSY 1u

#define RFX_START      0x4000u
#define RFX_OP_BEGIN   1u
#define RFX_OP_SPRITE  2u
#define RFX_OP_BG      3u
#define RFX_OP_COMPOSE 4u
#define RFX_OP_OFF     15u
#define RFX_WORD(op, arg) ((uint16_t)(RFX_START | (op) << 10 | ((arg) & 0x3FF)))

typedef struct rfx {
    int on, key;                       /* key: how many of the three key words came */
    uint16_t cmd[5 + 2 * 15];          /* the command being gathered */
    int ncmd, need;
    uint8_t layer[320 * 200];
    uint8_t pic[320 * 200];
    int bg;                            /* a background was given */
    uint32_t bg_addr; int bg_w, bg_v, bg_n;
    uint16_t b_end[15], b_f[15];
    /* the cart as the SH2 sees it (the mapper's windows): one byte at a cart address (0-0x3FFFFF) */
    uint8_t (*cart)(void* ctx, uint32_t addr);
    void* ctx;
} rfx_t;

void rfx_reset(rfx_t* fx);
/* an SH2 read of the cart at cart address addr (0-0x3FFFFF, even): 1 and *data when the engine answers it, 0 when
   the cart's memory does (the engine off, or an address outside its area) */
int rfx_read(rfx_t* fx, uint32_t addr, uint16_t* data);
#endif
