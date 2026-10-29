#ifndef T3_PSX_REPLAY_SOUNDS_H
#define T3_PSX_REPLAY_SOUNDS_H
#include <stdint.h>

enum { T3_REPLAY_SOUND_FRAMES=300 };
typedef struct { uint8_t kind,player;uint16_t code; } t3_psx_replay_sound_t;
/* Value-owned normal-VS sound ring. Initialize once, then preserve ring words
 * across round resets; only delay resets. The caller selects the playback bank
 * and supplies the replay cursor AFTER ReplayUpdate advances it. */
/* 0 invalid metadata, 1 success (including non-recording no-op). */
int t3_psx_replay_sound_record(uint32_t ring[T3_REPLAY_SOUND_FRAMES],int32_t* delay,
    int32_t mode,unsigned record,unsigned kind,unsigned player,unsigned code);
/* ReplayUpdate's recording branch, AFTER its record cursor advances. Clears
 * only the kind byte of the next free slot, retaining its code/player bytes. */
int t3_psx_replay_sound_clear(uint32_t ring[T3_REPLAY_SOUND_FRAMES],int32_t* delay,unsigned record);
/* 0 odd half-frame/empty/unknown kind, 1 original kind 1..3, -1 bad argument.
 * Returns raw logical codes; voice/sample resolution and replay echoes belong
 * to the audio consumer. It must never record a playback request again. */
int t3_psx_replay_sound_at(const uint32_t ring[T3_REPLAY_SOUND_FRAMES],unsigned play,
    t3_psx_replay_sound_t* sound);
#endif
