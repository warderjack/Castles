/* Minimal CPU shell used instead of the Musashi interpreter in the standalone ("native") build.
   All guest code runs as lifted C; this file only provides the register file the host/HLE code reads and writes,
   and an execute loop that drives castles_instr_hook() (which enters lifted routines, services traps and stubs). */
#ifndef CASTLES_NATIVE_M68K_H
#define CASTLES_NATIVE_M68K_H
#include <stdint.h>
#include <stdio.h>
typedef enum { M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
               M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
               M68K_REG_PC, M68K_REG_SR, M68K_REG_SP } m68k_register_t;
#define M68K_CPU_TYPE_68030 0
#define CASTLES_NATIVE 1
extern uint32_t native_regs[19];
extern int native_end_slice;
extern void castles_instr_hook(unsigned int pc);
extern int native_halted_flag;
static inline unsigned int m68k_get_reg(void *ctx, m68k_register_t r) { (void)ctx; return native_regs[r == M68K_REG_SP ? M68K_REG_A7 : r]; }
static inline void m68k_set_reg(m68k_register_t r, unsigned int v) { native_regs[r == M68K_REG_SP ? M68K_REG_A7 : r] = v; }
static inline void m68k_set_cpu_type(int t) { (void)t; }
static inline void m68k_init(void) {}
static inline void m68k_pulse_reset(void) { for (int i = 0; i < 19; i++) native_regs[i] = 0; native_regs[M68K_REG_SR] = 0x2700; }
static inline void m68k_end_timeslice(void) { native_end_slice = 1; }
int m68k_execute(int cycles);
#endif
