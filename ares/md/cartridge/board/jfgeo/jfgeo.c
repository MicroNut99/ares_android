/* jfgeo.c - the cart's geometry engine, the reference model: what jfgeo.h says, step by step
 *
 * A frame:
 *   1. every object of the stage image: its bounding sphere's centre in camera space (full scale,
 *      (M (o + c - camera)) >> 15); dropped when behind / beyond FAR / outside the view / under MIN_SIZE
 *      pixels; its depth step (z >> 6, far first) and whether it gets its small version (under LOD_PIX).
 *   2. a counting sort on the depth step (stable: equal steps keep the image's order).
 *   3. each object in that order: its vertices ((M (o + v - camera)) >> 16: half scale, x / z the same),
 *      projected: rz = 11796480 / z (FOCAL << 16), x and y clamped to +-8 z, sx = 160 + (x rz >> 16),
 *      sy = 112 + (y rz >> 16); z < 32 (NEAR halved): the vertex is behind, its triangles are dropped; so is
 *      z > 65535 (impossible inside FAR; it keeps the hardware's products within 18 x 18 and 20 x 20 bits).
 *   4. its triangles: dropped when turned away or under 1 pixel (twice the area > -MIN_AREA2), then the
 *      corners sorted by y, dropped when off the screen; written into list A (y0 < split) and / or list B
 *      (y2 > split).
 * Shifts of negative numbers are arithmetic (floor), divisions are of positive numbers.
 */
#include "jfgeo.h"
#include <string.h>

#define MAX_OBJ 1024

static JfGeoStats *S;
static JfGeoRead RD;
static void *CTX;

static int32_t rd16(uint32_t a)            /* a 16-bit word of PSRAM, signed */
{
    S->mem_reads++;
    return (int16_t)RD(CTX, a & 0xFFFFFE);
}
static uint32_t rdu16(uint32_t a) { return (uint32_t)rd16(a) & 0xFFFF; }
static void wr16(uint8_t *out, uint32_t a, uint32_t v)
{
    S->mem_writes++;
    out[a] = (uint8_t)(v >> 8); out[a + 1] = (uint8_t)v;
}
static int32_t asr(int64_t v, int n) { return (int32_t)(v >> n); }   /* arithmetic (gcc / clang: floor) */

