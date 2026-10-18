import struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_saturn_stages import CAPACITY,FIELDS,decode,summarize
from run_saturn_stage_matrix import cache_options

class StageRingTests(unittest.TestCase):
    def ring(self,count):
        raw=bytearray(12+CAPACITY*len(FIELDS)*4)
        struct.pack_into('>III',raw,0,1,count,count%CAPACITY)
        for i in range(max(0,count-CAPACITY),count):
            values=[i+200,4,7]+[0]*(len(FIELDS)-3)
            values[FIELDS.index('simulation')]=12
            struct.pack_into('>'+len(FIELDS)*'I',raw,12+(i%CAPACITY)*len(FIELDS)*4,*values)
        return raw
    def test_wrapped_ring_and_per_tick_cost(self):
        rows=decode(self.ring(CAPACITY+3))
        self.assertEqual(len(rows),CAPACITY-1)
        self.assertEqual(rows[0]['field'],204)
        self.assertEqual(rows[-1]['field'],CAPACITY+202)
        report=summarize(rows)
        self.assertEqual(report['simulation_ms_per_tick'],3)
        self.assertEqual(report['stage_ms']['simulation']['p95'],12)
        self.assertEqual(report['elapsed_fields_over_four'],(CAPACITY-1)*3)
        raw=self.ring(CAPACITY+3)
        struct.pack_into('>I',raw,12+3*len(FIELDS)*4,0xFFFFFFFF)
        self.assertEqual(decode(raw),rows) # ignore a partially overwritten slot
    def test_reject_incomplete_or_unpublished_dump(self):
        raw=self.ring(2)
        with self.assertRaises(ValueError):decode(raw[:-1])
        struct.pack_into('>I',raw,8,1)
        with self.assertRaises(ValueError):decode(raw)
        with self.assertRaises(ValueError):decode(self.ring(0))
    def test_reject_empty_warmup_window(self):
        with self.assertRaises(ValueError):summarize(decode(self.ring(2)),999)

    def test_matrix_preserves_optimization_and_toolchain_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);cache=root/'CMakeCache.txt'
            cache.write_text('CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG\n'+
                             '\n'.join(k+':FILEPATH=/fixture/'+k for k in
                             ('CMAKE_TOOLCHAIN_FILE','LibSaturn_DIR','LIBSATURN_SOURCE_DIR','Python3_EXECUTABLE')))
            meta=root/'CMakeFiles/4.0/CMakeCCompiler.cmake';meta.parent.mkdir(parents=True)
            meta.write_text('set(CMAKE_C_COMPILER "/compiler/bin/sh2eb-elf-gcc")\n')
            options,compiler=cache_options(cache)
            self.assertIn('-DCMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG',options)
            self.assertEqual(compiler,Path('/compiler/bin'))
            cache.write_text(cache.read_text().replace('-O3 -DNDEBUG',''))
            with self.assertRaisesRegex(ValueError,'Release C flags'):cache_options(cache)

if __name__=='__main__':unittest.main()
