"""No original PlayStation files: fabricate DIVMOT bank and test the compactor."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import divmot_to_logic as t3

def fake_bank(kind=0):
    b0=64
    b1=b0+12
    b2=b1+56
    b3=b2+(0 if kind==99 else t3.SLOTS*4)
    b4=b3+32
    buf=bytearray(b4)
    buf[1]=kind
    struct.pack_into("<H",buf,2,1)
    sections=(b0,b1,b2,b3)+tuple([b4]*11)
    struct.pack_into("<15I",buf,4,*sections)
    struct.pack_into("<H",buf,b0,0xC000)
    buf[b1+24]=16
    if kind!=99:
        for s in range(t3.SLOTS):
            struct.pack_into("<I",buf,b2+s*4,0 if s==0 else 0x3FFFFFFF)
    buf[b3:b4]=bytes(range(32))
    return bytes(buf)

class Tests(unittest.TestCase):
    def test_bank_compact(self):
        source=fake_bank(0)
        out,meta=t3.compact(source)
        self.assertLess(len(out),len(source))
        self.assertEqual(len(source)-len(out),32)
        self.assertEqual(meta["move_rows"],1)
        self.assertEqual(meta["branch_rows"],1)
        self.assertEqual(meta["slot_count"],4023)
        self.assertEqual(out[64:],source[64:-32])
        self.assertEqual(t3.sections_of(out)[1][-1],len(out))
        self.assertEqual(t3.compact(out)[0],out)

    def test_common_compact(self):
        source=fake_bank(99)
        out,meta=t3.compact(source)
        self.assertEqual(meta["bank_type"],99)
        self.assertEqual(meta["slot_count"],0)
        self.assertEqual(t3.sections_of(out)[0],1)

    def test_keep_section_9_attack_points_without_other_bank_sections(self):
        source=fake_bank(99)
        attack=bytes(range(80))
        # Fake original: the 32-byte section3 is discarded; section9 is
        # retained only when explicitly opting into authored hitbox data.
        data=bytearray(source+attack)
        for i in range(5,10):
            struct.pack_into("<I",data,4*i+4,len(source))
        for i in range(10,15):
            struct.pack_into("<I",data,4*i+4,len(data))
        default,default_meta=t3.compact(bytes(data))
        self.assertFalse(default_meta["attack_geometry_included"])
        with_points,meta=t3.compact(bytes(data),with_hitboxes=True)
        self.assertEqual(meta["hitbox_bytes"],len(attack))
        self.assertEqual(t3.sections_of(with_points)[1][9],
                         t3.sections_of(default)[1][-1])
        self.assertEqual(with_points[-len(attack):],attack)
        self.assertEqual(len(with_points)-len(default),len(attack))
        self.assertEqual(t3.compact(with_points,with_hitboxes=True)[0],with_points)
        self.assertEqual(t3.compact(with_points)[0],default)

    def test_preserve_motion_streams_and_hitboxes_independently(self):
        source = fake_bank(99)
        motion = b"\x34\x12" + bytes([12, 0]) + bytes(48) + b"\x00"*40
        attack = bytes(range(80))
        # Original section3 holds 32 sound bytes, section6 is appended,
        # section9 contains original authored hit points. Other sections empty.
        body = bytearray(source + motion + attack)
        s3 = struct.unpack_from("<I", body, 4 + 3*4)[0]
        s6 = len(source)
        s7 = s6 + len(motion)
        end = len(body)
        offsets = [64, 76, 132, s3, s6, s6, s6,
                   s7, s7, s7, end, end, end, end, end]
        struct.pack_into("<15I", body, 4, *offsets)
        default, _ = t3.compact(bytes(body))
        anim, meta_anim = t3.compact(bytes(body), with_motion=True)
        hit, meta_hit = t3.compact(bytes(body), with_hitboxes=True)
        both, meta_both = t3.compact(bytes(body), with_motion=True,
                                    with_hitboxes=True)
        self.assertEqual(meta_anim["motion_bytes"], len(motion))
        self.assertTrue(meta_anim["motion_streams_included"])
        self.assertEqual(meta_both["hitbox_bytes"], len(attack))
        self.assertEqual(len(anim)-len(default), len(motion))
        self.assertEqual(len(hit)-len(default), len(attack))
        self.assertEqual(len(both)-len(default), len(motion)+len(attack))
        starts = t3.sections_of(both)[1]
        self.assertEqual(starts[7]-starts[6], len(motion))
        self.assertEqual(starts[10]-starts[9], len(attack))
        self.assertEqual(both[starts[6]:starts[7]], motion)
        self.assertEqual(both[starts[9]:starts[10]], attack)
        self.assertEqual(t3.compact(both, True, True)[0], both)
        self.assertEqual(t3.compact(both, True, False)[0], hit)
        self.assertEqual(t3.compact(both, False, True)[0], anim)

    def test_private_frame_event_section4_opt_in(self):
        # Exactly two u16 frame/command pairs and one terminator.
        source=fake_bank()
        events=struct.pack("<6H",2,0x0B09,5,0x0C1A,0,0)
        body=bytearray(source+events)
        old=len(source)
        for section in range(5,15):
            struct.pack_into("<I",body,4+section*4,len(body))
        assert struct.unpack_from("<I",body,4+4*4)[0]==old
        default,manifest=t3.compact(bytes(body))
        self.assertFalse(manifest["move_frame_events_included"])
        assert manifest["event_bytes"]==0
        private,with_events=t3.compact(bytes(body),with_events=True)
        directory=t3.sections_of(private)[1]
        self.assertEqual(directory[5]-directory[4],len(events))
        self.assertEqual(private[directory[4]:directory[5]],events)
        self.assertEqual(with_events["event_bytes"],len(events))
        self.assertTrue(with_events["move_frame_events_included"])
        self.assertEqual(t3.compact(private,with_events=True)[0],private)
        self.assertEqual(t3.compact(private)[0],default)

    def test_reject_malformed(self):
        source=bytearray(fake_bank())
        source[0]=7
        with self.assertRaises(t3.FormatError):t3.compact(bytes(source))
        source=bytearray(fake_bank())
        struct.pack_into("<I",source,12,33)
        with self.assertRaises(t3.FormatError):t3.compact(bytes(source))

if __name__=="__main__":unittest.main()
