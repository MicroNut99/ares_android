/* z80f.c - a lean Z80 interpreter for the slave SH2 (z80f.h, docs/12). Ours (MIT).
 * #included by snd.c with ZRB / ZWB / ZIN / ZOUT defined, and Z80F_HOOK(pc) (non-zero: stop before this instruction).
 * The registers live in locals while z80f_run runs (the SH-2 keeps them in its registers). */
#include "z80f.h"

enum { FC = 0x01, FN = 0x02, FP = 0x04, FX = 0x08, FH = 0x10, FY = 0x20, FZ = 0x40, FS = 0x80 };
static uint8_t z80f_sz[256], z80f_szp[256];

static void z80f_tables(void)
{
    for (int i = 0; i < 256; i++) {
        int p = 0; for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        z80f_sz[i] = (uint8_t)((i & (FS | FY | FX)) | (i ? 0 : FZ));
        z80f_szp[i] = (uint8_t)(z80f_sz[i] | (p ? 0 : FP));
    }
}
static void z80f_reset(z80f_t *z)
{
    uint32_t c = z->cyc;
    z->af = z->sp = 0xffff; z->bc = z->de = z->hl = z->ix = z->iy = 0; z->pc = 0;
    z->af2 = z->bc2 = z->de2 = z->hl2 = 0;
    z->i = z->r = z->iff1 = z->iff2 = z->im = z->halted = z->nmi = 0;
    z->cyc = c;
}

/* the cycles of the unprefixed opcodes (conditional jumps, calls, returns: not taken; taken adds below) */
static const uint8_t z80f_cc[256] = {
     4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,   8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
     7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,   7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,   5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
     5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,   5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11 };

static inline uint32_t z80f_rd(uint32_t a) { a &= 0xffff; return ZRB(a); }        /* (once: the macros may look twice) */
static inline void z80f_wr(uint32_t a, uint32_t v) { a &= 0xffff; v &= 0xff; ZWB(a, v); }
#define RD(a)       z80f_rd(a)
#define WR(a, v)    z80f_wr((a), (v))
#define FETCH()     (pc = (pc + 1) & 0xffff, RD(pc - 1))
#define FETCHW()    (pc += 2, (uint32_t)RD(pc - 2) | (uint32_t)RD(pc - 1) << 8)
#define PUSH(v)     do { uint32_t v_ = (v); sp = (sp - 1) & 0xffff; WR(sp, v_ >> 8); sp = (sp - 1) & 0xffff; WR(sp, v_); } while (0)
#define POP(v)      do { (v) = RD(sp); sp = (sp + 1) & 0xffff; (v) |= (uint32_t)RD(sp) << 8; sp = (sp + 1) & 0xffff; } while (0)
#define BC          (b << 8 | c)
#define DE          (d << 8 | e)
#define HL          (h << 8 | l)
#define SETBC(v)    do { uint32_t v_ = (v); b = (v_ >> 8) & 0xff; c = v_ & 0xff; } while (0)
#define SETDE(v)    do { uint32_t v_ = (v); d = (v_ >> 8) & 0xff; e = v_ & 0xff; } while (0)
#define SETHL(v)    do { uint32_t v_ = (v); h = (v_ >> 8) & 0xff; l = v_ & 0xff; } while (0)

/* the ALU (A in a, flags in f) */
#define ADD8(v, cy) do { uint32_t v_ = (v), r_ = a + v_ + (cy); \
    f = z80f_sz[r_ & 0xff] | ((r_ >> 8) & FC) | ((a ^ v_ ^ r_) & FH) | ((((a ^ ~v_) & (a ^ r_)) >> 5) & FP); a = r_ & 0xff; } while (0)
#define SUB8(v, cy) do { uint32_t v_ = (v), r_ = a - v_ - (cy); \
    f = z80f_sz[r_ & 0xff] | ((r_ >> 8) & FC) | FN | ((a ^ v_ ^ r_) & FH) | ((((a ^ v_) & (a ^ r_)) >> 5) & FP); a = r_ & 0xff; } while (0)
