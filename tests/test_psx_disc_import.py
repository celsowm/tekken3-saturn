"""Synthetic PS1 ISO9660 and stage TMD tests, no commercial game data."""
import hashlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
import psx_disc_import as t3

def entry(name,lba,size,directory=False):
    n = 33+len(name)
    if n%2:n+=1
    data=bytearray(n)
    data[0]=n;data[2:6]=struct.pack("<I",lba)
    data[10:14]=struct.pack("<I",size)
    data[25]=2 if directory else 0
    data[32]=len(name)
    data[33:33+len(name)]=name
    return bytes(data)

def fixture(banks=()):
    exe=bytearray(0x16000);exe[:8]=b"PS-X EXE"
    struct.pack_into("<I",exe,0x10,0x80079c70)
    struct.pack_into("<II",exe,0x18,0x80010000,0x121000)
    for i in range(303):
        struct.pack_into("<II",exe,0x14c00+i*8,
                         0 if i<=56 else 1 if i<=71 else 2,
                         0x424 if i==56 else 16 if i==71 else 0)
    stage=bytearray(0x424)
    struct.pack_into("<III",stage,0,0x41,480,36)
    for i in range(36):
        struct.pack_into("<7I",stage,12+i*28,
                         0x404-12,4 if i==0 else 0,
                         0,0,0x3fc-12,1 if i==0 else 0,0)
    stage[0x3fc:0x404]=bytes([128,128,128,0x28,0,1,2,3])
    for i,v in enumerate(((0,0,0),(4096,0,0),(4096,0,4096),(0,0,4096))):
        struct.pack_into("<hhhh",stage,0x404+i*8,*v,0)
    bns=bytearray(4096);bns[:len(stage)]=stage;bns[2056:2060]=b"3DMK"
    for i, bank in enumerate(banks, 72):
        struct.pack_into("<II",exe,0x14c00+i*8,len(bns)//2048,len(bank))
        bns += bank
        bns += bytes(-len(bns)%2048)
    for i in range(72+len(banks),303):
        struct.pack_into("<II",exe,0x14c00+i*8,len(bns)//2048,0)
    sectors=[bytearray(2048) for _ in range(max(80,68+len(bns)//2048))]
    sectors[16][:7]=b"\x01CD001\x01"
    sectors[16][156:190]=entry(b"\x00",20,2048,True)
    d=entry(b"\x00",20,2048,True)+entry(b"TEKKEN3",21,2048,True)
    sectors[20][:len(d)]=d
    d=(entry(b"\x00",21,2048,True)+
       entry(b"SLUS_004.02;1",23,len(exe))+
       entry(b"TEKKEN3.BNS;1",68,len(bns)))
    sectors[21][:len(d)]=d
    for off in range(0,len(exe),2048):
        chunk=exe[off:off+2048]
        sectors[23+off//2048][:len(chunk)]=chunk
    for offset in range(0,len(bns),2048):
        sectors[68+offset//2048][:]=bns[offset:offset+2048]
    return sectors,bytes(exe)

class ImportTests(unittest.TestCase):
    def test_discover_and_compact_private_divmot_banks(self):
        from test_divmot_to_logic import fake_bank
        for kinds in ((0,99),(0,0)):
            sectors,exe=fixture([fake_bank(k) for k in kinds])
            with self.subTest(kinds=kinds),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)
                source=root/"disc.iso"
                source.write_bytes(b"".join(sectors))
                args=(source,root/"out")
                options={"expected_sha":hashlib.sha256(exe).hexdigest(),"divmot_banks":True}
                report=t3.import_game(*args,**options)
                self.assertEqual([b["bank_type"] for b in report["divmot_banks"]],list(kinds))
                self.assertEqual(report["records"][72]["kind"],"DIVMOT")
                self.assertEqual(report["records"][73]["bank_type"],kinds[1])
                self.assertFalse((root/"out/raw").exists())
                for record,kind in enumerate(kinds,72):
                    data=(root/f"out/divmot/{record:03}/bank_{kind}.t3logic").read_bytes()
                    self.assertEqual(t3.compact(data,True,True,True)[0],data)
                # Default import does not extract authored banks.
                t3.import_game(source,root/"default",expected_sha=options["expected_sha"])
                self.assertFalse((root/"default/divmot").exists())

    def test_divmot_candidate_requires_directory_and_record_length(self):
        from test_divmot_to_logic import fake_bank
        data=fake_bank(0)
        self.assertTrue(t3.divmot_candidate(data[:64],len(data)))
        self.assertFalse(t3.divmot_candidate(data[:63],len(data)))
        self.assertFalse(t3.divmot_candidate(data[:64],len(data)+1))
        bad=bytearray(data)
        struct.pack_into("<I",bad,8,63)  # recognized header, invalid directory
        sectors,exe=fixture([bad])
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            source=root/"bad.iso"
            source.write_bytes(b"".join(sectors))
            with self.assertRaisesRegex(t3.FormatError,"invalid DIVMOT BNS record 72"):
                t3.import_game(source,root/"out",expected_sha=hashlib.sha256(exe).hexdigest(),
                               divmot_banks=True)

    def test_archive_listing_url(self):
        expected = "https://archive.org/download/tekken3_playstation/Tekken%203%20%28USA%29.zip"
        listing = ("https://ia601000.us.archive.org/view_archive.php?archive="
                   "/33/items/tekken3_playstation/Tekken%203%20%28USA%29.zip")
        self.assertEqual(t3.archive_zip_url(listing), expected)
        self.assertEqual(t3.archive_zip_url(expected), expected)
        self.assertEqual(t3.archive_zip_url(expected.replace(
            "archive.org/", "ia601000.us.archive.org/")), expected)
        for bad in (
            listing.replace("https:", "http:"),
            listing.replace("ia601000.us.archive.org", "archive.org.example.com"),
            listing.replace("ia601000.us.archive.org", "user@archive.org"),
            listing + "&archive=/33/items/another/file.zip",
            listing + "&file=Track%201.bin",
            listing.replace("/tekken3_playstation/", "/../"),
            listing.replace(".zip", ".bin"),
            expected + "#fragment", expected + "?download=1",
            expected.replace("/download/", "/other/"),
            expected.replace("tekken3_playstation", ".."),
            expected.replace("tekken3_playstation", "bad%2Fitem"),
        ):
            with self.subTest(url=bad), self.assertRaises(t3.FormatError):
                t3.archive_zip_url(bad)

    def test_remote_listing_download_and_cache(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("Track 1.bin", b"synthetic track")
        url = "https://ia601000.us.archive.org/view_archive.php?archive=/33/items/item/game.zip"
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)/"game.zip"
            with mock.patch.object(t3.urllib.request, "urlopen",
                                   return_value=io.BytesIO(buf.getvalue())) as request:
                self.assertEqual(t3.remote_zip(url, cache), cache)
                request.assert_called_once_with(
                    "https://archive.org/download/item/game.zip", timeout=60)
            with mock.patch.object(t3.urllib.request, "urlopen") as request:
                self.assertEqual(t3.remote_zip(url, cache), cache)
                request.assert_not_called()
                cache.write_bytes(b"partial download")
                with self.assertRaises(t3.FormatError):
                    t3.remote_zip(url, cache)
                request.assert_not_called()

    def test_remote_invalid_download_cleanup(self):
        with tempfile.TemporaryDirectory() as root:
            cache = Path(root)/"game.zip"
            for response in (b"<html>not a zip</html>", b""):
                with mock.patch.object(t3.urllib.request, "urlopen",
                                       return_value=io.BytesIO(response)):
                    with self.assertRaises(t3.FormatError):
                        t3.remote_zip("https://archive.org/download/item/game.zip", cache)
                self.assertFalse(cache.exists())
                self.assertFalse(cache.with_suffix(".zip.part").exists())

    def test_ids(self):
        self.assertEqual(t3.selected_ids("56,71,73-74"),[56,71,73,74])
        for bad in ("1-0","303","foo"):
            with self.assertRaises(t3.FormatError):
                t3.selected_ids(bad)

    def test_track_formats_and_zip(self):
        sectors,exe=fixture()
        for raw in (False,True):
            with self.subTest(raw=raw),tempfile.TemporaryDirectory() as root_:
                root=Path(root_)
                name=root/("Tekken 3 USA (Track 1).bin" if raw else "track.iso")
                name.write_bytes(b"".join(
                    b"\0"*24+bytes(s)+b"\0"*280 if raw else bytes(s)
                    for s in sectors))
                with t3.Disc(name) as disc:
                    self.assertEqual(disc.width,2352 if raw else 2048)
                    entry_=disc.find("TEKKEN3","SLUS_004.02")
                    self.assertEqual(disc.span(entry_,0,entry_[2]),exe)
                    with self.assertRaises(t3.FormatError):
                        t3.parse_bns_index(exe,4096)
                source=name
                if raw:
                    source=root/"source.zip"
                    with zipfile.ZipFile(source,"w",zipfile.ZIP_DEFLATED) as z:
                        z.write(name,arcname="Tekken 3 USA (Track 1).bin")
                result=t3.import_game(
                    source,root/"import",[71],[56],None,hashlib.sha256(exe).hexdigest())
                self.assertEqual(len(result["records"]),303)
                self.assertEqual(result["records"][71]["kind"],"KMD/3DMK")
                self.assertEqual(result["records"][56]["geometry"]["quads"],1)
                self.assertIn("f 1 2 3 4",(root/"import/stages/stg_a.obj").read_text())
                self.assertTrue((root/"import/raw/071.bin").exists())

    def test_unsafe_zip(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/"bad.zip"
            with zipfile.ZipFile(p,"w") as z:
                z.writestr("../Track 1.bin",b"x")
            with zipfile.ZipFile(p) as z:
                with self.assertRaises(t3.FormatError):
                    t3.zip_track(z)

if __name__=="__main__":
    unittest.main()
