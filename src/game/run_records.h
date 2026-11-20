#ifndef T3_GAME_RUN_RECORDS_H
#define T3_GAME_RUN_RECORDS_H
/* Best Survival and Time Attack results. The Saturn record is our own encoding (no original save layout is
 * read), checksummed so a damaged record loads as nothing. */
#include <stdint.h>

#define T3_RUN_RECORDS_BYTES 16u
#define T3_RUN_RECORDS_MAX_WINS 255u
#define T3_RUN_RECORDS_MAX_SECONDS 5999u   /* 99:59, what the result screen can show */

typedef struct {
    uint8_t survival_wins;      /* the most wins in one Survival run; 0 = none yet */
    uint16_t attack_seconds;    /* the fastest ten wins; 0 = none yet */
} t3_run_records_t;

int t3_run_records_pack(const t3_run_records_t* in,uint8_t out[T3_RUN_RECORDS_BYTES]);
int t3_run_records_unpack(const uint8_t* in,uint32_t bytes,t3_run_records_t* out);
/* Fold a finished run in; returns 1 when it set a new record. Survival counts wins (a run with none is no record), Time
 * Attack the time of a completed run (`completed`: all ten won; a time of 0 is not one). */
int t3_run_records_survival(t3_run_records_t* records,unsigned wins);
int t3_run_records_attack(t3_run_records_t* records,unsigned seconds,int completed);
#endif
