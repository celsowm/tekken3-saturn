"""Synthetic stage parsing and orientation tests; no retail fixtures."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import build_local_paul_stage as stage


def descriptor(word=8<<16,clut=480<<6):
    return struct.pack('<I',128)+struct.pack('<II',word,clut<<16)*100+struct.pack('<hh',0,-51)


class StageTests(unittest.TestCase):
    def setUp(self):
        self.vram=[0]*(1024*512)
        self.vram[480*1024:480*1024+16]=range(1,17)
        for y in range(64):
            for x in range(0,64,4):
                self.vram[y*1024+512+x//4]=sum(((x+i+3*y)%16)<<(4*i) for i in range(4))

    def test_floor_cell_packing_round_trip(self):
        data,palette,colors=stage.floor_tile(self.vram,descriptor())
        self.assertEqual(len(data),2048)
        for y in range(64):
            for x in range(64):
                at=((y//8)*8+x//8)*32+(y%8)*4+(x%8)//2
                index=(data[at]>>(0 if x%2 else 4))&15
                self.assertEqual(palette[index],1+(x+3*y)%16)
                self.assertEqual(colors[y*64+x],palette[index])

    def test_all_floor_rotations_and_reflections(self):
        # Four rotations of the unmodified tile, then flips of each axis.
        maps=(lambda x,y:(x,y),lambda x,y:(y,63-x),
              lambda x,y:(63-x,63-y),lambda x,y:(63-y,x))
        for turn in range(4):
            for flip in range(4):
                word=(8<<16)|(turn<<30)|(flip<<25)
                _,_,colors=stage.floor_tile(self.vram,descriptor(word))
                for x,y in ((0,0),(63,0),(0,63),(63,63),(17,38)):
                    u,v=maps[turn](63-x if flip&1 else x,63-y if flip&2 else y)
                    self.assertEqual(colors[y*64+x],1+(u+3*v)%16,(turn,flip,x,y))

    def test_floor_rejects_mixed_pattern_and_unsupported_kind(self):
        d=bytearray(descriptor());d[12]^=1
        with self.assertRaises(ValueError):stage.floor_tile(self.vram,d)
        d=bytearray(descriptor());struct.pack_into('<h',d,804,1)
        with self.assertRaises(ValueError):stage.floor_tile(self.vram,d)

    def test_arc_rejects_bad_extents_and_truncated_tims(self):
        with self.assertRaises(ValueError):stage.read_arc(bytes(24))
        bad=struct.pack('<5I',2,24,16,40,808)+bytes(828)
        with self.assertRaises(ValueError):stage.read_arc(bad)

    def test_tmd_rejects_invalid_object_vertices_and_packets(self):
        with self.assertRaises(ValueError):stage.primitives(bytes(1020))
        tmd=bytearray(1020);struct.pack_into('<3I',tmd,0,0x41,400,36)
        struct.pack_into('<7I',tmd,12,1000,2,0,0,1000,1,0)
        with self.assertRaises(ValueError):stage.primitives(tmd)


if __name__=='__main__':unittest.main()
