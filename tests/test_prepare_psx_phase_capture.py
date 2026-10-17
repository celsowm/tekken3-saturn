import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import prepare_psx_phase_capture as capture


class RoundCaptureMappingTests(unittest.TestCase):
    def test_flow_capture_uses_halfword_pad_pairs_and_end_stop(self):
        layout_path=ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json'
        layout=json.loads(layout_path.read_text())
        raw=b'\0'*0x800+b'synthetic flow executable'
        layout['blocks']['exe']['sha256']=hashlib.sha256(raw[0x800:]).hexdigest()
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);exe=p/'exe';exe.write_bytes(raw)
            meta=p/'layout.json';meta.write_text(json.dumps(layout))
            with patch.object(capture,'US_EXE_SHA',hashlib.sha256(raw).hexdigest()):
                launcher=capture.prepare(exe,meta,p/'out',550,6500,round_flow_ram=True,stop_round_ends=2,pad_ram=True)
            provenance=json.loads((p/'out/provenance.json').read_text())
            self.assertTrue(provenance['round_flow_capture'])
            self.assertEqual(provenance['pad_fields']['held0']['address'],0x800A8C48)
            self.assertEqual(provenance['pad_fields']['held1']['address'],0x800A8C4A)
            self.assertEqual(provenance['pad_fields']['pressed0']['address'],0x800ADC78)
            self.assertEqual(provenance['pad_fields']['pressed1']['address'],0x800ADC7A)
            self.assertEqual(provenance['stop_round_ends'],2)
            fields=provenance['round_fields']
            for name,base in [('physical',0x800ADD68),('pressed',0x800ADF08)]:
                self.assertEqual(fields[name+'0']['address'],base)
                self.assertEqual(fields[name+'1']['address'],base+2)
                self.assertEqual(fields[name+'0']['size'],2)
                self.assertEqual(fields[name+'1']['size'],2)
            ranges=re.search(r'extra_ranges=\{(.*?)\},functions=',launcher.read_text()).group(1)
            spans=[tuple(map(int,pair)) for pair in re.findall(r'\{(\d+),(\d+)\}',ranges)]
            for field in fields.values():
                self.assertTrue(any(a<=field['address'] and field['address']+field['size']<=a+n for a,n in spans))
            self.assertIn('stop_round_ends=2,round_state=',launcher.read_text())
            with patch.object(capture,'US_EXE_SHA',hashlib.sha256(raw).hexdigest()):
                with self.assertRaisesRegex(ValueError,'round end stop'):
                    capture.prepare(exe,meta,p/'bad',550,6500,stop_round_ends=1)

    def test_verified_struct_offsets_widths_and_single_mapping(self):
        layout=json.loads((ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json').read_text())
        raw=b'\0'*0x800+b'synthetic executable body'
        layout['blocks']['exe']['sha256']=hashlib.sha256(raw[0x800:]).hexdigest()
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp);exe=p/'exe';exe.write_bytes(raw)
            meta=p/'layout.json';meta.write_text(json.dumps(layout))
            with patch.object(capture,'US_EXE_SHA',hashlib.sha256(raw).hexdigest()):
                launcher=capture.prepare(exe,meta,p/'out',550,6000,round_ram=True,stop_round_results=2)
            provenance=json.loads((p/'out/provenance.json').read_text())
            fields=provenance['round_fields']
            self.assertEqual(fields['match_winner']['address'],0x800AFAAA)
            self.assertEqual(fields['match_loser']['address'],0x800AFAAB)
            self.assertEqual(fields['human_mask']['size'],2)
            self.assertEqual(fields['tie_choice']['size'],1)
            self.assertEqual(fields['to_win']['size'],2)
            ranges=re.search(r'extra_ranges=\{(.*?)\},functions=',launcher.read_text()).group(1)
            ranges=[tuple(map(int,pair)) for pair in re.findall(r'\{(\d+),(\d+)\}',ranges)]
            for field in fields.values():
                self.assertTrue(any(a<=field['address'] and field['address']+field['size']<=a+n for a,n in ranges))
            self.assertIn((0x800AFA88,0x24),ranges)


if __name__=='__main__':unittest.main()
