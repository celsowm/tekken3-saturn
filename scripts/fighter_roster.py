"""The selectable roster: small facts about each character at costume 0.

Costume slots come from the costume key table (Japan Rev.1 0x80095CE8), voice
sets, bank types and name widths from g_charRecords (0x80098120), through
tools/research/char_records.py in the pinned Tekken3Remake, and the body sphere
radii from 0x80097384 (docs/research/code/combat.md). Addresses are Japan Rev.1.
Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0).
"""
from collections import namedtuple

Fighter = namedtuple('Fighter', 'id name slot voice_set bank_type name_width')
FIRST_RECORD = 71
SLOTS = 52
COMMON_RECORD = FIRST_RECORD + 4 * SLOTS
EXTRA_RECORDS = (10, 36, 56)

ROSTER = tuple(Fighter(*row) for row in (
    (0, 'PAUL', 0, 0, 0, 32),
    (1, 'LAW', 2, 1, 1, 26),
    (2, 'LEI', 4, 2, 2, 19),
    (3, 'KING', 6, 3, 3, 30),
    (4, 'YOSHIMITSU', 8, 4, 4, 74),
    (5, 'NINA', 10, 5, 5, 30),
    (6, 'HWOARANG', 12, 6, 6, 71),
    (7, 'XIAOYU', 14, 7, 7, 46),
    (8, 'EDDY', 16, 8, 8, 33),
    (9, 'JIN', 18, 9, 9, 20),
    (10, 'JULIA', 20, 10, 10, 37),
    (11, 'KUMA', 22, 11, 11, 36),
    (12, 'BRYAN', 24, 13, 12, 41),
    (13, 'HEIHACHI', 26, 14, 13, 57),
    (14, 'OGRE', 28, 15, 14, 35),
    (15, 'MOKUJIN', 30, 16, 9, 55),
    (16, 'GUN JACK', 34, 17, 16, 63),
    (17, 'GON', 42, 18, 19, 28),
    (18, 'ANNA', 36, 19, 17, 36),
    (19, 'DOCTOR B', 44, 20, 18, 67),
    (20, 'TRUE OGRE', 32, 21, 14, 35),
))
BY_BANK_TYPE = {f.bank_type: f for f in ROSTER if f.id not in (15, 20)}


def filter_words(rows):
    """branch_input_filter words of a bank: T3_BRANCH_INPUT_CLASSES (10) per 32 rows."""
    return (rows + 31) // 32 * 10


def records(fighter):
    """The fighter's disc records: KMD, voice header, ARC and DIVMOT."""
    kmd = FIRST_RECORD + 4 * fighter.slot
    return dict(kmd=kmd, vab=kmd + 1, arc=kmd + 2, bank=kmd + 3)


def bank_record(fighter):
    """The DIVMOT record the fighter plays. Mokujin copies another fighter's bank
    each round in the original; this port fixes his character record's bank."""
    return records(BY_BANK_TYPE[fighter.bank_type])['bank']


def body_radii(fighter):
    """The eight body sphere radii: head, forearms, waist, shins and feet."""
    head, waist = {11: (300, 375), 16: (300, 375), 20: (480, 375)}.get(fighter.id, (240, 300))
    return (head, 96, 96, waist, 120, 120, 120, 120)


def all_records():
    """Every disc record a full roster build reads, in order."""
    found = set(EXTRA_RECORDS) | {COMMON_RECORD}
    for fighter in ROSTER:
        r = records(fighter)
        found |= {r['kmd'], r['vab'], r['arc'], r['bank'], bank_record(fighter)}
    return sorted(found)
