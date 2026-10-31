/* T3_COLD_LOW_WRAM: LibSaturn defines its runtime state, including the VDP1
 * command staging buffer, in .wram_l. Inserted before the SDK script's .bss,
 * this rule claims that input section first and keeps it in High WRAM. The
 * section is NOLOAD and outside crt0's clear ranges: main() zeroes it before
 * sat_init. The SDK script's .bss and stack-reserve checks still apply. */
SECTIONS {
    .t3_sdk_hot (NOLOAD) : ALIGN(16) {
        ___t3_sdk_hot_start = .;
        *libsaturn.a:state.cpp.obj(.wram_l .wram_l.*)
        *libsaturn.a:vdp1.cpp.obj(.wram_l .wram_l.*)
        *libsaturn.a:asset_runtime.cpp.obj(.wram_l .wram_l.*)
        . = ALIGN(4);
        ___t3_sdk_hot_end = .;
    }
} INSERT BEFORE .bss;
