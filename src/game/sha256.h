#ifndef T3_SHA256_H
#define T3_SHA256_H
#include <stddef.h>
#include <stdint.h>
/* Portable observation digest. This is a diagnostic, not a gameplay clock. */
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} t3_sha256_t;
void t3_sha256_init(t3_sha256_t* state);
void t3_sha256_update(t3_sha256_t* state,const void* data,size_t length);
void t3_sha256_finish(t3_sha256_t* state,uint8_t digest[32]);
#endif
