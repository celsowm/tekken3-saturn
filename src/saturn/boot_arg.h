#ifndef T3_BOOT_ARG_H
#define T3_BOOT_ARG_H
/* The argument the intro, menu and fight programs hand each other through sat_exec.
 * A program that receives a different size treats the start as a cold boot. */
enum {
    BOOT_ARG_BYTES=16,
    ARG_PHASE=0, ARG_PICK0=1, ARG_PICK1=2,
    ARG_STAGE=3,           /* arcade ladder: the fight index, 0-9 */
    ARG_FLAGS=4,           /* bit 0 debug, 1 practice, 2-3 run mode, 4 music off, 5 VS MODE */
    ARG_WINS=5, ARG_SECONDS=6,                 /* two bytes, high first */
    ARG_SEED=8,            /* arcade ladder: four bytes, high first */
    ARG_LADDER=12,         /* LADDER_ON while a one-player arcade run lasts; LADDER_CARRY with a score to keep */
    ARG_CARRY_WINS=13,     /* two bytes: both sides' round wins */
    ARG_CARRY_PLAYED=15,   /* rounds played */
    /* LADDER_CHALLENGE: a challenger's VS match interrupts the run; LADDER_SWAP: port 2's player
     * won one and carries the run on, so the fight reads the two pads the other way round. */
    LADDER_ON=1, LADDER_CARRY=2, LADDER_CHALLENGE=4, LADDER_SWAP=8,
    PHASE_TITLE='T', PHASE_MENU='M', PHASE_SELECT='S', PHASE_FIGHT='F', PHASE_DEMO='D', PHASE_RESULT='R',
    PHASE_ENDING='E'
};
#endif