#define CP8(v)      do { uint32_t v_ = (v), r_ = a - v_; \
    f = (z80f_sz[r_ & 0xff] & ~(FY | FX)) | (v_ & (FY | FX)) | ((r_ >> 8) & FC) | FN | ((a ^ v_ ^ r_) & FH) | ((((a ^ v_) & (a ^ r_)) >> 5) & FP); } while (0)
#define AND8(v)     do { a &= (v); f = z80f_szp[a] | FH; } while (0)
#define OR8(v)      do { a = (a | (v)) & 0xff; f = z80f_szp[a]; } while (0)
#define XOR8(v)     do { a = (a ^ (v)) & 0xff; f = z80f_szp[a]; } while (0)
#define INC8(x)     do { uint32_t r_ = ((x) + 1) & 0xff; f = (f & FC) | z80f_sz[r_] | (r_ == 0x80 ? FP : 0) | ((r_ & 0x0f) ? 0 : FH); (x) = r_; } while (0)
#define DEC8(x)     do { uint32_t r_ = ((x) - 1) & 0xff; f = (f & FC) | FN | z80f_sz[r_] | (r_ == 0x7f ? FP : 0) | ((r_ & 0x0f) == 0x0f ? FH : 0); (x) = r_; } while (0)
#define ALU(op, v)  do { uint32_t w_ = (v); switch (op) { \
    case 0: ADD8(w_, 0); break; case 1: ADD8(w_, f & FC); break; case 2: SUB8(w_, 0); break; case 3: SUB8(w_, f & FC); break; \
    case 4: AND8(w_); break; case 5: XOR8(w_); break; case 6: OR8(w_); break; default: CP8(w_); break; } } while (0)
#define COND(k)     ((k) == 0 ? !(f & FZ) : (k) == 1 ? (f & FZ) : (k) == 2 ? !(f & FC) : (k) == 3 ? (f & FC) : \
                     (k) == 4 ? !(f & FP) : (k) == 5 ? (f & FP) : (k) == 6 ? !(f & FS) : (f & FS))

/* CB's shifts and rotations of v (flags set), result returned */
static uint32_t z80f_rot(uint32_t op, uint32_t v, uint32_t *pf)
{
    uint32_t r, cy, f = *pf;
    switch (op & 7) {
    case 0: cy = v >> 7; r = ((v << 1) | cy) & 0xff; break;              /* RLC */
    case 1: cy = v & 1; r = (v >> 1) | (cy << 7); break;                 /* RRC */
    case 2: cy = v >> 7; r = ((v << 1) | (f & FC)) & 0xff; break;        /* RL */
    case 3: cy = v & 1; r = (v >> 1) | ((f & FC) << 7); break;           /* RR */
    case 4: cy = v >> 7; r = (v << 1) & 0xff; break;                     /* SLA */
    case 5: cy = v & 1; r = (v >> 1) | (v & 0x80); break;                /* SRA */
    case 6: cy = v >> 7; r = ((v << 1) | 1) & 0xff; break;               /* SLL (undocumented) */
    default: cy = v & 1; r = v >> 1; break;                              /* SRL */
    }
    *pf = z80f_szp[r] | cy;
    return r;
}

