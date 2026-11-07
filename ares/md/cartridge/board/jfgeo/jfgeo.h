/* jfgeo.h - Jumping Flash CD32X: the cart's geometry engine, the reference model (C)
 *
 * The Mega EverDrive PRO's FPGA, beside krikzz's SSF mapper, does the 3D front end of a frame: it reads the
 * stage image (tools/jf_stage.py) in the cart's PSRAM, culls and sorts the objects, transforms and projects
 * their vertices, drops the triangles turned away / too small / off the screen, sorts each one's corners by
 * y and writes it, 16 bytes, into one or both lists of a frame buffer in its own SRAM.  The SH2s only fill.
 *
 * This file is the definition: the Verilog (fpga/jfgeo/rtl) must give the same bytes for the same input
 * (fpga/jfgeo/sim), and ares's model of the cart runs this code.  All arithmetic is integer and stated here.
 *
 * ---- the cart window (window 0's fourth 512 KB: cart 0x180000-0x1FFFFF; the SH2 at 0x22180000, no cache)
 *
 *   0x180000  frame buffer 0 (64 KB)        read only for the console
 *   0x1A0000  frame buffer 1
 *   0x1C0000 + 2v   a read = "the next parameter is v" (v 0-65535; the parameters below, in order)
 *   0x1E0000  a read = parameters from the start (returns the status)
 *   0x1E0002  a read = GO: one frame into the buffer parameter 15 names (returns the status)
 *   0x1E0004  status: bit 0 busy; bits 8-15 = frames done (mod 256)
 *
 * ---- the parameters (16 bits each)
 *   0, 1   the stage image's byte address in the cart's PSRAM (high, low)
 *   2-4    the camera x, y, z (s16, world units)
 *   5-13   the turn, Q15 (s16): rows x, y, z of camera = M (world - camera)
 *   14     the screen split line (list A: triangles starting above it; list B: ending below it)
 *   15     bit 0: the frame buffer to write
 *
 * ---- a frame buffer
 *   0      u16 triangles in list A, u16 in list B, u16 frame number, u16 flags (bit 0: a list was full)
 *   16     list A: JFGEO_LIST x 16 bytes
 *   16 + JFGEO_LIST x 16   list B
 *   a triangle (big-endian): s16 y0, y1, y2, x0, x1, x2 (corners sorted by y; y0 < y2), u8 palette index,
 *          u8 side (1: the edge y0 -> y2 is the left one), u16 0
 */
#ifndef JFGEO_H
#define JFGEO_H
#include <stdint.h>

#define JFGEO_LIST    2040       /* triangles a list holds (16 + 2 x 2040 x 16 <= 64 KB) */
#define JFGEO_BUF     0x10000    /* bytes a frame buffer has in the window (buffer 1 at +0x20000) */
#define JFGEO_NPARAM  16

/* the constants of the view (the SH2 renderer's, jflash/src/render.c) */
#define JFGEO_FOCAL     180
#define JFGEO_NEAR      64       /* nearer than this (full scale): not drawn */
#define JFGEO_FAR       12000
#define JFGEO_MIN_SIZE  4        /* radius under this many pixels: not drawn */
#define JFGEO_LOD_PIX   12       /* radius under this many pixels: the small version */
#define JFGEO_MIN_AREA2 2        /* twice the area > -this: dropped */

typedef struct {
    uint32_t objects, drawn_objects, vertices, triangles, written;   /* what one frame did */
    uint32_t mem_reads, mem_writes;                                  /* 16-bit accesses (for the timing) */
} JfGeoStats;

/* the cart's PSRAM: the 16-bit word at byte address a (even; big-endian as the console sees it) */
typedef uint16_t (*JfGeoRead)(void *ctx, uint32_t a);

/* one frame: p = the 16 parameters, out = the frame buffer (JFGEO_BUF bytes). 0, or -1: no image there */
int jfgeo_frame(JfGeoRead rd, void *ctx, const uint16_t *p, uint8_t *out, JfGeoStats *st);

#endif
