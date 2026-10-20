"""Synthetic KMD parser/skin/compiler checks; contains zero original game bytes."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import kmd_to_ska as kmd
from build_local_kmd_assets import skeleton_parts,generate
from audit_kmd_topology import audit,classify


class TopologyAuditTests(unittest.TestCase):
    def test_seams_cross_joint_faces_and_two_sided_faces_stay_distinct(self):
        report=classify([0,0,0,1],5,
            [(0,1,2,2),(0,1,2,2),(1,2,3,3),(0,1,4,4)],
            [False,True,False,False])
        self.assertEqual(report['face_classes'],dict(single_rigid_joint=2,
            single_rigid_joint_cullable=1,multiple_rigid_joints=1,
            multiple_rigid_joints_cullable=1,has_blend=1,has_blend_cullable=1))
        self.assertEqual(report['single_rigid_joint_referenced_vertices'],3)
        self.assertEqual(report['vertices_exclusive_to_cullable_single_joint_faces'],0)
        self.assertEqual(report['blend_vertices'],1)

    def test_invalid_topology_is_rejected(self):
        for faces,flags in [([(0,1,2,3)],[]), ([(0,1,2,4)],[False])]:
            with self.assertRaises(ValueError):classify([0,0,0,0],4,faces,flags)

def synthetic_kmd():
    # The 27-row header ends at 0x5F8. One row includes four rigid vertices,
    # one normal list, one FT4 face and an indexed texmap.
    start=0x10+27*56
    def u32(n):return struct.pack("<I",n)
    # PS1 quad strip order: top-left, top-right, bottom-left, bottom-right.
    coord=((0,0,0),(4096,0,0),(0,0,4096),(4096,0,4096))
    verts=(u32(2)+u32(0)+u32(0)+u32(4)+
           b"".join(struct.pack("<4h",*v,0) for v in coord)+u32(0)*6)
    normal=u32(4)+u32(0)+u32(0)+u32(0)+u32(0)*2
    face_word=(0<<2)|(1<<9)|(2<<16)|(3<<25)
    faces=u32(0)+u32(1)+struct.pack("<II",face_word,0)+u32(0)+u32(0)
    uvs=bytes((0,0,8,0,8,8,0,8))
    # Groups: FT3 count0; FT4 count1, material0, four UV indices;
    # GT3 count0; GT4 count0; zero padding to a four-byte boundary.
    material=struct.pack("<HHH",4,7,10)+uvs+bytes((0,1,0,0,1,2,3,0,0,0))
    assert len(material)==24,(len(material),material)
    start_normal=start+len(verts)
    start_faces=start_normal+len(normal)
    start_material=start_faces+len(faces)
    hdr=bytearray(start)
    struct.pack_into("<II4s",hdr,0,27,100,b"\0\0\0\0")
    hdr[8:12]=b"3DMK"
    # row 0 is root (no mesh); row 1 belongs to draw part 1, parent part 0.
    struct.pack_into("<I",hdr,0x10+8*4,0xFFFFFFFF)
    struct.pack_into("<14I",hdr,0x10+1*56,
                     start,0,start_normal,start_faces,start_material,
                     100,0,0,0,0,0,0,1,0)
    return bytes(hdr)+verts+normal+faces+material

class KmdTests(unittest.TestCase):
    def test_empty_or_differently_scaled_override_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);primary=root/'primary.kmd';override=root/'override.kmd'
            primary.write_bytes(synthetic_kmd());override.write_bytes(b'')
            with self.assertRaises(ValueError):
                generate(primary,root,root/'out',actor0_kmd=override)
            other=bytearray(synthetic_kmd());struct.pack_into('<I',other,4,101)
            override.write_bytes(other)
            with self.assertRaisesRegex(ValueError,'same scale'):
                generate(primary,root,root/'out',actor0_kmd=override)
            self.assertFalse((root/'out').exists())

    def test_skeleton_keeps_each_models_offsets_and_loader_z_fixup(self):
        first=bytearray(synthetic_kmd());second=bytearray(first)
        row=kmd.PART_ROWS[6]
        struct.pack_into('<3i',first,0x10+row*56+20,31,-7,-45)
        struct.pack_into('<3i',second,0x10+row*56+20,31,-7,-46)
        a=skeleton_parts(kmd.parse_kmd(bytes(first)))
        b=skeleton_parts(kmd.parse_kmd(bytes(second)))
        self.assertEqual(a[6],(0,kmd.PART_MATRICES[6],(31,-7,45)))
        self.assertEqual(b[6],(0,kmd.PART_MATRICES[6],(31,-7,46)))
        self.assertEqual(a[:6]+a[7:],b[:6]+b[7:])

    def test_skeleton_rejects_forward_parent(self):
        data=bytearray(synthetic_kmd())
        struct.pack_into('<i',data,0x10+kmd.PART_ROWS[6]*56+32,6)
        with self.assertRaisesRegex(ValueError,'parent'):
            skeleton_parts(kmd.parse_kmd(bytes(data)))

    def test_meshless_root_texmap_has_no_trailer(self):
        block = struct.pack('<HH4B', 2, 2, 0, 0, 0, 0)
        self.assertEqual(kmd.parse_texmap(block, 0, len(block), (0, 0, 0, 0)), ())

    def test_parse_fixture(self):
        data=synthetic_kmd()
        parsed=kmd.parse_kmd(data)
        self.assertEqual(parsed.scale_percent,100)
        self.assertEqual(len(parsed.rows),27)
        self.assertEqual(len(parsed.rows[1].vert.coords),4)
        self.assertEqual(parsed.rows[1].faces,((0,1,2,3),))
        self.assertEqual(parsed.rows[1].materials,(7,))
        _,refs,variants=kmd.record_slots(parsed)
        self.assertEqual(refs,[(1,1,(0,1,2,3))])
        self.assertEqual(variants,0)

    def test_quad_two_sided_flag_is_not_a_vertex_index_bit(self):
        data=bytearray(synthetic_kmd())
        face_base=kmd.KMD_HEADER+kmd.ROWS*kmd.ROW_BYTES+72+24
        struct.pack_into('<I',data,face_base+12,0x1000000)
        row=kmd.parse_kmd(data).rows[1]
        self.assertEqual(row.double_sided,(1,))
        self.assertEqual(row.faces,((0,1,2,3),))

    def test_reject_bad_signature_and_index(self):
        data=bytearray(synthetic_kmd());data[8]=0
        with self.assertRaises(kmd.KmdError):kmd.parse_kmd(bytes(data))
        data=bytearray(synthetic_kmd())
        # The fourth slot is encoded at bits 25..31; 7 > own slot count 4.
        pos=kmd.KMD_HEADER+kmd.ROWS*kmd.ROW_BYTES+72+24+4
        data[pos+4:pos+8]=struct.pack("<I",((0<<2)|(1<<9)|(2<<16)|(7<<25)))
        with self.assertRaises(kmd.KmdError):kmd.record_slots(kmd.parse_kmd(bytes(data)))

    def test_blended_seam_exchange(self):
        empty=((),(),(),(),(),())
        rows=[kmd.KmdRow(i,(0,)*14,None,(),(),()) for i in range(27)]
        # The exported half of row 1's own vertex is imported by the second
        # chest mesh row (row 2), which adds half its local vertex to it.
        a=kmd.VertBlock((),(),((32,0,0),),((),(),(),(0x102,),(),()))
        b=kmd.VertBlock((),(2,),((96,0,0),),((),(0x102,),(),(),(),()))
        rows[1]=kmd.KmdRow(1,(0,)*14,a,(),(),())
        rows[2]=kmd.KmdRow(2,(0,)*14,b,((1,1,1),),(),())
        slots,refs,_=kmd.record_slots(kmd.Kmd(100,tuple(rows)))
        self.assertEqual(refs,[(1,2,(1,1,1))])
        self.assertEqual(slots[1,2][1],{(1,1,0):0.5,(1,2,0):0.5})

    def test_compile_ska_with_libsaturn(self):
        checkout=os.environ.get("LIBSATURN_SOURCE_DIR")
        if not checkout or not (Path(checkout)/"tools/model_pipeline/skeletal_export.py").exists():
            self.skipTest("Pass LIBSATURN_SOURCE_DIR to test the real LibSaturn SKA1 exporter")
        with tempfile.TemporaryDirectory() as root:
            dest=Path(root)
            parsed=kmd.parse_kmd(synthetic_kmd())
            model,quads,_=kmd.to_source_model(parsed,Path(checkout))
            self.assertEqual(quads,[(0,1,3,2)])
            self.assertEqual(model.triangles,[(0,1,2),(1,3,2)])
            source=dest/'fixture.kmd';source.write_bytes(synthetic_kmd())
            topology=audit(source,Path(checkout))
            self.assertEqual((topology['vertices'],topology['faces']),(4,1))
            self.assertEqual(topology['face_classes'],
                dict(single_rigid_joint=1,single_rigid_joint_cullable=1))
            self.assertEqual(topology['single_rigid_joint_referenced_vertices'],4)
            self.assertEqual(topology['vertices_exclusive_to_cullable_single_joint_faces'],4)
            self.assertEqual(topology['maximum_influence_position_l1'],32768)
            self.assertEqual(topology['position_shift'],14)
            self.assertEqual(topology['q12_rotation_round_bias'],512)
            result=kmd.compile_ska(parsed,Path(checkout),dest)
            self.assertEqual(result["format"],"SKA1")
            self.assertEqual(result["joints"],22)
            self.assertEqual(result["faces"],1)
            self.assertEqual(result["logical_vertices"],4)
            data=(dest/"fighter_bind.ska").read_bytes()
            self.assertEqual(data[:4],b"SKA1")
            self.assertEqual(data[4:8],struct.pack(">I",1))
            self.assertEqual(len(data),result["archive_bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(),result["archive_sha256"])
            self.assertEqual(result["clip"],["bind"])
            self.assertTrue((dest/"fighter_bind.obj").exists())

    def test_file_offsets_are_loaded_before_saturn_basis_conversion(self):
        checkout=os.environ.get("LIBSATURN_SOURCE_DIR")
        if not checkout:
            self.skipTest("Pass LIBSATURN_SOURCE_DIR for SourceModel integration")
        data=bytearray(synthetic_kmd())
        # Original FighterSetupPart loads (x,y,-z), then the renderer uses
        # D=(1,-1,-1). Rest translations therefore become (x,-y,+z).
        struct.pack_into('<3i',data,kmd.KMD_HEADER+kmd.ROW_BYTES+5*4,
                         123,-456,789)
        model,_,_=kmd.to_source_model(kmd.parse_kmd(bytes(data)),Path(checkout))
        self.assertEqual(model.nodes[1].translation,
                         (123/4096,456/4096,789/4096))

if __name__=="__main__":unittest.main()
