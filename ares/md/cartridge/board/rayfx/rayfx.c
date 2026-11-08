/* rayfx.c - the compositor's reference model (rayfx.h says what it does) */
#include <string.h>
#include "rayfx.h"

void rfx_reset(rfx_t* fx) {
    fx->on = fx->key = 0;
    fx->ncmd = fx->need = 0;
    fx->bg = 0;
    memset(fx->layer, 0, sizeof(fx->layer));
    memset(fx->pic, 0, sizeof(fx->pic));
}

static int rfx_words(uint16_t w0) {    /* a command's length from its first word */
    switch ((w0 >> 10) & 15) {
    case RFX_OP_SPRITE: return 8;
    case RFX_OP_BG: return 5 + 2 * (w0 & 15);
    default: return 1;
    }
}

static void rfx_sprite(rfx_t* fx) {
    const uint16_t* c = fx->cmd;
    int flip = c[0] & 1, all = c[0] >> 1 & 1;
    uint8_t color = (uint8_t)(c[0] >> 2);
    int x = c[1], y = c[2], w = c[3], h = c[4], stride = c[5];
    uint32_t src = (uint32_t)(c[6] & 0x7FF) << 11 | (c[7] & 0x7FF);
    for (int r = 0; r < h; r++) {
        if (y + r >= 200) break;
        uint8_t* d = fx->layer + (y + r) * 320 + x;
        uint32_t s = src + (uint32_t)r * (uint32_t)stride;
        for (int i = 0; i < w; i++) {
            uint8_t v = fx->cart(fx->ctx, (s + (uint32_t)i) & 0x3FFFFF);
            int dx = flip ? w - i - 1 : i;
            if (all) {
                if (v != 0 && x + dx < 320) d[dx] = v;
            } else if (v < 160) {
                if (x + dx < 320) d[dx] = (uint8_t)(v | color);
            } else {
                i += v - 160;
            }
        }
    }
}

static void rfx_bg(rfx_t* fx) {
    const uint16_t* c = fx->cmd;
    fx->bg_n = c[0] & 15;
    fx->bg_addr = (uint32_t)(c[1] & 0x7FF) << 11 | (c[2] & 0x7FF);
    fx->bg_w = c[3] & 0x7FF;
    fx->bg_v = c[4] & 0x3FF;
    for (int i = 0; i < fx->bg_n; i++) { fx->b_end[i] = c[5 + 2 * i]; fx->b_f[i] = c[6 + 2 * i]; }
    fx->bg = fx->bg_w >= 4;
}

static void rfx_compose(rfx_t* fx) {
    for (int y = 0; y < 200; y++) {
        int r = y + fx->bg_v, f = 0;
        for (int i = 0; i < fx->bg_n; i++) if (r < fx->b_end[i]) { f = fx->b_f[i]; break; }
        f &= ~1;
        for (int x = 0; x < 320; x++) {
            uint8_t v = fx->layer[y * 320 + x];
            if (!v && fx->bg) {
                int col = (f + x) % fx->bg_w;
                v = fx->cart(fx->ctx, (fx->bg_addr + (uint32_t)r * (uint32_t)fx->bg_w + (uint32_t)col) & 0x3FFFFF);
            }
            fx->pic[y * 320 + x] = v;
        }
    }
}

static void rfx_word(rfx_t* fx, uint16_t w) {
    if (w & RFX_START) { fx->ncmd = 0; fx->need = rfx_words(w); }
    else if (fx->ncmd == 0) return;    /* an argument with no command: ignored */
    if (fx->ncmd >= (int)(sizeof(fx->cmd) / sizeof(fx->cmd[0]))) { fx->ncmd = 0; return; }
    fx->cmd[fx->ncmd++] = w;
    if (fx->ncmd < fx->need) return;
    switch ((fx->cmd[0] >> 10) & 15) {
    case RFX_OP_BEGIN: memset(fx->layer, 0, sizeof(fx->layer)); break;
    case RFX_OP_SPRITE: rfx_sprite(fx); break;
    case RFX_OP_BG: rfx_bg(fx); break;
    case RFX_OP_COMPOSE: rfx_compose(fx); break;
    case RFX_OP_OFF: fx->on = 0; fx->key = 0; fx->bg = 0; break;
    }
    fx->ncmd = 0;
}

int rfx_read(rfx_t* fx, uint32_t addr, uint16_t* data) {
    addr &= 0x3FFFFE;
    if (addr >= RFX_CMD_BASE) {
        uint16_t w = (uint16_t)((addr - RFX_CMD_BASE) >> 1);
        if (!fx->on) {
            static const uint16_t keys[3] = { RFX_KEY0, RFX_KEY1, RFX_KEY2 };
            fx->key = w == keys[fx->key] ? fx->key + 1 : w == RFX_KEY0 ? 1 : 0;
            if (fx->key < 3) return 0;
            fx->on = 1; fx->ncmd = 0;
            *data = (uint16_t)(RFX_ID << 8);
            return 1;
        }
        rfx_word(fx, w);
        *data = (uint16_t)(RFX_ID << 8);   /* (the model is never busy) */
        return 1;
    }
    if (fx->on && addr >= RFX_PIC_BASE && addr < RFX_PIC_END) {
        uint32_t i = addr - RFX_PIC_BASE;
        *data = (uint16_t)(fx->pic[i] << 8 | fx->pic[i + 1]);
        return 1;
    }
    return 0;
}
