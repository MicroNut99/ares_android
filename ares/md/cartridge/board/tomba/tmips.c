/* tmips.c - fpga/tmips.sv as C (tmips.h says what it is). Tomba CD32X, 2026. MIT. */
#include "tmips.h"
#include <string.h>
#ifndef TM_FILL
#define TM_FILL 2         /* (fitted to tb_tmips's clocks: the states around a fill, a memory access) */
#endif
#ifndef TM_MEM
#define TM_MEM  2
#endif

void tmips_reset(tmips_t* c, uint32_t pc)
{
    memset(c->r, 0, sizeof c->r); c->hi = c->lo = 0;
    c->pc = pc; c->npc = pc + 4; c->in_ds = 0; c->ld_r = 0; c->ld_v = 0;
    c->sr = c->cause = c->epc = 0;
    memset(c->itag, 0, sizeof c->itag);
    c->stop = 1; c->why = 4; c->h_pc = pc; c->h_ir = 0; c->h_ea = 0;
    c->clk = 0; c->md_until = 0;
}
void tmips_flush(tmips_t* c) { memset(c->itag, 0, sizeof c->itag); c->clk += 1024; }
void tmips_halt(tmips_t* c) { if (!c->stop) { c->stop = 1; c->why = 4; c->h_pc = c->pc; } }
/* (docs/17) a load in flight at a halt (why 4: stopped before the next instruction's decode, which lands it) is NOT in
   the register file yet - tmips.sv's S_H reads and writes the file as it is, and the load lands over it after the go.
   At a BREAK, an I/O access or a GTE stop it has landed (S_D, before S_E stops). */
uint32_t tmips_reg(tmips_t* c, int i) { return i == 32 ? c->hi : i == 33 ? c->lo : i ? c->r[i & 31] : 0; }
void tmips_set(tmips_t* c, int i, uint32_t v)
{
    if (i == 32) c->hi = v; else if (i == 33) c->lo = v; else if (i > 0 && i < 32) c->r[i] = v;
}
void tmips_go(tmips_t* c, int at, uint32_t pc)
{
    if (at) { c->pc = pc; c->npc = pc + 4; } else { c->pc = c->npc; c->npc += 4; }
    c->in_ds = 0; c->stop = 0; c->clk += 3;
}

static int irq_now, ds_t;
void tmips_irq(tmips_t* c) { (void)c; irq_now = 1; }

static void exception(tmips_t* c, uint32_t at, int code, uint32_t ins, int bt)
{
    c->epc = c->in_ds ? at - 4 : at;
    c->cause = (c->in_ds ? 0x80000000u : 0) | ((c->in_ds && bt) ? 0x40000000u : 0) | ((ins >> 26 & 3u) << 28) |
               (c->cause & 0x0FFFFF00u & ~0x30000000u) | ((uint32_t)code << 2) | (code == 0 ? 0x400u : 0);
    c->sr = (c->sr & ~0x3Fu) | ((c->sr << 2) & 0x3Cu);
    c->pc = (c->sr & 0x400000u) ? 0xBFC00180u : 0x80000080u; c->npc = c->pc + 4; c->in_ds = 0;
}

static uint32_t ld_ext(uint32_t op, uint32_t a, uint32_t w, uint32_t old)
{
    const uint32_t b = (w >> (8 * (a & 3))) & 0xFF, h = (a & 2) ? w >> 16 : w & 0xFFFF;
    switch (op) {
    case 0x20: return (uint32_t)(int8_t)b;
    case 0x24: return b;
    case 0x21: return (uint32_t)(int16_t)h;
    case 0x25: return h;
    case 0x22: switch (a & 3) { case 0: return (w << 24) | (old & 0x00FFFFFF); case 1: return (w << 16) | (old & 0xFFFF);
                                case 2: return (w << 8) | (old & 0xFF); default: return w; }
    case 0x26: switch (a & 3) { case 0: return w; case 1: return (old & 0xFF000000) | (w >> 8);
                                case 2: return (old & 0xFFFF0000) | (w >> 16); default: return (old & 0xFFFFFF00) | (w >> 24); }
    default: return w;
    }
}

