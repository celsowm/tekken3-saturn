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

    def test_pages_split_between_ticks_and_keep_every_event(self):
        events = [(t, 0x10, t) for t in range(0, 1300) for _ in range(1)]
        events += [(700, 0x12, 0)] * 5                   # a tick with several events
        events.sort(key=lambda e: e[0])
        pages = convert.paginate(events, 1400)
        self.assertEqual(sum(p['count'] for p in pages), len(events))
        self.assertTrue(all(p['count'] <= 512 for p in pages))
        self.assertTrue(pages[-1]['last'] and not any(p['last'] for p in pages[:-1]))
        for a, b in zip(pages, pages[1:]):
            self.assertEqual(a['end'] + a['time_base'], b['events'][0][0] + b['time_base'])   # no gap between pages
        self.assertEqual(pages[-1]['end'] + pages[-1]['time_base'], 1400)

    def test_relative_pages_rebase_times(self):
        events = [(1000 + i, 0x10, i) for i in range(600)]
        pages = convert.paginate(events, 1700, relative=True)
        self.assertEqual(pages[0]['time_base'], 1000)
        self.assertEqual(pages[0]['events'][0][0], 0)
        self.assertEqual(pages[1]['time_base'] + pages[1]['events'][0][0], events[pages[0]['count']][0])
        blob = convert.pages_bytes(pages)
        count, flags, base, end, _ = struct.unpack_from('>HHIII', blob, 0)
        self.assertEqual((count, flags, base), (pages[0]['count'], 0, 1000))
        self.assertEqual(len(blob), sum(16 + 8 * p['count'] for p in pages))

    def test_a_128_sample_tick_halves_the_quantisation(self):
        try:
            convert.set_timebase(128, 0.0)
            self.assertEqual(convert.tick_of(0.01), round(0.01 * 44100 / 128))
            self.assertAlmostEqual(convert.TICK_SECONDS, 128 / 44100)
            convert.set_timebase(256, 10.0)
            self.assertEqual(convert.tick_of(10.0), 0)       # stream time 0 is the window start
        finally:
            convert.set_timebase(256, 0.0)

    def test_dynamic_bank_never_shares_space_and_time(self):
        from types import SimpleNamespace
        keys = {k: (0x3000 + 0x10000 * k, False, 30000) for k in range(4)}          # four 30,000-byte samples
        notes = [SimpleNamespace(key=(k, 0, 0, False), t_on=t, t_off=t + 2.0, t_natural=None)
                 for k, t in ((0, 1.0), (1, 2.0), (2, 12.0), (3, 13.0), (0, 30.0))]
        convert.set_timebase(256, 0.0)
        res, loads, report = convert.plan_dynamic_bank(notes, {n.key: keys[n.key[0]] for n in notes}, 0x3000, 80000)
        self.assertEqual(report['residencies'], 4 + 1)                              # sample 0 comes back after 28 s
        self.assertEqual(report['reloaded_samples'], 1)
        self.assertLessEqual(report['packed_extent_bytes'], 80000)                  # 2 live at a time, not 4
        for a in res:
            for b in res:
                if a is not b and a['addr'] < b['addr'] + b['bytes'] and b['addr'] < a['addr'] + a['bytes']:
                    self.assertTrue(a['span'][1] <= b['span'][0] or b['span'][1] <= a['span'][0])
                    later, earlier = (a, b) if a['span'][0] > b['span'][0] else (b, a)
                    self.assertGreaterEqual(later['load_after'], earlier['span'][1])    # written only after the old one ends
        for n in notes:
            self.assertIsNotNone(n.address_override)
        with self.assertRaises(ValueError):
            convert.plan_dynamic_bank(notes, {n.key: keys[n.key[0]] for n in notes}, 0x3000, 40000)

    def test_the_packer_reaches_the_live_peak_when_first_fit_does_not(self):
        # A long-lived small sample placed first would force first-fit by start time to leave a hole; the best of the
        # orders is never worse than time order and never below the live peak.
        from types import SimpleNamespace
        convert.set_timebase(256, 0.0)
        sizes = {0: 20000, 1: 40000, 2: 40000, 3: 20000}
        keys = {k: (0x3000 + 0x20000 * k, False, sizes[k]) for k in sizes}
        notes = [SimpleNamespace(key=(k, 0, 0, False), t_on=t, t_off=t + d, t_natural=None)
                 for k, t, d in ((0, 0.0, 9.0), (1, 1.0, 2.0), (2, 4.0, 2.0), (3, 5.0, 8.0))]
        addresses = {n.key: keys[n.key[0]] for n in notes}
        _, _, report = convert.plan_dynamic_bank(notes, addresses, 0x3000, 1 << 20, lead_s=0.5, merge_gap_s=0.0)
        self.assertGreaterEqual(report['packed_extent_bytes'], report['peak_live_bytes'])
        self.assertLessEqual(report['packed_extent_bytes'], report['peak_live_bytes'] + 20000)
        def residencies():
            return [dict(bytes=sizes[k], first=k, span=(k - 0.5, k + 9.0)) for k in sizes]
        by_order = {order: convert.place(residencies(), order) for order in convert.PACK_ORDERS}
        res = residencies()
        convert.pack_residencies(res)
        self.assertEqual(max(r['addr'] + r['bytes'] for r in res), min(by_order.values()))

    def test_the_loop_is_unrolled_in_order_and_the_total_grows(self):
        info = dict(events=[(0, 1, 1), (100, 2, 2), (250, 3, 3)], total=300, loop_start=200, loop_samples=100)
        same = convert.unroll_loop(info, 1)
        self.assertEqual(same['events'], info['events'])
        two = convert.unroll_loop(info, 2)
        self.assertEqual(two['total'], 400)
        self.assertEqual(two['events'][-1], (350, 3, 3))                            # the loop's event again, one length later
        self.assertEqual([e[0] for e in two['events']], sorted(e[0] for e in two['events']))
        three = convert.unroll_loop(info, 3)
        self.assertEqual(len([e for e in three['events'] if e[1:] == (3, 3)]), 3)
        flat = dict(events=[(0, 1, 1)], total=10, loop_start=10, loop_samples=0)
        self.assertEqual(convert.unroll_loop(flat, 5)['events'], flat['events'])  # a song without a loop plays once

    def test_max_slots_keeps_the_score_off_the_slots_left_for_the_sh2(self):
        rom = bytes(range(64)) * 8
        writes = []
        for v in range(8):
            writes += voice(v, 0x80, 0x4000, 0x0000, 0, 0, 100, 0)
        writes += [(10, 0x202, 0)]
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'a.vgm.gz'
            path.write_bytes(gzip.compress(vgm(writes, rom)))
            for limit in (8, 4):
                r = convert.convert(path, 0.0, 1.0, 0x3000, 0.5, max_slots=limit)
                used = {n.slot for n in r['placed']}
                self.assertTrue(all(slot < limit for slot in used))

    def test_meta_bin_matches_the_game_module_layout(self):
        # src/saturn/arcade_music.h t3_music_meta_t: sixteen big-endian words, magic first.
        import tempfile
        rom = bytes(range(64)) * 8
        writes = voice(0, 0x80, 0x4000, 0x0000, 0, 0, 100, 0) + [(10, 0x202, 0)]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'a.vgm.gz'
            path.write_bytes(gzip.compress(vgm(writes, rom)))
            r = convert.convert(path, 0.0, 1.0, 0x3000, 0.5, tick_samples=128, bank_budget=100000)
            manifest = convert.summary(r, type('A', (), dict(start=0.0, end=1.0, pcm8_base=0x3000))(), path)
            out = Path(tmp) / 'o'
            convert.write_outputs(out, r, manifest, 0.0, 1.0, 0x3000)
            meta = struct.unpack('>16I', (out / 'meta.bin').read_bytes())
        self.assertEqual(meta[0], convert.META_MAGIC)
        self.assertEqual(meta[1], 1)
        self.assertEqual(meta[4], 128)                                              # tick_samples
        self.assertEqual(meta[6], 0x3000)                                           # base
        self.assertEqual(meta[11], 1000)                                            # window_ms


if __name__ == '__main__':
    unittest.main()