int jfgeo_frame(JfGeoRead rd, void *ctx, const uint16_t *p, uint8_t *out, JfGeoStats *st)
{
    static JfGeoStats dummy;
    S = st ? st : &dummy; RD = rd; CTX = ctx;
    memset(S, 0, sizeof *S);
    uint32_t img = (uint32_t)p[0] << 16 | p[1];
    int32_t cam[3] = { (int16_t)p[2], (int16_t)p[3], (int16_t)p[4] };
    int32_t m[9];
    for (int i = 0; i < 9; i++) m[i] = (int16_t)p[5 + i];
    int32_t split = (int16_t)p[14];
    uint32_t nA = 0, nB = 0, full = 0;

    if (rdu16(img) != 0x4A46 || (rdu16(img + 2) >> 8) != 0x53)       /* "JFS" */
    {
        wr16(out, 0, 0); wr16(out, 2, 0); wr16(out, 6, 0);
        return -1;
    }
    uint32_t nmesh = rdu16(img + 4), nobj = rdu16(img + 10);
    uint32_t nvert = rdu16(img + 6), ntri = rdu16(img + 8);
    uint32_t mesh0 = img + 12, vert0 = mesh0 + 20 * nmesh, tri0 = vert0 + 8 * nvert, obj0 = tri0 + 4 * ntri;
    if (nobj > MAX_OBJ) nobj = MAX_OBJ;
    S->objects = nobj;

    /* 1. cull, depth step, small version */
    static uint16_t cand[MAX_OBJ], order[MAX_OBJ];
    static uint8_t cand_d[MAX_OBJ];
    static uint16_t bucket[256];
    uint32_t n = 0;
    memset(bucket, 0, sizeof bucket);
    for (uint32_t i = 0; i < nobj; i++)
    {
        uint32_t o = obj0 + 8 * i;
        uint32_t mi = rdu16(o + 6);
        if (mi >= nmesh) continue;
        uint32_t M = mesh0 + 20 * mi;
        int32_t c[3];
        for (int k = 0; k < 3; k++) c[k] = rd16(o + 2 * k) + rd16(M + 12 + 2 * k) - cam[k];
        int32_t q[3];
        for (int r = 0; r < 3; r++)
            q[r] = asr((int64_t)m[3 * r] * c[0] + (int64_t)m[3 * r + 1] * c[1] + (int64_t)m[3 * r + 2] * c[2], 15);
        int32_t rad = rd16(M + 18), xc = q[0], yc = q[1], zc = q[2];
        if (zc + rad < JFGEO_NEAR || zc - rad > JFGEO_FAR) continue;
        int32_t ax = xc < 0 ? -xc : xc, ay = yc < 0 ? -yc : yc;
        if ((int64_t)(ax - 2 * rad) * JFGEO_FOCAL > (int64_t)(zc + rad) * 160) continue;
        if ((int64_t)(ay - 2 * rad) * JFGEO_FOCAL > (int64_t)(zc + rad) * 112) continue;
        if (zc > rad && (int64_t)rad * JFGEO_FOCAL < (int64_t)JFGEO_MIN_SIZE * zc) continue;
        int32_t d = zc < 0 ? 0 : zc >> 6;
        if (d > 255) d = 255;
        d = 255 - d;
        /* the mesh's +8: u8 hue, lod nvert; +10: u8 lod ntri, 0 */
        int lod = (rdu16(M + 10) >> 8) && (int64_t)rad * JFGEO_FOCAL < (int64_t)JFGEO_LOD_PIX * zc;
        cand[n] = (uint16_t)(i | (lod ? 0x8000 : 0)); cand_d[n] = (uint8_t)d; bucket[d]++; n++;
    }
    /* 2. far first */
    for (uint32_t i = 0, s = 0; i < 256; i++) { uint32_t k = bucket[i]; bucket[i] = (uint16_t)s; s += k; }
    for (uint32_t i = 0; i < n; i++) order[bucket[cand_d[i]]++] = cand[i];
    S->drawn_objects = n;

    /* 3 + 4 */
    static int16_t sx[256], sy[256];
    for (uint32_t k = 0; k < n; k++)
    {
        uint32_t i = order[k] & 0x3FF, o = obj0 + 8 * i, M = mesh0 + 20 * rdu16(o + 6);
        uint32_t v0 = rdu16(M), nv = rdu16(M + 2), t0 = rdu16(M + 4), nt = rdu16(M + 6);
        if (order[k] & 0x8000) { nv = rdu16(M + 8) & 0xFF; nt = rdu16(M + 10) >> 8; }
        if (nv > 256) continue;
        int32_t base[3];
        for (int j = 0; j < 3; j++) base[j] = rd16(o + 2 * j) - cam[j];
        for (uint32_t v = 0; v < nv; v++)
        {
            uint32_t a = vert0 + 8 * (v0 + v);
            int32_t w[3];
            for (int j = 0; j < 3; j++) w[j] = base[j] + rd16(a + 2 * j);
            int32_t q[3];
            for (int r = 0; r < 3; r++)
                q[r] = asr((int64_t)m[3 * r] * w[0] + (int64_t)m[3 * r + 1] * w[1] + (int64_t)m[3 * r + 2] * w[2], 16);
            int32_t z = q[2];
            S->vertices++;
            if (z < JFGEO_NEAR / 2 || z > 65535) { sx[v] = 0; sy[v] = -32768; continue; }
            int32_t rz = (int32_t)(((uint32_t)JFGEO_FOCAL << 16) / (uint32_t)z);
            int32_t lim = z * 8, x = q[0], y = q[1];
            if (x > lim) x = lim;
            if (x < -lim) x = -lim;
            if (y > lim) y = lim;
            if (y < -lim) y = -lim;
            sx[v] = (int16_t)(160 + asr((int64_t)x * rz, 16));
            sy[v] = (int16_t)(112 + asr((int64_t)y * rz, 16));
        }
        for (uint32_t t = 0; t < nt; t++)
        {
            uint32_t a = tri0 + 4 * (t0 + t);
            uint32_t ab = rdu16(a), cc = rdu16(a + 2);
            uint32_t ia = ab >> 8, ib = ab & 0xFF, ic = cc >> 8, col = cc & 0xFF;
            S->triangles++;
            if (ia >= nv || ib >= nv || ic >= nv) continue;
            int32_t x0 = sx[ia], y0 = sy[ia], x1 = sx[ib], y1 = sy[ib], x2 = sx[ic], y2 = sy[ic];
            if (y0 == -32768 || y1 == -32768 || y2 == -32768) continue;
            int32_t area2 = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
            if (area2 > -JFGEO_MIN_AREA2) continue;
            int32_t tx, ty;
            if (y0 > y1) { tx = x0; x0 = x1; x1 = tx; ty = y0; y0 = y1; y1 = ty; }
            if (y0 > y2) { tx = x0; x0 = x2; x2 = tx; ty = y0; y0 = y2; y2 = ty; }
            if (y1 > y2) { tx = x1; x1 = x2; x2 = tx; ty = y1; y1 = y2; y2 = ty; }
            if (y2 < 0 || y0 >= 224 || y0 >= y2) continue;
            if (x0 < 0 && x1 < 0 && x2 < 0) continue;
            if (x0 >= 320 && x1 >= 320 && x2 >= 320) continue;
            int side = (x1 - x0) * (y2 - y0) > (x2 - x0) * (y1 - y0);
            int inA = y0 < split, inB = y2 > split;
            for (int l = 0; l < 2; l++)
            {
                if (!(l ? inB : inA)) continue;
                uint32_t *cnt = l ? &nB : &nA;
                if (*cnt >= JFGEO_LIST) { full = 1; continue; }
                uint32_t r = 16 + (uint32_t)l * JFGEO_LIST * 16 + *cnt * 16;
                wr16(out, r, (uint32_t)y0); wr16(out, r + 2, (uint32_t)y1); wr16(out, r + 4, (uint32_t)y2);
                wr16(out, r + 6, (uint32_t)x0); wr16(out, r + 8, (uint32_t)x1); wr16(out, r + 10, (uint32_t)x2);
                wr16(out, r + 12, col << 8 | (uint32_t)side); wr16(out, r + 14, 0);
                (*cnt)++;
            }
            S->written++;
        }
    }
    static uint16_t frame;
    wr16(out, 0, nA); wr16(out, 2, nB); wr16(out, 4, ++frame); wr16(out, 6, full);
    return 0;
}
