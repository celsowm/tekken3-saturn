"""Synthetic fixture integrity and fail-closed continuous-trace comparisons."""
import hashlib,json,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import build_reference_replay as fixture
import saturn_reference_suite as suite

def synthetic_export(root,frames):
    seed=b'T3RANGES'+struct.pack('<III',1,0x80000000,4)+bytes([1,2,3,4])
    (root/'seed.ranges').write_bytes(seed)
    (root/'seed-addresses.json').write_text(json.dumps({n:0x80000000 for n in ('step_x','step_z','previous','ramp')}))
    rows=[str(frame)+'\t'+('\t'.join(map(str,[32 if i>0 else 0,0,0,0,0,0,0,0,3]))) for i,frame in enumerate(frames)]
    (root/'inputs.tsv').write_text('\n'.join(rows)+'\n')
    digests=b''.join(hashlib.sha256(bytes([i])).digest() for i in range(len(rows)))
    (root/'expected.sha256').write_bytes(digests)
    project=Path(__file__).resolve().parents[1]
    manifest=dict(schema=1,codec='sha256-i64be-v1',ticks=len(rows),audit_passed=True,trace_sha256='a'*64,
        observation_schema_sha256=hashlib.sha256(b''.join((project/n).read_bytes() for n in fixture.SCHEMA_FILES)).hexdigest(),
        files={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in
               ('seed.ranges','seed-addresses.json','inputs.tsv','expected.sha256')})
    (root/'manifest.json').write_text(json.dumps(manifest))

    return digests

class ReferenceReplayTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.export=self.root/'export';self.export.mkdir()
        self.out=self.root/'guest';self.out.mkdir()
        self.frames=[100,101,101,105]
        self.make_export()

    def make_export(self):
        self.digests=synthetic_export(self.export,self.frames)

    def guest(self,count=4,error=0):
        (self.out/'reference-fixture.bin').write_bytes(hashlib.sha256((self.export/'manifest.json').read_bytes()).digest())
        (self.out/'reference-count.bin').write_bytes(struct.pack('>I',count))
        (self.out/'reference-error.bin').write_bytes(struct.pack('>I',error))
        (self.out/'reference-trace.bin').write_bytes(self.digests)
        (self.out/'reference-fields.bin').write_bytes(struct.pack('>4H',3,3,3,3))
        return dict(elf_sha256='b'*64,provenance={})

    def test_fixture_encodes_source_gaps_and_input_changes(self):
        out=self.root/'fixture.h';report=fixture.generate(self.export,out);text=out.read_text()
        self.assertEqual(report['events'],2)
        self.assertEqual(report['expected_state_bytes_in_image'],0)
        self.assertIn('t3_reference_deltas[] = {\n    49,',text) # packed 1,0,4
        self.assertNotIn('reference_expected',text)
        self.assertIn('T3_REFERENCE_TICKS 4u',text)

    def test_changed_export_is_refused(self):
        (self.export/'seed.ranges').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'changed private'):fixture.load(self.export)

    def test_unsupported_source_gap_is_refused(self):
        self.frames=[100,103];self.make_export()
        with self.assertRaisesRegex(ValueError,'source-frame delta'):fixture.generate(self.export,self.root/'fixture.h')

    def test_full_trace_and_first_divergence(self):
        observed=self.guest();report=suite.compare(self.out,self.export,observed)
        self.assertTrue(report['passed']);self.assertEqual(report['matching_prefix'],4)
        changed=bytearray(self.digests);changed[32]^=1
        (self.out/'reference-trace.bin').write_bytes(changed)
        report=suite.compare(self.out,self.export,observed)
        self.assertFalse(report['passed']);self.assertEqual(report['first_difference']['tick'],2)
        self.assertEqual(report['matching_prefix'],1)

    def test_matching_prefix_cannot_pass_full_gate(self):
        report=suite.compare(self.out,self.export,self.guest(count=2))
        self.assertFalse(report['passed']);self.assertFalse(report['complete'])
        self.assertEqual(report['matching_prefix'],2)

    def test_guest_failure_and_field_count_are_not_hidden(self):
        report=suite.compare(self.out,self.export,self.guest(error=4))
        self.assertFalse(report['passed']);self.assertEqual(report['error'],4)
        observed=self.guest()
        (self.out/'reference-fields.bin').write_bytes(struct.pack('>4H',3,3,2,3))
        report=suite.compare(self.out,self.export,observed)
        self.assertFalse(report['passed']);self.assertEqual(report['first_difference']['tick'],3)

    def test_wrong_digest_length_is_refused(self):
        observed=self.guest();(self.out/'reference-trace.bin').write_bytes(self.digests[:-1])
        with self.assertRaisesRegex(ValueError,'size/counter mismatch'):suite.compare(self.out,self.export,observed)

    def test_guest_fixture_identity_is_bound_to_export(self):
        observed=self.guest();(self.out/'reference-fixture.bin').write_bytes(bytes(32))
        with self.assertRaisesRegex(ValueError,'fixture identity'):suite.compare(self.out,self.export,observed)

if __name__=='__main__':unittest.main()
