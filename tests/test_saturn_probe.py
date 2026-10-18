"""Validate the external debugger ABI without depending on host C layout."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import saturn_probe as probe


class DiagnosticsTests(unittest.TestCase):
    def test_native_panic_cannot_hide_behind_stale_diagnostics(self):
        raw=struct.pack('>Iii32s',0x50414E43,-8,193,b'main.c\0')
        self.assertEqual(probe.decode_panic(raw),dict(code=-8,line=193,file='main.c'))
        self.assertIsNone(probe.decode_panic(bytes(44)))
        with self.assertRaises(ValueError):probe.decode_panic(raw[:-1])

    def fixture(self):
        fields = probe.PREFIX + [f'{name}{i}' for name in probe.PAIRS for i in range(2)] + probe.TAIL
        values = dict.fromkeys(fields, 0)
        values.update(magic=0x54334431, version=14, frame_serial=17,
                      fighter_x0=0xfffffe98, fighter_x1=360,
                      winner=0xffffffff, first_error=0xfffffffd,
                      sim_ticks=83, vblanks=108, health0=930, health1=1000)
        return (struct.pack('>'+'I'*len(fields),*(values[name] for name in fields))+
                bytes(152)+struct.pack('>I10h',1,-1,295,4,8,14,9,0,2,-1,3))

    def test_target_big_endian_signed_fields_and_independent_clocks(self):
        state = probe.decode(self.fixture())
        self.assertEqual(state['fighter_x'], [-360, 360])
        self.assertEqual(state['health'], [930, 1000])
        self.assertEqual(state['winner'], -1)
        self.assertEqual(state['first_error'], -3)
        self.assertEqual((state['frame_serial'], state['sim_ticks'], state['vblanks']), (17, 83, 108))
        self.assertEqual(state['psx_lifecycle_active'],1)
        self.assertEqual(state['psx_pending_slot'],[-1,295])
        self.assertEqual(state['psx_hold_frames'],[-1,3])

    def test_owned_coordinates_and_health(self):
        raw=bytearray(self.fixture());struct.pack_into('>I',raw,4,15)
        raw+=struct.pack('>I14i',1,140*65536,134*65536,
                         -1000,-1027,-4,1000,-1027,4,-1000,0,-4,1000,0,4)
        state=probe.decode(raw)
        self.assertEqual(state['psx_owned_active'],1)
        self.assertEqual(state['psx_health_q16'],[140*65536,134*65536])
        self.assertEqual(state['psx_world_root'],[-1000,-1027,-4,1000,-1027,4])
        with self.assertRaises(ValueError):probe.decode(raw[:-1])

    def test_body_contact_snapshot(self):
        raw=bytearray(self.fixture());struct.pack_into('>I',raw,4,16)
        raw+=struct.pack('>I14i',1,140*65536,140*65536,*([0]*12))
        raw+=struct.pack('>III6i',1,7,9,-25,0,0,25,0,0)
        state=probe.decode(raw)
        self.assertEqual(state['psx_body_contacts'],[7,9])
        self.assertEqual(state['psx_body_push'],[-25,0,0,25,0,0])
        with self.assertRaises(ValueError):probe.decode(raw[:-1])

    def test_contact_snapshot(self):
        raw=bytearray(self.fixture());struct.pack_into('>I',raw,4,17)
        raw+=struct.pack('>I14i',1,140*65536,140*65536,*([0]*12))
        raw+=struct.pack('>III6i',1,7,9,-25,0,0,25,0,0)
        raw+=struct.pack('>IIIiiIIII',1,2,0,8,-1,2,0,3,0)
        state=probe.decode(raw)
        self.assertEqual(state['psx_contact_zone'],[8,-1])
        self.assertEqual(state['psx_hit_gate'],[2,0])
        self.assertEqual(state['psx_contact_frames'],[3,0])
        with self.assertRaises(ValueError):probe.decode(raw[:-1])

    def test_retained_gate_query_counters(self):
        raw=bytearray(self.fixture());struct.pack_into('>I',raw,4,18)
        raw+=bytes(60+36+36)+struct.pack('>II',4,0)
        self.assertEqual(probe.decode(raw)['psx_gate_frames'],[4,0])
        with self.assertRaises(ValueError):probe.decode(raw[:-1])

    def test_live_combat_snapshot(self):
        raw=bytearray(self.fixture());struct.pack_into('>I',raw,4,19)
        raw+=bytes(60+36+36+8)
        raw+=struct.pack('>I6I6i6I',1,0,2,0,1,0,0,0,12,-1,2352,-1,42,1,0,0,1,0,1)
        state=probe.decode(raw)
        self.assertEqual(state['psx_combat_active'],1)
        self.assertEqual(state['psx_registered_hits'],[0,2])
        self.assertEqual(state['psx_reaction_slot'],[-1,2352])
        self.assertEqual(state['psx_ko_collapse'],[0,1])
        with self.assertRaises(ValueError):probe.decode(raw[:-1])

    def test_saved_version_13_remains_readable(self):
        raw=bytearray(self.fixture()[:-24]);struct.pack_into('>I',raw,4,13)
        state=probe.decode(raw)
        self.assertEqual(state['version'],13)
        self.assertNotIn('psx_lifecycle_active',state)

    def test_original_round_and_replay_snapshot(self):
        raw=bytearray(self.fixture());struct.pack_into('>I',raw,4,20)
        raw+=bytes(60+36+36+8+76)
        raw+=struct.pack('>I7i2I',1,4,91,240,1,12,300,-2,2000,3)
        state=probe.decode(raw)
        self.assertEqual(state['psx_round_flow_active'],1)
        self.assertEqual(state['psx_round_state'],4)
        self.assertEqual(state['psx_replay_play'],300)
        self.assertEqual(state['psx_replay_mark'],-2)
        self.assertEqual(state['psx_replay_epoch'],3)
        with self.assertRaises(ValueError):probe.decode(raw[:-1])

    def test_reject_truncated_or_wrong_endian_or_unpublished(self):
        raw = self.fixture()
        for invalid in (raw[:-3], b''.join(raw[i:i + 4][::-1] for i in range(0, len(raw), 4)),
                        raw[:8] + bytes(4) + raw[12:], raw[:320] + struct.pack('>I', 3) + raw[324:]):
            with self.subTest(invalid=invalid[:12]), self.assertRaises(ValueError):
                probe.decode(invalid)


if __name__ == '__main__':
    unittest.main()
