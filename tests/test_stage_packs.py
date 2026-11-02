"""Synthetic stage parsing and orientation tests; no retail fixtures."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import build_stage_packs as stage


def descriptor(word=8<<16,clut=480<<6,kind=0):
    return struct.pack('<I',128)+struct.pack('<II',word,clut<<16)*100+struct.pack('<hh',kind,-51)


def checker(columns):
    """A descriptor whose entries repeat every `columns` columns, on pages 8 and 9."""
    entries=[((8+c%columns)<<16,480<<22) for r in range(10) for c in range(10)]
    return struct.pack('<I',128)+b''.join(struct.pack('<II',*e) for e in entries)+struct.pack('<hh',0,-51)


class StageTests(unittest.TestCase):
    def setUp(self):
        self.vram=[0]*(1024*512)
        self.vram[480*1024:480*1024+16]=range(1,17)
        for y in range(64):
            for x in range(0,64,4):
                self.vram[y*1024+512+x//4]=sum(((x+i+3*y)%16)<<(4*i) for i in range(4))

    def colours(self,word):
        return stage.floor_entry(self.vram,word,480<<22)

    def test_floor_bitmap_repeats_one_tile(self):
        indices,palette,cut=stage.floor_bitmap(self.vram,descriptor())
        self.assertEqual(len(indices),stage.FLOOR_W*stage.FLOOR_H)
        self.assertFalse(cut)
        self.assertEqual(palette[0],0)
        for y,x in ((0,0),(5,7),(63,63),(64,0),(0,64),(255,511)):
            self.assertEqual(palette[indices[y*stage.FLOOR_W+x]],1+(x%64+3*(y%64))%16)

    def test_floor_bitmap_entry_offset(self):
        d=bytearray(descriptor())
        struct.pack_into('<II',d,4+(5*10+6)*8,9<<16,480<<22)
        indices,_,_=stage.floor_bitmap(self.vram,d)
        # Bitmap tile (1, 0) takes entry column 6, row 5; page 9's texels are all equal.
        self.assertEqual(len(set(indices[64:128])),1)
        self.assertGreater(len(set(indices[0:64])),1)
        self.assertGreater(len(set(indices[128:192])),1)

    def test_floor_bitmap_keeps_index_zero_transparent(self):
        self.vram[480*1024+5]=0
        indices,palette,_=stage.floor_bitmap(self.vram,descriptor())
        self.assertEqual(indices[5],0)
        self.assertEqual(palette[0],0)
        self.assertNotIn(0,palette[1:])

    def test_floor_bitmap_reports_patterns_it_cuts(self):
        self.assertFalse(stage.floor_bitmap(self.vram,checker(2))[2])
        self.assertTrue(stage.floor_bitmap(self.vram,checker(5))[2])

    def test_floor_bitmap_applies_the_level(self):
        _,full,_=stage.floor_bitmap(self.vram,descriptor())
        _,half,_=stage.floor_bitmap(self.vram,descriptor(),64)
        self.assertEqual(half,[stage.scale_colour(c,64) for c in full])
        self.assertEqual(stage.scale_colour(0x8000|16|8<<5|31<<10,64),0x8000|8|4<<5|15<<10)

    def test_spot_level_is_dim_only_for_spotlights(self):
        self.assertEqual(stage.spot_level(0),128)
        self.assertLess(stage.spot_level(1),255)
        self.assertLess(stage.spot_level(2,64),65)

    def test_all_floor_rotations_and_reflections(self):
        # Four rotations of the unmodified tile, then flips of each axis.
        maps=(lambda x,y:(x,y),lambda x,y:(y,63-x),
              lambda x,y:(63-x,63-y),lambda x,y:(63-y,x))
        for turn in range(4):
            for flip in range(4):
                colors=self.colours((8<<16)|(turn<<30)|(flip<<25))
                for x,y in ((0,0),(63,0),(0,63),(63,63),(17,38)):
                    u,v=maps[turn](63-x if flip&1 else x,63-y if flip&2 else y)
                    self.assertEqual(colors[y*64+x],1+(u+3*v)%16,(turn,flip,x,y))

    def test_floor_rejects_bad_descriptor(self):
        with self.assertRaises(ValueError):stage.floor_bitmap(self.vram,descriptor()[:-1])
        d=bytearray(descriptor());d[0]^=1
        with self.assertRaises(ValueError):stage.floor_bitmap(self.vram,d)

    def test_pack_layout(self):
        floor,picture=bytes(stage.FLOOR_W*stage.FLOOR_H),bytes(stage.COLUMNS*stage.ROWS)
        out=stage.pack(3,1200,0x8000,0x8123,39,[0,0x8001],floor,[0,0x8002],picture)
        self.assertEqual(len(out),stage.HEAD_BYTES+len(floor)+len(picture))
        self.assertEqual(struct.unpack_from('>7I',out),
                         (int(stage.FORMAT['MAGIC'],16),int(stage.FORMAT['VERSION']),3,1200,0x8000,0x8123,39))
        self.assertEqual(struct.unpack_from('>H',out,stage.FLOOR_PALETTE_AT+2)[0],0x8001)
        self.assertEqual(struct.unpack_from('>H',out,stage.PICTURE_PALETTE_AT+2)[0],0x8002)
        with self.assertRaises(ValueError):stage.pack(3,1200,0,0,39,[0],floor[:-1],[0],picture)

    def test_arc_rejects_bad_extents_and_truncated_tims(self):
        with self.assertRaises(ValueError):stage.read_arc(bytes(24))
        bad=struct.pack('<5I',2,24,16,40,808)+bytes(828)
        with self.assertRaises(ValueError):stage.read_arc(bad)

    def test_tmd_rejects_invalid_object_vertices_and_packets(self):
        with self.assertRaises(ValueError):stage.primitives(bytes(1020))
        tmd=bytearray(1020);struct.pack_into('<3I',tmd,0,0x41,400,36)
        struct.pack_into('<7I',tmd,12,1000,2,0,0,1000,1,0)
        with self.assertRaises(ValueError):stage.primitives(tmd)

    def test_box_filter_means_only_opaque_samples(self):
        import numpy as np
        red,blue=0x8000|31,0x8000|(31<<10)
        # One row of two texels, 2 x 2 samples each, stored row-major.
        samples=np.array([red,blue,red,0,
                          red,blue,red,0])
        got=stage.box_filter(samples,2,1,2)
        # Half red and half blue, then red with no darkening from the 0 samples
        # (at half coverage the second texel stays opaque).
        self.assertEqual(int(got[0]),0x8000|16|(16<<10))
        self.assertEqual(int(got[1]),red)
        self.assertEqual(int(stage.box_filter(np.array([red,0,0,0]),1,1,2)[0]),0)


if __name__=='__main__':unittest.main()
