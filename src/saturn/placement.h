#ifndef T3_SATURN_PLACEMENT_H
#define T3_SATURN_PLACEMENT_H

/* Bank for state that is rewritten every image or tick. By default it shares
 * Low WRAM with large buffers because High WRAM holds the embedded DIVMOT
 * banks. T3_COLD_LOW_WRAM loads those immutable banks into Low WRAM from the
 * disc instead, leaving High WRAM (SDRAM) for this mutable working set. */
#if defined(T3_COLD_LOW_WRAM)
#define T3_HOT_WRAM
#else
#define T3_HOT_WRAM __attribute__((section(".wram_l")))
#endif

#endif
