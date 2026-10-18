#include "perf.h"
#if defined(T3_PERF_SAMPLES)
t3_perf_sample_t g_t3_perf_current;
volatile t3_perf_capture_t g_t3_perf __attribute__((section(".wram_l")));
void t3_perf_init(void) {
    /* Low WRAM is not ordinary startup-cleared BSS. Old samples are ignored
     * until count publishes them; only the metadata needs initialization. */
    g_t3_perf.version=1;g_t3_perf.count=0;g_t3_perf.cursor=0;
}
void t3_perf_begin(void) { g_t3_perf_current=(t3_perf_sample_t){0}; }
void t3_perf_end(void) {
    g_t3_perf.version=1;
    g_t3_perf.samples[g_t3_perf.cursor]=g_t3_perf_current;
    g_t3_perf.cursor=(g_t3_perf.cursor+1u)%T3_PERF_CAPACITY;
    ++g_t3_perf.count;
}
#endif
