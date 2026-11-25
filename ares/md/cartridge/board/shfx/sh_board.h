/* host/sh_board.h - Super Hang-On's arcade board without its sub CPU, as a library (shangon_ref.cpp built with
 * -DSHB_LIBRARY, Musashi beside it): what the FPGA does, for a host that runs the sub program itself - ares'
 * EverDrive PRO model, where the Genesis' 68000 runs it (docs/07, docs/08). MAME-derived parts: BSD-3-Clause. */
#ifndef SH_BOARD_H
#define SH_BOARD_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const unsigned char* main_rom;  /* 0x60000, the 68000's byte order */
    const unsigned char* sub_rom;   /* 0x60000 (the original: the main CPU reads it) */
    const unsigned char* tiles;     /* 0x18000, gfx1 */
    const unsigned char* sprites;   /* 0x100000, big-endian words */
    const unsigned char* road;      /* 0x8000 */
} shb_roms;
typedef struct { unsigned char unknown, dsw, adc[4]; } shb_inputs;   /* coin / start / supercharger (active low), DIPs, steer gas brake - */
int      shb_init(const shb_roms* r);
void     shb_line(int line, const shb_inputs* in);   /* one of the frame's 262 lines (0-261), in order */
long     shb_pictures(void);                          /* pictures made so far (one at each line 224) */
int      shb_sub_irq4(void);                          /* the sub's IRQ4 line (line 223) */
int      shb_take_sub_reset(void);                    /* the main CPU's RESET since the last call: restart the sub */
unsigned shb_sub_read(unsigned off);                  /* the sub's world, 0x60000-0x9FFFF */
void     shb_sub_write(unsigned off, unsigned data, unsigned mask);
int      shb_picture8(unsigned char* pix /* 320 x 224 */, unsigned short* pal /* 256 BGR555 */);
int      shb_take_sound(void);                        /* the next sound command (a packet's first byte), -1 none */
int      shb_take_latch(void);                        /* the sound board's bytes and resets, in order (bits 9-0 as register 0x48, bits 27-16 as 0x4A); -1 none */
#ifdef __cplusplus
}
#endif
#endif
