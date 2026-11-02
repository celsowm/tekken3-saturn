import gzip
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import c352_scsp_convert as convert


def vgm(writes, rom, total_samples=44100):
    """A minimal VGM 1.71 with one C352 ROM block; writes are (time in 44100ths, register, value)."""
    body = bytearray()
    body += bytes([0x67, 0x66, 0x92]) + struct.pack('<III', 8 + len(rom), len(rom), 0) + rom
    now = 0
    for t, reg, val in sorted(writes):
        if t > now:
            body += bytes([0x61]) + struct.pack('<H', t - now)
            now = t
        body += bytes([0xE1]) + struct.pack('>HH', reg, val)
    body += bytes([0x66])
    header = bytearray(0x100)
    header[:4] = b'Vgm '
    struct.pack_into('<I', header, 0x18, total_samples)
    struct.pack_into('<I', header, 0x34, 0x100 - 0x34)
    struct.pack_into('<I', header, 0xDC, 25401600)
    header[0xD6] = 0x48
    return bytes(header) + bytes(body)


def voice(v, vol_f, freq, flags, bank, start, end, loop):
    base = v * 8
    return [(0, base + 0, vol_f), (0, base + 1, 0), (0, base + 2, freq), (0, base + 3, flags),
            (0, base + 4, bank), (0, base + 5, start), (0, base + 6, end), (0, base + 7, loop)]


class Tests(unittest.TestCase):
    def test_mulaw_table_matches_libvgm(self):
        table = convert.mulaw_table()
        self.assertEqual(table[0], 0)
        self.assertEqual(table[1], 32)
        self.assertEqual(table[127], 31232)
        self.assertEqual(table[128], -32)                # ~0 & ~0x1f, as a signed 16-bit value
        self.assertEqual(table[255], -(table[127] + 32))

    def test_pitch_word(self):
        self.assertEqual(convert.pitch_word(44100), 0)
        self.assertEqual(convert.pitch_word(22050), 0xF << 11)
        self.assertEqual(convert.pitch_word(88200), 1 << 11)
        self.assertEqual(convert.pitch_word(44100 * (1 + 3 / 1024)), 3)

    def test_level_and_pan(self):
        tl, pan = convert.level_pan(0x4000, 0, 1.0)      # hard left at 64/256
        self.assertEqual(tl, round(20 * 0.60206 / 0.375))
        self.assertEqual(pan, (7 << 13) | (0x10 | 15) << 8)
        tl, pan = convert.level_pan(0x4040, 0, 1.0)      # centred
        self.assertEqual(pan >> 8 & 0x1F, 0)
        self.assertEqual(convert.level_pan(0, 0, 1.0), (0xFF, 0))

    def test_note_plan(self):
        rom = bytes(range(256)) * 64
        writes = voice(2, 0x2020, 0x8000, 0x0002, 0, 100, 4000, 200) + [(0, 3 * 8 + 3, 0)]
        writes += [(0, 0x202, 0)]
        writes[3] = (0, 2 * 8 + 3, 0x4002)              # KEYON | LOOP
        writes += [(441 * 10, 2 * 8 + 3, 0x2002), (441 * 10, 0x202, 0)]    # KEYOFF at 0.1 s
        data = vgm(writes, rom)
        path = Path(self.id() + '.vgz')
        path.write_bytes(gzip.compress(data))
        try:
            r = convert.convert(path, 0.0, 0.2, 0x3000, 1.0)
        finally:
            path.unlink()
        self.assertEqual(len(r['notes']), 1)
        events = r['events']
        base = r['placed'][0].slot * 0x20
        key_on = [e for e in events if e[1] == base and e[2] & convert.KYONB]
        key_off = [e for e in events if e[1] == base and not e[2] & convert.KYONB and e[2] & convert.KYONEX]
        self.assertEqual(len(key_on), 1)
        self.assertEqual(key_on[0][0], 0)
        self.assertTrue(key_on[0][2] & convert.KYONEX)
        self.assertEqual(len(key_off), 1)
        self.assertEqual(key_off[0][0], convert.tick_of(0.1))
        regs = {e[1] - base: e[2] for e in events if e[0] == 0 and base <= e[1] < base + 0x20}
        self.assertEqual(regs[0x04], 100)                # loop start relative to the sample start
        self.assertEqual(regs[0x06], 3901)               # sample length: LEA is exclusive
        self.assertEqual(regs[0x10], convert.pitch_word(88200 * 0x8000 / 65536))
        self.assertEqual(r['dropped'], [])

    def test_slot_reuse_waits_for_a_key_off_tick(self):
        rom = bytes(range(256)) * 64
        writes = voice(0, 0x2020, 0x8000, 0x4000, 0, 0, 99, 0)       # one-shot of 100 samples (~4.5 ms)
        writes += [(0, 0x202, 0), (441 * 10, 0, 0x2020), (441 * 10, 3, 0x4000), (441 * 10, 0x202, 0)]
        path = Path(self.id() + '.vgz')
        path.write_bytes(gzip.compress(vgm(writes, rom)))
        try:
            r = convert.convert(path, 0.0, 0.2, 0x3000, 1.0)
        finally:
            path.unlink()
        self.assertEqual(len(r['notes']), 2)
        self.assertEqual({n.slot for n in r['placed']}, {0})         # the second note reuses the first one's slot
        off_ticks = sorted(e[0] for e in r['events'] if e[1] == 0 and not e[2] & convert.KYONB and e[2] & convert.KYONEX)
        on_ticks = sorted(e[0] for e in r['events'] if e[1] == 0 and e[2] & convert.KYONB)
        self.assertEqual(len(on_ticks), 2)
        self.assertTrue(any(on_ticks[0] < t < on_ticks[1] for t in off_ticks))   # keyed off strictly before the retrigger


if __name__ == '__main__':
    unittest.main()