/* runs until cyc reaches limit, or Z80F_HOOK(pc) says stop (checked before each instruction) */
static void z80f_run(z80f_t *z, uint32_t limit)
{
    uint32_t a = z->af >> 8, f = z->af & 0xff, b = z->bc >> 8, c = z->bc & 0xff, d = z->de >> 8, e = z->de & 0xff;
    uint32_t h = z->hl >> 8, l = z->hl & 0xff, ix = z->ix, iy = z->iy, sp = z->sp, pc = z->pc, cyc = z->cyc, r = z->r, ninst = 0;
    while ((int32_t)(cyc - limit) < 0) {
        if (z->nmi) {                                       /* NMI: to 0x0066, IFF1 off */
            z->nmi = 0; z->halted = 0; z->iff2 = z->iff1; z->iff1 = 0; r++;
            PUSH(pc); pc = 0x66; cyc += 11;
            continue;
        }
        if (z->halted) { uint32_t n = (limit - cyc + 3) >> 2; cyc += n << 2; r += n; break; }
        if (Z80F_HOOK(pc)) break;
#ifdef Z80F_TRACE
        Z80F_TRACE(pc);
#endif
        uint32_t op = FETCH(), idx = 0, xy = 0, sh = 0, sl = 0;
        r++; ninst++;
        cyc += z80f_cc[op];
    prefixed:
        switch (op) {
        /* ---- loads */
        case 0x00: break;
        case 0x01: SETBC(FETCHW()); break;
        case 0x11: SETDE(FETCHW()); break;
        case 0x21: SETHL(FETCHW()); break;
        case 0x31: sp = FETCHW(); break;
        case 0x02: WR(BC, a); break;
        case 0x12: WR(DE, a); break;
        case 0x0a: a = RD(BC); break;
        case 0x1a: a = RD(DE); break;
        case 0x22: { uint32_t n = FETCHW(); WR(n, l); WR(n + 1, h); } break;
        case 0x2a: { uint32_t n = FETCHW(); l = RD(n); h = RD(n + 1); } break;
        case 0x32: WR(FETCHW(), a); break;
        case 0x3a: a = RD(FETCHW()); break;
        case 0x06: b = FETCH(); break;  case 0x0e: c = FETCH(); break;
        case 0x16: d = FETCH(); break;  case 0x1e: e = FETCH(); break;
        case 0x26: h = FETCH(); break;  case 0x2e: l = FETCH(); break;
        case 0x3e: a = FETCH(); break;
        case 0x36: { uint32_t ad; if (idx) { ad = (HL + (int8_t)FETCH()) & 0xffff; cyc += 5; } else ad = HL; WR(ad, FETCH()); } break;
        case 0xf9: sp = HL; break;
        /* ---- 16-bit arithmetic */
        case 0x03: SETBC(BC + 1); break;  case 0x13: SETDE(DE + 1); break;
        case 0x23: SETHL(HL + 1); break;  case 0x33: sp = (sp + 1) & 0xffff; break;
        case 0x0b: SETBC(BC - 1); break;  case 0x1b: SETDE(DE - 1); break;
        case 0x2b: SETHL(HL - 1); break;  case 0x3b: sp = (sp - 1) & 0xffff; break;
        case 0x09: case 0x19: case 0x29: case 0x39: {
            uint32_t x = HL, v = op == 0x09 ? BC : op == 0x19 ? DE : op == 0x29 ? HL : sp, s = x + v;
            f = (f & (FS | FZ | FP)) | ((s >> 16) & FC) | (((x ^ v ^ s) >> 8) & FH) | ((s >> 8) & (FY | FX));
            SETHL(s);
        } break;
        /* ---- 8-bit inc / dec */
        case 0x04: INC8(b); break; case 0x0c: INC8(c); break; case 0x14: INC8(d); break; case 0x1c: INC8(e); break;
        case 0x24: INC8(h); break; case 0x2c: INC8(l); break; case 0x3c: INC8(a); break;
        case 0x05: DEC8(b); break; case 0x0d: DEC8(c); break; case 0x15: DEC8(d); break; case 0x1d: DEC8(e); break;
        case 0x25: DEC8(h); break; case 0x2d: DEC8(l); break; case 0x3d: DEC8(a); break;
        case 0x34: case 0x35: {
            uint32_t ad, v;
            if (idx) { ad = (HL + (int8_t)FETCH()) & 0xffff; cyc += 8; } else ad = HL;
            v = RD(ad); if (op == 0x34) INC8(v); else DEC8(v); WR(ad, v);
        } break;
        /* ---- rotations of A, flags */
        case 0x07: { uint32_t cy = a >> 7; a = ((a << 1) | cy) & 0xff; f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | cy; } break;
        case 0x0f: { uint32_t cy = a & 1; a = (a >> 1) | (cy << 7); f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | cy; } break;
        case 0x17: { uint32_t cy = a >> 7; a = ((a << 1) | (f & FC)) & 0xff; f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | cy; } break;
        case 0x1f: { uint32_t cy = a & 1; a = (a >> 1) | ((f & FC) << 7); f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | cy; } break;
        case 0x27: {                                        /* DAA */
            uint32_t cor = 0, cy = f & FC, hf;
            if ((f & FH) || (a & 0x0f) > 9) cor = 0x06;
            if (cy || a > 0x99) { cor |= 0x60; cy = FC; }
            if (f & FN) { hf = (f & FH) && (a & 0x0f) < 6 ? FH : 0; a = (a - cor) & 0xff; }
            else { hf = (a & 0x0f) > 9 ? FH : 0; a = (a + cor) & 0xff; }
            f = (f & FN) | z80f_szp[a] | hf | cy;
        } break;
        case 0x2f: a ^= 0xff; f = (f & (FS | FZ | FP | FC)) | FH | FN | (a & (FY | FX)); break;
        case 0x37: f = (f & (FS | FZ | FP)) | FC | (a & (FY | FX)); break;
        case 0x3f: f = ((f & (FS | FZ | FP | FC)) | ((f & FC) << 4) | (a & (FY | FX))) ^ FC; break;
        /* ---- exchanges */
        case 0x08: { uint32_t t = a << 8 | f; a = z->af2 >> 8; f = z->af2 & 0xff; z->af2 = (uint16_t)t; } break;
        case 0xd9: {
            uint32_t t;
            t = BC; SETBC(z->bc2); z->bc2 = (uint16_t)t;
            t = DE; SETDE(z->de2); z->de2 = (uint16_t)t;
            if (idx) { t = sh << 8 | sl; sh = z->hl2 >> 8; sl = z->hl2 & 0xff; z->hl2 = (uint16_t)t; }
            else { t = HL; SETHL(z->hl2); z->hl2 = (uint16_t)t; }
        } break;
        case 0xeb: {
            uint32_t t = DE;
            if (idx) { SETDE(sh << 8 | sl); sh = t >> 8; sl = t & 0xff; }
            else { SETDE(HL); SETHL(t); }
        } break;
        case 0xe3: { uint32_t lo = RD(sp), hi = RD(sp + 1); WR(sp, l); WR(sp + 1, h); l = lo; h = hi; } break;
        /* ---- jumps, calls, returns */
        case 0x10: { int32_t o = (int8_t)FETCH(); b = (b - 1) & 0xff; if (b) { pc = (pc + o) & 0xffff; cyc += 5; } } break;
        case 0x18: { int32_t o = (int8_t)FETCH(); pc = (pc + o) & 0xffff; } break;
        case 0x20: case 0x28: case 0x30: case 0x38: {
            int32_t o = (int8_t)FETCH();
            if (COND((op >> 3) & 3)) { pc = (pc + o) & 0xffff; cyc += 5; }
        } break;
        case 0xc3: pc = FETCHW(); break;
        case 0xc2: case 0xca: case 0xd2: case 0xda: case 0xe2: case 0xea: case 0xf2: case 0xfa: {
            uint32_t n = FETCHW(); if (COND((op >> 3) & 7)) pc = n;
        } break;
        case 0xe9: pc = HL; break;
        case 0xcd: { uint32_t n = FETCHW(); PUSH(pc); pc = n; } break;
        case 0xc4: case 0xcc: case 0xd4: case 0xdc: case 0xe4: case 0xec: case 0xf4: case 0xfc: {
            uint32_t n = FETCHW(); if (COND((op >> 3) & 7)) { PUSH(pc); pc = n; cyc += 7; }
        } break;
        case 0xc9: POP(pc); break;
        case 0xc0: case 0xc8: case 0xd0: case 0xd8: case 0xe0: case 0xe8: case 0xf0: case 0xf8:
            if (COND((op >> 3) & 7)) { POP(pc); cyc += 6; }
            break;
        case 0xc7: case 0xcf: case 0xd7: case 0xdf: case 0xe7: case 0xef: case 0xf7: case 0xff: PUSH(pc); pc = op & 0x38; break;
        /* ---- the stack */
        case 0xc5: PUSH(BC); break; case 0xd5: PUSH(DE); break; case 0xe5: PUSH(HL); break; case 0xf5: PUSH(a << 8 | f); break;
        case 0xc1: { uint32_t v; POP(v); SETBC(v); } break;
        case 0xd1: { uint32_t v; POP(v); SETDE(v); } break;
        case 0xe1: { uint32_t v; POP(v); SETHL(v); } break;
        case 0xf1: { uint32_t v; POP(v); a = v >> 8; f = v & 0xff; } break;
        /* ---- ports, interrupts, halt */
        case 0xd3: { uint32_t n = FETCH(); ZOUT(n, a); } break;
        case 0xdb: { uint32_t n = FETCH(); a = ZIN(n); } break;
        case 0xf3: z->iff1 = z->iff2 = 0; break;
        case 0xfb: z->iff1 = z->iff2 = 1; break;
        case 0x76: z->halted = 1; break;
        /* ---- the 8-bit ALU with an immediate */
        case 0xc6: case 0xce: case 0xd6: case 0xde: case 0xe6: case 0xee: case 0xf6: case 0xfe: ALU((op >> 3) & 7, FETCH()); break;
        /* ---- CB */
        case 0xcb: {
            uint32_t ad = 0, v, o2, rg;
            if (idx) { ad = (HL + (int8_t)FETCH()) & 0xffff; o2 = FETCH(); rg = 6; cyc += 19; }   /* (DDCB: 23) */
            else { o2 = FETCH(); r++; rg = o2 & 7; if (rg == 6) { ad = HL; cyc += 15; } else cyc += 8; }
            if (idx || rg == 6) v = RD(ad);
            else v = rg == 0 ? b : rg == 1 ? c : rg == 2 ? d : rg == 3 ? e : rg == 4 ? h : rg == 5 ? l : a;
            const uint32_t bit = (o2 >> 3) & 7;
            switch (o2 >> 6) {
            case 0: v = z80f_rot(o2, v, &f); break;
            case 1: f = (f & FC) | FH | (z80f_sz[v & (1u << bit)] & ~(FY | FX)) | ((v & (1u << bit)) ? 0 : FP) | (v & (FY | FX));
                    if (idx || rg == 6) cyc -= 3;   /* (BIT n,(HL) 12, (IX+d) 20) */
                    goto cb_done;
            case 2: v &= ~(1u << bit); break;
            default: v |= 1u << bit; break;
            }
            if (idx || rg == 6) WR(ad, v);
            if (!(idx && (o2 & 7) == 6)) {
                switch (o2 & 7) { case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
                                  case 4: if (idx) sh = v; else h = v; break; case 5: if (idx) sl = v; else l = v; break;
                                  case 7: a = v; break; default: break; }
            }
        cb_done: ;
        } break;
        /* ---- ED */
        case 0xed: {
            const uint32_t o2 = FETCH(); r++;
            switch (o2) {
            case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x70: case 0x78: {
                uint32_t v = ZIN(c); f = (f & FC) | z80f_szp[v]; cyc += 12;
                switch ((o2 >> 3) & 7) { case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
                                         case 4: h = v; break; case 5: l = v; break; case 7: a = v; break; default: break; }
            } break;
            case 0x41: case 0x49: case 0x51: case 0x59: case 0x61: case 0x69: case 0x71: case 0x79: {
                const uint32_t k = (o2 >> 3) & 7;
                ZOUT(c, k == 0 ? b : k == 1 ? c : k == 2 ? d : k == 3 ? e : k == 4 ? h : k == 5 ? l : k == 7 ? a : 0); cyc += 12;
            } break;
            case 0x42: case 0x52: case 0x62: case 0x72: case 0x4a: case 0x5a: case 0x6a: case 0x7a: {
                const uint32_t k = (o2 >> 4) & 3, v = k == 0 ? BC : k == 1 ? DE : k == 2 ? HL : sp, x = HL, cy = f & FC;
                uint32_t s;
                if (o2 & 8) { s = x + v + cy; f = ((s >> 16) & FC) | (((x ^ v ^ s) >> 8) & FH) | ((((x ^ ~v) & (x ^ s)) >> 13) & FP); }
                else { s = x - v - cy; f = FN | ((s >> 16) & FC) | (((x ^ v ^ s) >> 8) & FH) | ((((x ^ v) & (x ^ s)) >> 13) & FP); }
                s &= 0xffff; f |= ((s >> 8) & (FS | FY | FX)) | (s ? 0 : FZ);
                SETHL(s); cyc += 15;
            } break;
            case 0x43: case 0x53: case 0x63: case 0x73: {
                const uint32_t k = (o2 >> 4) & 3, v = k == 0 ? BC : k == 1 ? DE : k == 2 ? HL : sp, n = FETCHW();
                WR(n, v); WR(n + 1, v >> 8); cyc += 20;
            } break;
            case 0x4b: case 0x5b: case 0x6b: case 0x7b: {
                const uint32_t k = (o2 >> 4) & 3, n = FETCHW(), v = RD(n) | (uint32_t)RD(n + 1) << 8;
                if (k == 0) SETBC(v); else if (k == 1) SETDE(v); else if (k == 2) SETHL(v); else sp = v;
                cyc += 20;
            } break;
            case 0x44: case 0x4c: case 0x54: case 0x5c: case 0x64: case 0x6c: case 0x74: case 0x7c: {
                const uint32_t v = a; a = 0; SUB8(v, 0); cyc += 8;
            } break;
            case 0x45: case 0x4d: case 0x55: case 0x5d: case 0x65: case 0x6d: case 0x75: case 0x7d:
                POP(pc); z->iff1 = z->iff2; cyc += 14; break;
            case 0x46: case 0x4e: case 0x66: case 0x6e: z->im = 0; cyc += 8; break;
            case 0x56: case 0x76: z->im = 1; cyc += 8; break;
            case 0x5e: case 0x7e: z->im = 2; cyc += 8; break;
            case 0x47: z->i = (uint8_t)a; cyc += 9; break;
            case 0x4f: r = a; cyc += 9; break;
            case 0x57: a = z->i; f = (f & FC) | z80f_sz[a] | (z->iff2 ? FP : 0); cyc += 9; break;
            case 0x5f: a = (r & 0x7f) | (z->r & 0x80); f = (f & FC) | z80f_sz[a] | (z->iff2 ? FP : 0); cyc += 9; break;
            case 0x67: { uint32_t m = RD(HL); WR(HL, ((a << 4) | (m >> 4)) & 0xff); a = (a & 0xf0) | (m & 0x0f);
                         f = (f & FC) | z80f_szp[a]; cyc += 18; } break;                                   /* RRD */
            case 0x6f: { uint32_t m = RD(HL); WR(HL, ((m << 4) | (a & 0x0f)) & 0xff); a = (a & 0xf0) | (m >> 4);
                         f = (f & FC) | z80f_szp[a]; cyc += 18; } break;                                   /* RLD */
            case 0xa0: case 0xa8: case 0xb0: case 0xb8: {   /* LDI LDD LDIR LDDR */
                const int32_t dir = (o2 & 8) ? -1 : 1;
                uint32_t v = RD(HL); WR(DE, v);
                SETHL(HL + dir); SETDE(DE + dir); SETBC(BC - 1);
                v += a; f = (f & (FS | FZ | FC)) | (v & FX) | ((v << 4) & FY) | (BC ? FP : 0);
                cyc += 16;
                if ((o2 & 0x10) && BC) { pc = (pc - 2) & 0xffff; cyc += 5; }
            } break;
            case 0xa1: case 0xa9: case 0xb1: case 0xb9: {   /* CPI CPD CPIR CPDR */
                const int32_t dir = (o2 & 8) ? -1 : 1;
                const uint32_t v = RD(HL), s = (a - v) & 0xff;
                const uint32_t hf = (a ^ v ^ s) & FH;
                SETHL(HL + dir); SETBC(BC - 1);
                uint32_t n = s - (hf ? 1 : 0);
                f = (f & FC) | FN | (z80f_sz[s] & (FS | FZ)) | hf | (BC ? FP : 0) | (n & FX) | ((n << 4) & FY);
                cyc += 16;
                if ((o2 & 0x10) && BC && s) { pc = (pc - 2) & 0xffff; cyc += 5; }
            } break;
            case 0xa2: case 0xaa: case 0xb2: case 0xba: {   /* INI IND INIR INDR */
                const int32_t dir = (o2 & 8) ? -1 : 1;
                WR(HL, ZIN(c)); SETHL(HL + dir); b = (b - 1) & 0xff;
                f = (f & FC) | FN | z80f_sz[b]; cyc += 16;
                if ((o2 & 0x10) && b) { pc = (pc - 2) & 0xffff; cyc += 5; }
            } break;
            case 0xa3: case 0xab: case 0xb3: case 0xbb: {   /* OUTI OUTD OTIR OTDR */
                const int32_t dir = (o2 & 8) ? -1 : 1;
                const uint32_t v = RD(HL); b = (b - 1) & 0xff; ZOUT(c, v); SETHL(HL + dir);
                f = (f & FC) | FN | z80f_sz[b]; cyc += 16;
                if ((o2 & 0x10) && b) { pc = (pc - 2) & 0xffff; cyc += 5; }
            } break;
            default: cyc += 8; break;                        /* (a NOP) */
            }
        } break;
        /* ---- DD / FD: the next instruction with IX / IY as HL */
        case 0xdd: case 0xfd: {
            const uint32_t o2 = FETCH(); r++;
            if (o2 == 0xdd || o2 == 0xfd || o2 == 0xed) { pc = (pc - 1) & 0xffff; cyc += 4; break; }   /* (the prefix alone) */
            idx = op == 0xdd ? 1 : 2; xy = idx == 1 ? ix : iy;
            sh = h; sl = l; h = xy >> 8; l = xy & 0xff;
            op = o2; cyc += 4 + (o2 == 0xcb ? 0 : z80f_cc[o2]);
            if (o2 == 0x66 || o2 == 0x6e || o2 == 0x74 || o2 == 0x75 || o2 == 0x46 || o2 == 0x4e || o2 == 0x56 || o2 == 0x5e ||
                o2 == 0x7e || o2 == 0x70 || o2 == 0x71 || o2 == 0x72 || o2 == 0x73 || o2 == 0x77 ||
                ((o2 & 0xc7) == 0x86)) {                      /* the (IX+d) loads, stores and ALU ops: H and L are H and L */
                const uint32_t ad = (xy + (int8_t)FETCH()) & 0xffff;
                h = sh; l = sl; cyc += 8;
                if ((o2 & 0xc0) == 0x40 && (o2 & 7) == 6) {
                    const uint32_t v = RD(ad);
                    switch ((o2 >> 3) & 7) { case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
                                             case 4: h = v; break; case 5: l = v; break; default: a = v; break; }
                } else if ((o2 & 0xf8) == 0x70) {
                    const uint32_t k = o2 & 7;
                    WR(ad, k == 0 ? b : k == 1 ? c : k == 2 ? d : k == 3 ? e : k == 4 ? h : k == 5 ? l : a);
                } else ALU((o2 >> 3) & 7, RD(ad));
                sh = h; sl = l; h = xy >> 8; l = xy & 0xff;   /* (the real H and L kept; IX / IY unchanged below) */
                break;
            }
            goto prefixed;
        }
        /* ---- the 8-bit loads and the ALU on registers (0x40-0xbf) */
        default:
            if (op >= 0x40 && op < 0x80) {
                uint32_t v; const uint32_t s = op & 7;
                if (s == 6) v = RD(HL);
                else v = s == 0 ? b : s == 1 ? c : s == 2 ? d : s == 3 ? e : s == 4 ? h : s == 5 ? l : a;
                switch ((op >> 3) & 7) {
                case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
                case 4: h = v; break; case 5: l = v; break; case 6: WR(HL, v); break; default: a = v; break;
                }
            } else if (op >= 0x80 && op < 0xc0) {
                const uint32_t s = op & 7;
                const uint32_t v = s == 6 ? RD(HL) : s == 0 ? b : s == 1 ? c : s == 2 ? d : s == 3 ? e : s == 4 ? h : s == 5 ? l : a;
                ALU((op >> 3) & 7, v);
            }
            break;
        }
        if (idx) {                                          /* IX / IY back from H and L, HL back */
            xy = h << 8 | l;
            if (idx == 1) ix = xy; else iy = xy;
            h = sh; l = sl;
        }
    }
    z->af = (uint16_t)(a << 8 | f); z->bc = (uint16_t)BC; z->de = (uint16_t)DE; z->hl = (uint16_t)HL;
    z->ix = (uint16_t)ix; z->iy = (uint16_t)iy; z->sp = (uint16_t)sp; z->pc = (uint16_t)pc; z->cyc = cyc;
    z->r = (uint8_t)((z->r & 0x80) | (r & 0x7f));
    z->ninstr += ninst;
}
