// sh_sound.h - Super Hang-On's sound board for the reference (host/sh_sound.cpp, docs/04). Ours (MIT).
#pragma once
#include <cstdint>
void sh_sound_init(const uint8_t* z80rom /* 32 KB */, const uint8_t* pcm256k);
void sh_sound_latch(uint8_t d, long at = 0); // the main 68000's write to the 315-5195's register 3, `at` Z80 cycles
                                             // into the next sh_sound_run's slice
void sh_sound_reset(bool on, long at = 0);   // the main 68000's I/O 0x0020 bit 0 (0: the sound held in reset)
int  sh_sound_run(long z80_cycles, int16_t* out, int max_frames);   // the SegaPCM's raw sums, left / right
extern void (*sh_sound_pcm_log)(uint8_t reg, uint8_t data);          // each write to the SegaPCM (for MAME's)
extern void (*sh_sound_latch_log)(uint8_t data);                     // each Z80 read of the latch
uint8_t sh_sound_peek(uint16_t z80_addr);                           // (debug) the Z80's view of its memory
