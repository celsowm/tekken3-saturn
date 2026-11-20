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


    def cylinder(self):
        """A synthetic cylinder: sky rows, then bands of a few colours with a stripe pattern."""
        import numpy as np
        rows,columns=stage.PANO_ROWS,stage.PANO_COLUMNS
        y,x=np.mgrid[0:rows,0:columns]
        colour=np.zeros((rows,columns),np.int64)
        band=y>=64
        palette=np.array([0x8000|(r|g<<5|b<<10) for r,g,b in
                          ((31,0,0),(0,31,0),(0,0,31),(20,20,0),(0,20,20),(10,10,10),(25,5,25),(5,25,5),(31,31,31))])
        colour[band]=palette[((x//8+y//8+(x//3)%2)%len(palette))[band]]
        return colour

    def test_cells_share_characters_and_keep_the_palette_budget(self):
        import numpy as np
        import stage_cells
        got=stage_cells.cell_picture(self.cylinder(),stage.PANO_COLUMNS,stage.PANO_ROWS,stage.MAX_CHARS)
        chars,names,palette_of=got['chars'],got['names'],got['palette_of']
        self.assertEqual(names.shape,(stage.PANO_ROWS//8,stage.PANO_COLUMNS//8))
        self.assertTrue(not chars[0].any(),'character 0 is the transparent cell')
        self.assertTrue((names[:8]==0).all(),'the sky rows are character 0')
        self.assertLess(len(chars),names.size//2,'equal cells share a character')
        self.assertLessEqual(int(palette_of.max()),stage_cells.PALETTES-1)
        self.assertLessEqual(int(chars.max()),stage_cells.PALETTE_COLOURS)
        self.assertEqual(got['palettes'].shape,(stage_cells.PALETTES,16))
        self.assertTrue((got['palettes'][:,0]==0).all(),'dot 0 of every palette is transparent')
        self.assertLess(got['rms'],0.5,'nine colours fit one palette exactly')

    def test_cells_merge_down_to_the_character_limit(self):
        import numpy as np
        import stage_cells
        rng=np.random.default_rng(5)
        colour=self.cylinder()
        # Noise in the lower half makes nearly every cell unique.
        noisy=rng.random(colour[200:].shape)<0.5
        colour[200:]=np.where(noisy,0x8000|0x7fff&int(rng.integers(0,0x7fff)),colour[200:])
        full=stage_cells.cell_picture(colour,stage.PANO_COLUMNS,stage.PANO_ROWS,1<<20)
        limit=len(full['chars'])-300
        capped=stage_cells.cell_picture(colour,stage.PANO_COLUMNS,stage.PANO_ROWS,limit)
        self.assertEqual(len(capped['chars']),limit)
        self.assertTrue(not capped['chars'][0].any())
        self.assertLess(int(capped['names'].max()),limit)

    def test_cell_pack_layout(self):
        import numpy as np
        import stage_cells
        cells=stage_cells.cell_picture(self.cylinder(),stage.PANO_COLUMNS,stage.PANO_ROWS,stage.MAX_CHARS)
        floor=bytes(stage.FLOOR_W*stage.FLOOR_H)
        out=stage.pack_cells(3,1200,0x8000,0x8123,39,[0,0x8001],floor,cells)
        chars=len(cells['chars'])
        names_at=stage.HEAD_BYTES+len(floor)
        chars_at=names_at+stage.PANO_COLUMNS//8*(stage.PANO_ROWS//8)*4
        self.assertEqual(len(out)%stage.HEAD_BYTES,0)
        self.assertEqual(len(out),chars_at+(chars*32+stage.HEAD_BYTES-1)//stage.HEAD_BYTES*stage.HEAD_BYTES)
        self.assertEqual(struct.unpack_from('>8I',out),
                         (int(stage.FORMAT['MAGIC'],16),int(stage.FORMAT['CELL_VERSION']),3,1200,0x8000,0x8123,39,chars))
        self.assertEqual(struct.unpack_from('>H',out,stage.FLOOR_PALETTE_AT+2)[0],0x8001)
        # A name: palette number, then the character number; the characters are 4 bits a dot, left dot high.
        at=(10*(stage.PANO_COLUMNS//8)+20)*4
        number=int(cells['names'][10,20])
        self.assertEqual(struct.unpack_from('>HH',out,names_at+at),
                         (stage.PALETTE_NUMBER+int(cells['palette_of'][number]),number))
        dots=cells['chars'][number]
        self.assertEqual(out[chars_at+32*number],(int(dots[0])<<4)|int(dots[1]))
        self.assertEqual(out[chars_at+32*number+31],(int(dots[62])<<4)|int(dots[63]))
        with self.assertRaises(ValueError):stage.pack_cells(3,1200,0,0,39,[0],floor[:-1],cells)


if __name__=='__main__':unittest.main()
