/* z80f.h - a lean Z80 interpreter for the slave SH2 (docs/12). Ours (MIT).
 *
 * Included by snd.c after it defines the memory and port accessors as macros (so they inline):
 *   ZRB(a)    read a byte          ZWB(a, v)  write a byte
 *   ZIN(p)    read port p (8 bit)  ZOUT(p, v) write port p
 * Every documented instruction, the prefixes DD / FD (IXH / IXL too), CB, ED, DDCB / FDCB; the flags from tables
 * (S, Z, Y, H, X, P/V, N, C with the undocumented 3 and 5 as the chip makes them for the common cases); R counted;
 * the cycle counts of the Zilog manual (the timers of the sound board are counted in them). No maskable interrupt (the
 * board does not wire one): NMI only. Checked against superzazu's z80 on the sound program (host/, docs/12).
 */
#ifndef Z80F_H
#define Z80F_H
#include <stdint.h>

typedef struct {
    uint16_t af, bc, de, hl, ix, iy, sp, pc;
    uint16_t af2, bc2, de2, hl2;
    uint8_t  i, r, iff1, iff2, im, halted, nmi;
    uint32_t cyc, ninstr;
} z80f_t;

#define Z80F_A(z)  ((uint8_t)((z)->af >> 8))
#define Z80F_F(z)  ((uint8_t)(z)->af)

#endif
