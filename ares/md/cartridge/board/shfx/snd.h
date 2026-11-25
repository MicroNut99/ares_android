/* snd.h - Super Hang-On's sound effects on the slave SH2 (snd.c, docs/12). MIT. */
#ifndef SND_H
#define SND_H
#include <stdint.h>
typedef struct {
    uint32_t instr, ticks;                  /* Z80 instructions run, ticks made */
    uint32_t lost;                          /* bytes from the main CPU lost to a full queue */
    int32_t  voices;                        /* the voices playing in the last tick */
} snd_stats_t;
extern snd_stats_t snd_stats;
void snd_init(const uint8_t *z80rom /* 32 KB */, const uint8_t *pcm128k /* the four sample ROMs in a row */);
void snd_event(uint16_t e, uint16_t delta); /* the core's latch FIFO entry (0x48 bits 9-0: a byte, or bit 9 a reset, bit 8 1 =
                                              let go) and its time after the one before (0x4A: 4 us units, 4095 = 16 ms or more) */
/* one 8 ms tick: the Z80, then n output samples (31,250 / step a second: the address moves step chip samples a sample),
   stereo into out[2n] (left, right, ...): the voices' volume (0-127) x (sample - 0x80) summed, unscaled */
int  snd_is_music(uint16_t e);              /* the tunes' commands (MD+ plays them; the Z80 gets 0x80) */
void snd_tick(int32_t *out, int n, int step);
#ifndef __32X__
extern void (*snd_pcm_log)(uint8_t reg, uint8_t data);   /* (the PC: each write to the SegaPCM) */
void snd_debug(void);
extern unsigned *snd_pc_hist;                          /* (the PC: instructions by address) */
uint8_t snd_peek(uint16_t a);                                   /* (the PC: the state, to stderr) */
#endif
#endif
