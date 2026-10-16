"""Synthetic TIM/ARC placement, palettes and quad UV topology."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from psx_character_textures import load_character_vram, sample, bake_face


def arc_fixture(depth=4):
    colors=[0,0x1f,0x3e0,0x7c00]+[0]*((16 if depth==4 else 256)-4)
    clut=struct.pack('<I4H',12+2*len(colors),0,0,len(colors),1)+struct.pack('<'+'H'*len(colors),*colors)
    image=struct.pack('<I4HH',14,0,0,1,1,0x3210 if depth==4 else 0x0201)
    member=struct.pack('<II',0x10,8 if depth==4 else 9)+clut+image+bytes(4)
    header=bytearray(48)
    struct.pack_into('<I',header,0,5)
    struct.pack_into('<II',header,4,48,len(member))
    for i in range(1,5):struct.pack_into('<II',header,4+8*i,48+len(member),0)
    return bytes(header)+member


class Textures(unittest.TestCase):
    def test_upload_placement_and_4bit_clut(self):
        vram,count=load_character_vram(arc_fixture())
        self.assertEqual(count,1)
        self.assertEqual(vram[384],0x3210)
        self.assertEqual(vram[504*1024+1],0x1f)
        self.assertEqual(sample(vram,0,0,0),(0,0,0,0))
        self.assertEqual(sample(vram,0,1,0),(255,0,0,255))
        self.assertEqual(sample(vram,0,2,0),(0,255,0,255))
        self.assertEqual(sample(vram,0,3,0),(0,0,255,255))

    def test_8bit_and_strip_to_perimeter_bake(self):
        vram,_=load_character_vram(arc_fixture(8))
        self.assertEqual(sample(vram,0x8000,0,0),(255,0,0,255))
        self.assertEqual(sample(vram,0x8000,1,0),(0,255,0,255))
        vram,_=load_character_vram(arc_fixture())
        rgba=bake_face(vram,0,((1,0),(3,0),(1,0),(3,0)),2,2)
        self.assertEqual(rgba,[(255,0,0,255),(0,0,255,255)]*2)
        tri=bake_face(vram,0,((1,0),(3,0),(2,0)),2,2)
        self.assertEqual(tri[2:],[(0,255,0,255)]*2)

    def test_reject_unsupported_or_truncated_sources(self):
        raw=arc_fixture()
        for changed in (raw[:-1],raw[:48]+struct.pack('<I',0x11)+raw[52:]):
            with self.assertRaises(ValueError):load_character_vram(changed)
        vram,_=load_character_vram(raw)
        with self.assertRaises(ValueError):sample(vram,0x10000,0,0)


if __name__=='__main__':unittest.main()
