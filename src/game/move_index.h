#ifndef T3_MOVE_INDEX_H
#define T3_MOVE_INDEX_H
#include <stdint.h>
struct t3_divmot;
enum { T3_MOVE_INDEX_FALLBACK_SLOT = 0xDC5 };
/* A fighter's merged move index entry for `slot`: its own bank's entry, else
 * (when the bank is linked) the opponent bank's, else its own fallback slot.
 * Writes the bank holding the entry and the raw entry; 0 when empty. */
int t3_move_index_entry(const struct t3_divmot* bank,unsigned slot,
                        const struct t3_divmot** source,uint32_t* entry);
/* MoveTotalDamage: the u16 damage word plus the argument of every 0x0Bxx
 * (throw damage) event of the move's list, as the bank load stores it. */
uint16_t t3_move_total_damage(const struct t3_divmot* bank,uint16_t word,uint32_t event_index);
#endif
