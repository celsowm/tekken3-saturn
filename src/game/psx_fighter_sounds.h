#ifndef T3_PSX_FIGHTER_SOUNDS_H
#define T3_PSX_FIGHTER_SOUNDS_H
#include <stdint.h>
#include <stddef.h>

/* Sound decisions only. Rendering/audio backends consume the original codes;
 * replay recording and voice remapping belong to the consumer. */
enum { T3_SOUND_SYSTEM=1,T3_SOUND_VOICE=2,T3_SOUND_EFFECT=3,T3_SOUND_VOICE_OFF=4 };
typedef void (*t3_psx_sound_emit_t)(void* user,unsigned kind,unsigned player,uint16_t code);
typedef struct {
    const uint8_t* data;size_t size;
} t3_psx_sound_tables_t;
typedef struct {
    int16_t script,last_script,position,frame,voice_cooldown;
    int32_t damage_voice_timer;
    uint8_t hold;
} t3_psx_sound_state_t;
typedef struct {
    uint8_t player,character,voice_set,first_joint;
    uint8_t ko,hit_clean,got_hit,contact,guarded,move_changed,landed_a,landed_b,wooden;
    int32_t health;
    int16_t damage,throw_state,pose_frame;
    uint8_t air_first,active_first;
    const uint8_t* commands;size_t command_bytes; /* <=3 little-endian u16, FFFF ends */
} t3_psx_sound_facts_t;
typedef struct {
    t3_psx_sound_tables_t tables;
    uint32_t* random;
    uint32_t frame_counter,vblank;
    unsigned camera_phase;
    t3_psx_sound_emit_t emit;void* user;
} t3_psx_sound_context_t;

int t3_psx_sound_tables_valid(t3_psx_sound_tables_t tables);
/* 1 resolved, 0 invalid category/index, -1 malformed private tables. */
int t3_psx_sound_voice_code(t3_psx_sound_tables_t tables,unsigned voice,uint16_t id,uint16_t* code);
void t3_psx_sound_script_stop(t3_psx_sound_state_t* state);
/* Run one active fighter in FightFrame order. Caller retains both states:
 * impact/special commands can stop the opponent's script. Returns 0 for
 * malformed private tables or commands; no gameplay approximations. */
int t3_psx_fighter_sounds(t3_psx_sound_state_t state[2],
    const t3_psx_sound_facts_t facts[2],unsigned actor,const t3_psx_sound_context_t* context);
#endif
