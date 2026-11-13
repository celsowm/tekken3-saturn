/* Diagnostic only (T3_FORCE_PROBE_TEXT_PAD): bytes of code space ahead of the game library's code. The SH-2 cache maps
 * code by address, so a few bytes of layout move the input probe's counts by an image; an A/B shifts every build by
 * the same few amounts and compares the totals (scripts/mednafen_force_rate_sweep.py --vary). */
#if T3_FORCE_PROBE_TEXT_PAD
/* Kept by -Wl,--undefined (CMakeLists.txt): --gc-sections would drop it otherwise. */
__attribute__((section(".text.t3_probe_pad"),aligned(4)))
const unsigned char g_t3_probe_text_pad[T3_FORCE_PROBE_TEXT_PAD]={1};
#else
typedef int t3_probe_text_pad_unused;
#endif