int64_t tmips_run(tmips_t* c, int64_t clocks)
{
    const int64_t start = c->clk, end = c->clk + clocks;
    static int64_t gte_until;
    while (!c->stop && c->clk < end) {
        const uint32_t pc = c->pc, fpa = pc & 0x1FFFFFFF;
        int bt_prev = 0;
        if (irq_now && c->exc) { irq_now = 0; exception(c, pc, 0, c->fetch32 ? c->fetch32(c->ctx, fpa & ~3u) : c->rd32(c->ctx, fpa & ~3u), ds_t); c->clk += 4; continue; }
        if (!c->exc && fpa >= 0x00800000u) { c->stop = 1; c->why = 5; c->h_pc = pc; c->h_ea = pc; break; }
        /* the fetch: the instruction cache (16 KB, 16-byte lines; kseg1 a word at a time) */
        uint32_t ins;
        if ((pc >> 29) != 5) {
            const uint32_t line = fpa >> 4, i = line & 1023;
            if (c->itag[i] != line + 1) { c->itag[i] = line + 1; c->clk += c->mem_clk + 3 * c->line_clk + TM_FILL; }
        } else c->clk += c->mem_clk;
        ins = c->fetch32 ? c->fetch32(c->ctx, fpa & ~3u) : c->rd32(c->ctx, fpa & ~3u);
        c->clk += 3;
        const uint32_t op = ins >> 26, rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, rd = (ins >> 11) & 31, sa = (ins >> 6) & 31,
                       fn = ins & 63;
        const uint32_t imm = ins & 0xFFFF, simm = (uint32_t)(int32_t)(int16_t)imm;
        const uint32_t vs = rs ? c->r[rs] : 0, vt = rt ? c->r[rt] : 0;
        const int pend_r = c->ld_r; const uint32_t pend_v = c->ld_v;
        c->ld_r = 0;
        int wr = 0; uint32_t wv = 0, ea = 0, rval = 0;
        uint32_t npc = c->npc, nnpc = c->npc + 4; int branch = 0, taken = 0;
        int newld = 0; uint32_t newld_v = 0;
        (void)bt_prev;
        switch (op) {
        case 0x00:
            switch (fn) {
            case 0x00: wr = rd; wv = vt << sa; break;
            case 0x02: wr = rd; wv = vt >> sa; break;
            case 0x03: wr = rd; wv = (uint32_t)((int32_t)vt >> sa); break;
            case 0x04: wr = rd; wv = vt << (vs & 31); break;
            case 0x06: wr = rd; wv = vt >> (vs & 31); break;
            case 0x07: wr = rd; wv = (uint32_t)((int32_t)vt >> (vs & 31)); break;
            case 0x08: branch = taken = 1; nnpc = vs; break;
            case 0x09: branch = taken = 1; nnpc = vs; wr = rd; wv = pc + 8; break;
            case 0x0C: case 0x0D:
                if (c->exc) { exception(c, pc, fn == 0x0C ? 8 : 9, ins, ds_t); c->clk += 1; if (c->retire) c->retire(c->ctx, pc, ins, 0, 0);
                              if (pend_r) c->r[pend_r] = pend_v; continue; }
                if (pend_r) c->r[pend_r] = pend_v; /* (tmips.sv: S_D lands it before S_E stops) */ c->stop = 1; c->why = 1; c->h_pc = pc; c->h_ir = ins; continue;
            case 0x10: if (c->clk < c->md_until) c->clk = c->md_until; wr = rd; wv = c->hi; break;
            case 0x11: c->hi = vs; break;
            case 0x12: if (c->clk < c->md_until) c->clk = c->md_until; wr = rd; wv = c->lo; break;
            case 0x13: c->lo = vs; break;
            case 0x18: { const int64_t p = (int64_t)(int32_t)vs * (int32_t)vt; c->lo = (uint32_t)p; c->hi = (uint32_t)((uint64_t)p >> 32); c->md_until = c->clk + 3; break; }
            case 0x19: { const uint64_t p = (uint64_t)vs * vt; c->lo = (uint32_t)p; c->hi = (uint32_t)(p >> 32); c->md_until = c->clk + 3; break; }
            case 0x1A:
                if (vt == 0) { c->lo = ((int32_t)vs < 0) ? 1 : 0xFFFFFFFF; c->hi = vs; }
                else if (vs == 0x80000000u && vt == 0xFFFFFFFFu) { c->lo = 0x80000000u; c->hi = 0; c->md_until = c->clk + 33; }
                else { c->lo = (uint32_t)((int32_t)vs / (int32_t)vt); c->hi = (uint32_t)((int32_t)vs % (int32_t)vt); c->md_until = c->clk + 33; }
                break;
            case 0x1B:
                if (vt == 0) { c->lo = 0xFFFFFFFF; c->hi = vs; } else { c->lo = vs / vt; c->hi = vs % vt; c->md_until = c->clk + 33; }
                break;
            case 0x20: case 0x21: wr = rd; wv = vs + vt; break;
            case 0x22: case 0x23: wr = rd; wv = vs - vt; break;
            case 0x24: wr = rd; wv = vs & vt; break;
            case 0x25: wr = rd; wv = vs | vt; break;
            case 0x26: wr = rd; wv = vs ^ vt; break;
            case 0x27: wr = rd; wv = ~(vs | vt); break;
            case 0x2A: wr = rd; wv = (int32_t)vs < (int32_t)vt; break;
            case 0x2B: wr = rd; wv = vs < vt; break;
            default: break;
            }
            break;
        case 0x01:
            branch = 1; taken = (rt & 1) ? !((int32_t)vs < 0) : ((int32_t)vs < 0);
            if (taken) nnpc = npc + (simm << 2);
            if (rt & 0x10) { wr = 31; wv = pc + 8; }
            break;
        case 0x02: branch = taken = 1; nnpc = (npc & 0xF0000000u) | ((ins & 0x3FFFFFF) << 2); break;
        case 0x03: branch = taken = 1; nnpc = (npc & 0xF0000000u) | ((ins & 0x3FFFFFF) << 2); wr = 31; wv = pc + 8; break;
        case 0x04: branch = 1; taken = vs == vt; if (taken) nnpc = npc + (simm << 2); break;
        case 0x05: branch = 1; taken = vs != vt; if (taken) nnpc = npc + (simm << 2); break;
        case 0x06: branch = 1; taken = (int32_t)vs <= 0; if (taken) nnpc = npc + (simm << 2); break;
        case 0x07: branch = 1; taken = (int32_t)vs > 0; if (taken) nnpc = npc + (simm << 2); break;
        case 0x08: case 0x09: wr = rt; wv = vs + simm; break;
        case 0x0A: wr = rt; wv = (int32_t)vs < (int32_t)simm; break;
        case 0x0B: wr = rt; wv = vs < simm; break;
        case 0x0C: wr = rt; wv = vs & imm; break;
        case 0x0D: wr = rt; wv = vs | imm; break;
        case 0x0E: wr = rt; wv = vs ^ imm; break;
        case 0x0F: wr = rt; wv = imm << 16; break;
        case 0x10:                                                     /* COP0 */
            if (rs == 0) { newld = rt; newld_v = rd == 12 ? c->sr : rd == 13 ? c->cause : rd == 14 ? c->epc : rd == 15 ? 2 : 0; rval = newld_v; }
            else if (rs == 4) { if (rd == 12) c->sr = vt; else if (rd == 13) c->cause = (c->cause & ~0x300u) | (vt & 0x300u); rval = vt; }
            else if ((ins & (1u << 25)) && fn == 0x10) c->sr = (c->sr & ~0xFu) | ((c->sr >> 2) & 0xFu);
            break;
        case 0x12:                                                     /* COP2 */
            if (c->clk < gte_until) c->clk = gte_until;
            if (ins & (1u << 25)) {
                const int n = c->gte_op(c->ctx, ins);
                if (n < 0 && !c->exc) { if (pend_r) c->r[pend_r] = pend_v; /* (tmips.sv: S_D lands it before S_E stops) */ c->clk -= 3; c->stop = 1; c->why = 2; c->h_pc = pc; c->h_ir = ins; continue; }
                gte_until = c->clk + (n > 0 ? n : 0);
            } else if (rs == 0 || rs == 2) { newld = rt; newld_v = c->gte_rd(c->ctx, (rs == 2 ? 32 : 0) + (int)rd); rval = newld_v; c->clk += 2; }
            else if (rs == 4 || rs == 6) { c->gte_wr(c->ctx, (rs == 6 ? 32 : 0) + (int)rd, vt); rval = vt; }
            break;
        default:
            if ((op >= 0x20 && op <= 0x26) || op == 0x32 || (op >= 0x28 && op <= 0x2B) || op == 0x2E || op == 0x3A) {
                ea = vs + simm;
                const uint32_t pa = ea & 0x1FFFFFFF;
                const int sp = pa >= 0x1F800000u && pa < 0x1F800400u, ram = pa < 0x00800000u;
                const int load = op <= 0x26 || op == 0x32;
                if ((op == 0x32 || op == 0x3A) && c->clk < gte_until) c->clk = gte_until;
                if (!sp && !ram && !c->exc) { if (pend_r) c->r[pend_r] = pend_v; /* (tmips.sv: S_D lands it before S_E stops) */ c->clk -= 3; c->stop = 1; c->why = 3; c->h_pc = pc; c->h_ir = ins; c->h_ea = ea; continue; }
                if (load) {
                    uint32_t w;
                    if (sp) { memcpy(&w, c->spad + (pa & 0x3FC), 4); c->clk += 2; }
                    else if (ram) { w = c->rd32(c->ctx, pa & ~3u); c->clk += c->mem_clk + TM_MEM; }
                    else {                                             /* (the benches' I/O) */
                        const int size = (op == 0x20 || op == 0x24) ? 1 : (op == 0x21 || op == 0x25) ? 2 : 4;
                        const uint32_t v = c->io_rd(c->ctx, pa, size);
                        w = size == 1 ? (v & 0xFF) << (8 * (pa & 3)) : size == 2 ? (v & 0xFFFF) << (8 * (pa & 2)) : v;
                        c->clk += c->mem_clk;
                    }
                    rval = w;
                    if (op == 0x32) c->gte_wr(c->ctx, (int)rt, w);
                    else { const uint32_t old = (pend_r == (int)rt) ? pend_v : vt; newld = rt; newld_v = ld_ext(op, pa, w, old); }
                } else {
                    uint32_t be = 0xF, d = vt;
                    if (op == 0x3A) { d = c->gte_rd(c->ctx, (int)rt); c->clk += 2; }
                    switch (op) {
                    case 0x28: be = 1u << (pa & 3); d = (vt & 0xFF) * 0x01010101u; break;
                    case 0x29: be = (pa & 2) ? 0xC : 0x3; d = (vt & 0xFFFF) * 0x00010001u; break;
                    case 0x2A: switch (pa & 3) { case 0: be = 1; d = vt >> 24; break; case 1: be = 3; d = vt >> 16; break;
                                                 case 2: be = 7; d = vt >> 8; break; default: be = 15; d = vt; } break;
                    case 0x2E: switch (pa & 3) { case 0: be = 15; d = vt; break; case 1: be = 14; d = vt << 8; break;
                                                 case 2: be = 12; d = vt << 16; break; default: be = 8; d = vt << 24; } break;
                    default: break;
                    }
                    rval = op == 0x3A ? d : vt;
                    if (sp) { for (int k = 0; k < 4; k++) if (be & (1u << k)) c->spad[(pa & 0x3FC) + k] = (uint8_t)(d >> (8 * k)); }
                    else if (ram) { c->wr32(c->ctx, pa & ~3u, d, be); c->clk += c->mem_clk + TM_MEM; }
                    else { const int size = op == 0x28 ? 1 : op == 0x29 ? 2 : 4; c->io_wr(c->ctx, pa, vt, size); c->clk += c->mem_clk;
                           if (pa == 0x1F801070u) c->cause &= ~0x400u; }
                }
            }
            break;
        }
        /* the delay slots */
        if (wr && wr < 32) c->r[wr] = wv;
        if (pend_r && pend_r != wr) c->r[pend_r] = pend_v;
        if (newld && newld < 32) { c->ld_r = newld; c->ld_v = newld_v; }
        c->r[0] = 0;
        if (c->retire) c->retire(c->ctx, pc, ins, ea, rval);
        c->in_ds = branch; ds_t = branch && taken;
        c->pc = npc; c->npc = nnpc;
    }
    return c->clk - start;
}
