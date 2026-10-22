#ifndef T3_SATURN_DISC_H
#define T3_SATURN_DISC_H
#include "saturn/cdfs.h"

/* One CD block and ISO volume mounted on first use and shared by every
 * boot-time loader. Not reentrant; call only from the Master during boot. */
sat_result_t t3_disc_volume(sat_cdfs_volume_t** out_volume);

#endif
