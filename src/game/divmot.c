#include "divmot.h"
#include "branch_rules.h"
#include <stddef.h>

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)le16(p) | ((uint32_t)le16(p+2) << 16u);
}

int t3_divmot_open(t3_divmot_t* out, const uint8_t* data, size_t length) {
    uint32_t sections[T3_DIVMOT_SECTIONS];
    unsigned i;
    uint16_t moves;
    uint8_t kind;
    if (!out || !data || length < 64u || length > 4u * 1024u * 1024u)
        return 0;
    kind = data[1];
    moves = le16(data+2);
    if (data[0] != 0u || (kind > 20u && kind != 99u) || !moves)
        return 0;
    for (i = 0; i < T3_DIVMOT_SECTIONS; ++i) {
        sections[i] = le32(data + 4u + i*4u);
        if (sections[i] < 64u || sections[i] > length ||
            (i && sections[i] < sections[i-1u]))
            return 0;
    }
    if ((sections[1]-sections[0])/T3_DIVMOT_BRANCH_BYTES > 65535u) return 0;
    if (sections[0] != 64u || sections[14] != length ||
        (sections[1]-sections[0]) % T3_DIVMOT_BRANCH_BYTES ||
        sections[2]-sections[1] != (uint32_t)moves*T3_DIVMOT_MOVE_BYTES ||
        (kind != 99u && sections[3]-sections[2] != T3_DIVMOT_MOVE_SLOTS*4u) ||
        (kind == 99u && sections[3] != sections[2]))
        return 0;
    out->data = data;
    out->size = length;
    for (i = 0; i < T3_DIVMOT_SECTIONS; ++i) out->sections[i]=sections[i];
    out->move_count = moves;
    out->branch_count = (uint16_t)((sections[1]-sections[0]) /
                                    T3_DIVMOT_BRANCH_BYTES);
    out->bank_type = kind;
    return 1;
}

int t3_divmot_move(const t3_divmot_t* bank, unsigned index,
                   t3_move_record_t* out) {
    const uint8_t* p;
    if (!bank || !out || !bank->data || index >= bank->move_count) return 0;
    p = bank->data + bank->sections[1] + index*T3_DIVMOT_MOVE_BYTES;
    out->animation_offset = le32(p);
    out->state_word = le32(p+4);
    out->attack_word = le16(p+8);
    out->branch_index = le32(p+12);
    out->stance_slot = le16(p+16);
    out->damage_class = le16(p+20);
    out->step = (int16_t)le16(p+22);
    out->length = p[24];
    out->air_start = p[25];
    out->air_end = p[26];
    out->hold_frame = p[27];
    out->flags = le32(p+36);
    out->attack_descriptor = le32(p+40);
    out->hit_freeze = p[44];
    out->active_first = p[45];
    out->active_last = p[46];
    out->reaction = le16(p+50);
    out->close_reaction = le16(p+52);
    return 1;
}

int t3_divmot_branch(const t3_divmot_t* bank, unsigned index,
                     t3_branch_record_t* out) {
    const uint8_t* p;
    if (!bank || !bank->data || !out || index >= bank->branch_count) return 0;
    p = bank->data + bank->sections[0] + index*T3_DIVMOT_BRANCH_BYTES;
    out->command = le16(p);
    out->restriction = p[2];
    out->condition = p[3];
    out->parameter = le16(p+4);
    out->target_slot = le16(p+6);
    out->flags = p[8];
    out->first_frame = p[9];
    out->last_frame = p[10];
    out->target_frame = p[11];
    return 1;
}

int t3_divmot_slot_resolve(const t3_divmot_t* bank,
                            const t3_divmot_t* common,
                            unsigned slot, t3_move_record_t* out,
                            const t3_divmot_t** owner) {
    uint32_t id;
    const t3_divmot_t* selected;
    if (owner) *owner = NULL;
    if (!bank || !out || !bank->data || bank->bank_type==99u ||
        slot>=T3_DIVMOT_MOVE_SLOTS) return -1;
    id=le32(bank->data+bank->sections[2]+slot*4u);
    if (id==T3_DIVMOT_EMPTY_SLOT) return 0;
    selected=bank;
    if (id>=0x40000000u) {
        if (!common || common->bank_type!=99u || !common->data) return 0;
        selected=common;
        id-=0x40000000u;
    }
    if (!t3_divmot_move(selected,id,out)) return 0;
    if (owner) *owner=selected;
    return 1;
}

int t3_divmot_slot(const t3_divmot_t* bank, const t3_divmot_t* common,
                   unsigned slot, t3_move_record_t* out) {
    return t3_divmot_slot_resolve(bank,common,slot,out,NULL);
}

int t3_divmot_branch_pick_ex(const t3_divmot_t* bank, t3_move_record_t current,
                             const t3_input_history_t* input, unsigned frame,
                             uint8_t own_bank, const t3_branch_context_t* facts,
                             const t3_command_sequence_t* sequences,
                             size_t sequence_count,
                             t3_branch_record_t* picked) {
    unsigned i;
    t3_branch_record_t row;
    if (!bank || !input || !picked || current.branch_index>=bank->branch_count)
        return 0;
    for (i=current.branch_index; i<bank->branch_count; ++i) {
        if (!t3_divmot_branch(bank,i,&row)) return 0;
        if (row.command == 0xC000u) {
            if (frame >= current.length) {*picked=row;return 1;}
            return 0;
        }
        if (frame<row.first_frame || frame>row.last_frame ||
            !t3_branch_restriction_matches(row.restriction,own_bank,facts) ||
            !t3_branch_condition_matches(row.condition,row.parameter,facts))
            continue;
        if (t3_command_matches_table(row.command,input,sequences,sequence_count)) {
            if (row.flags==0x2Cu) {
                /* An eligible skip-to-terminator branch discards the remaining
                 * alternatives. An ineligible one must NOT skip them. */
                while (++i<bank->branch_count) {
                    if (!t3_divmot_branch(bank,i,&row)) return 0;
                    if (row.command==0xC000u) break;
                }
                if (i>=bank->branch_count) return 0;
                if (frame>=current.length) {*picked=row;return 1;}
                return 0;
            }
            *picked=row;return 1;
        }
    }
    return 0;
}

int t3_divmot_branch_pick(const t3_divmot_t* bank, t3_move_record_t current,
                          const t3_input_history_t* input, unsigned frame,
                          t3_branch_record_t* picked) {
    return t3_divmot_branch_pick_ex(bank,current,input,frame,bank ? bank->bank_type : 0u,NULL,NULL,0u,picked);
}
