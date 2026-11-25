/* host/shconf.h - Musashi's configuration for host/shangon_ref: two plain 68000s, interrupts
 * acknowledged through the host (level lines stay asserted until the board drops them, as MAME's
 * ASSERT_LINE), the RESET instruction reported (the main 68000's RESET resets the sub 68000:
 * segaorun.cpp m68k_reset_callback). Everything else is Musashi's own m68kconf.h. */
#ifndef SHCONF_H
#define SHCONF_H
#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2
#define M68K_EMULATE_INT_ACK     M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A) sh_int_ack(A)
#define M68K_EMULATE_RESET       M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_HOOK    M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) sh_instr_hook(pc)
#define M68K_RESET_CALLBACK()    sh_reset_out()
#ifdef __cplusplus
extern "C" {
#endif
int  sh_int_ack(int level);
void sh_reset_out(void);
void sh_instr_hook(unsigned int pc);
#ifdef __cplusplus
}
#endif
#include "m68kconf.h"
#endif
