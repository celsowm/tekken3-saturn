/* Boot-time cost probe (T3_MEMORY_PROBE): FRC/128 ticks for 4096 iterations
 * of eight identical instructions, so a result r means r * 128 / 32768 =
 * r / 256 cycles per instruction (loop overhead in row 0). Read
 * g_t3_memory_probe from a Mednafen save state (scripts/mednafen_state.py). */
#include <stdint.h>
#include "saturn/time.h"

#define N 4096u
volatile uint32_t g_t3_memory_probe[12];

#define EIGHT(x) x x x x x x x x
#define RUN(slot,setup,body) do { \
    uint32_t n=N; \
    setup; \
    const uint16_t t0=sat_time_frc(); \
    __asm__ volatile("1:\n\t" body "dt %0\n\tbf 1b" : "+r"(n), "+r"(p) : : "r0","r1","r2","t","mach","macl","memory"); \
    g_t3_memory_probe[slot]=(uint16_t)(sat_time_frc()-t0); \
} while(0)

static uint32_t g_hot[16] __attribute__((aligned(16)));

void t3_memory_probe(void) {
    uint32_t* p;
    uint32_t stack[64] __attribute__((aligned(16)));
    /* 0: loop overhead only (dt, bf). */
    RUN(0,p=g_hot,"");
    /* 1: eight loads from one cached line. */
    RUN(1,p=g_hot,EIGHT("mov.l @%1,r0\n\t"));
    /* 2: eight stores to one High WRAM address (write-through). */
    RUN(2,p=stack,EIGHT("mov.l r0,@%1\n\t"));
    /* 3: eight loads, each a new 16-byte line of code in High WRAM (misses);
     * 4096 * 8 * 16 bytes wraps the 512 KB from 0x06004000. */
    RUN(3,p=(uint32_t*)0x06004000u,EIGHT("mov.l @%1,r0\n\tadd #16,%1\n\t") "mov.l 2f,r1\n\tand r1,%1\n\tmov.l 3f,r1\n\tor r1,%1\n\tbra 4f\n\tnop\n\t.align 2\n2:\t.long 0x0007FFF0\n3:\t.long 0x06004000\n4:\n\t");
    /* 4: eight uncached loads from High WRAM (cache-through alias). */
    RUN(4,p=(uint32_t*)(0x20000000u|(uint32_t)g_hot),EIGHT("mov.l @%1,r0\n\t"));
    /* 5: eight uncached loads from Low WRAM. */
    RUN(5,p=(uint32_t*)0x20200000u,EIGHT("mov.l @%1,r0\n\t"));
    /* 6: eight stores to Low WRAM (the last 512 bytes before sat_exec's are free at boot). */
    RUN(6,p=(uint32_t*)0x002FFC00u,EIGHT("mov.l r0,@%1\n\t"));
    /* 7: eight mac.l on cached data. */
    RUN(7,p=g_hot,"clrmac\n\t" EIGHT("mov %1,r1\n\tmac.l @r1+,@r1+\n\t"));
    /* 8: eight mul.l and sts macl. */
    RUN(8,p=g_hot,EIGHT("mul.l r0,r0\n\tsts macl,r0\n\t"));
    /* 9: one 64/32 divide on the on-chip divider (DVSR, DVDNTH, DVDNTL, read). */
    RUN(9,p=(uint32_t*)0xFFFFFF00u,"mov #7,r0\n\tmov.l r0,@%1\n\tmov #0,r0\n\tmov.l r0,@(16,%1)\n\tmov #100,r0\n\tmov.l r0,@(20,%1)\n\tmov.l @(20,%1),r0\n\t");
    /* 10: eight halfword stores to one High WRAM address. */
    RUN(10,p=stack,EIGHT("mov.w r0,@%1\n\t"));
    /* 11: eight stores, alternating with eight cached loads. */
    RUN(11,p=stack,EIGHT("mov.l r0,@%1\n\tmov.l @%1,r2\n\t"));
}
