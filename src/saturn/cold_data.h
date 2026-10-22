#ifndef T3_SATURN_COLD_DATA_H
#define T3_SATURN_COLD_DATA_H
#include "saturn/core.h"

/* T3_COLD_LOW_WRAM: read the immutable DIVMOT banks from MOTION.BIN into
 * their Low WRAM storage and verify the build's checksum. Must complete
 * before any bank is opened. A no-op returning SAT_OK otherwise. */
sat_result_t t3_cold_data_load(void);

/* T3_VERIFY_COLD: re-hash a slice of the loaded banks per call, wrapping over
 * both banks. Returns SAT_ERR_IO once a completed pass differs from the
 * build's checksum; passes are counted in g_t3_cold_verify_passes. The SH-2
 * has no memory protection, so this detects stray writes in test builds
 * only. A no-op returning SAT_OK otherwise. */
sat_result_t t3_cold_data_verify_slice(void);
extern volatile uint32_t g_t3_cold_verify_passes;

#endif
