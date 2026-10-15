#!/usr/bin/env python3
"""Local PS1 Tekken 3 USA (SLUS-00402) BNS inventory and stage geometry preview.

Inputs: a user-supplied CD-ROM Mode 2/2352 Track 1 BIN, cooked 2048 ISO or
ZIP holding Track 1, optionally downloaded explicitly from archive.org.
This is an offline tool and is not invoked by the Saturn build or CI.
Game data/output must stay local, ignored by Git.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tempfile
import urllib.parse
import urllib.request
import zipfile

US_EXE_SHA = "fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c"
STAGE_NAMES = "a b c d e f g n i j k l m t u".split()
SECTOR = 2048

class FormatError(ValueError):
    pass

class Disc:
    """ISO9660 logical 2048-byte sectors from 2352-byte PS1 Form 1 or ISO."""
    def __init__(self, path):
        self.file = path.open("rb")
        self.length = path.stat().st_size
        self.width = self.offset = 0
        for width, offset in ((2352, 24), (2352, 16), (2048, 0)):
            if self.length % width:
                continue
            self.file.seek(16 * width + offset)
            if self.file.read(7) == b"\x01CD001\x01":
                self.width, self.offset = width, offset
                break
        if not self.width:
            self.file.close()
            raise FormatError("not a recognized 2352-byte Form 1 track or 2048-byte ISO")
        pvd = self.sectors(16, 1)
        self.volume = pvd[40:72].decode("ascii", "replace").strip()
        roots = self.directory(pvd[156:156 + pvd[156]])
        if len(roots) != 1 or not roots[0][3]:
            raise FormatError("missing ISO9660 root record")
        self.root = roots[0]

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.file.close()

    def sectors(self, start, count):
        if start < 0 or count < 0 or count > 16384 or (start+count)*self.width > self.length:
            raise FormatError("out-of-range CD read")
        buf = bytearray()
        for lba in range(start, start+count):
            self.file.seek(lba*self.width+self.offset)
            block = self.file.read(SECTOR)
            if len(block) != SECTOR:
                raise FormatError("short CD read")
            buf.extend(block)
        return bytes(buf)

    @staticmethod
    def directory(data):
        found = []
        pos = 0
        while pos < len(data):
            size = data[pos]
            if not size:
                pos = (pos//SECTOR+1)*SECTOR
                continue
            if size < 34 or pos+size > len(data) or 33+data[pos+32] > size:
                raise FormatError("invalid ISO directory entry")
            name = data[pos+33:pos+33+data[pos+32]]
            name = ("." if name == b"\x00" else ".." if name == b"\x01"
                    else name.decode("ascii", "replace").split(";", 1)[0])
            found.append((name,
                          struct.unpack_from("<I", data, pos+2)[0],
                          struct.unpack_from("<I", data, pos+10)[0],
                          bool(data[pos+25] & 2)))
            pos += size
        return found

    def span(self, entry, offset, size):
        if offset < 0 or size < 0 or offset+size > entry[2]:
            raise FormatError("ISO file read outside extent")
        if not size:
            return b""
        first = offset//SECTOR
        last = (offset+size+SECTOR-1)//SECTOR
        data = self.sectors(entry[1]+first, last-first)
        start = offset%SECTOR
        return data[start:start+size]

    def find(self, *parts):
        record = self.root
        for name in parts:
            if not record[3] or record[2] > 1024*1024:
                raise FormatError("invalid or oversized ISO directory")
            entries = self.directory(self.span(record, 0, record[2]))
            record = next((x for x in entries if x[0].casefold() == name.casefold()), None)
            if record is None:
                raise FormatError("missing ISO path: " + "/".join(parts))
        return record

def selected_ids(value):
    out = set()
    for item in filter(None, (i.strip() for i in value.split(","))):
        parts = item.split("-")
        if len(parts) > 2 or not all(p.isdecimal() for p in parts):
            raise FormatError("invalid ID range: " + item)
        start, end = int(parts[0]), int(parts[-1])
        if start > end or end >= 303:
            raise FormatError("BNS record IDs must be in [0,302]")
        out.update(range(start, end+1))
    return sorted(out)

def parse_bns_index(exe, bns_length, expected_sha=US_EXE_SHA):
    sha = hashlib.sha256(exe).hexdigest()
    if sha != expected_sha or exe[:8] != b"PS-X EXE":
        raise FormatError("unsupported Tekken 3 USA revision, executable SHA256=" + sha)
    if len(exe) < 0x14c00 + 303*8 or (
        struct.unpack_from("<I", exe, 0x10)[0],
        *struct.unpack_from("<II", exe, 0x18)
    ) != (0x80079c70, 0x80010000, 0x121000):
        raise FormatError("unexpected SLUS_004.02 executable header or index size")
    entries = []
    end = 0
    for i in range(303):
        lba, size = struct.unpack_from("<II", exe, 0x14c00 + i*8)
        off = lba*SECTOR
        if off < end or off+size > bns_length:
            raise FormatError(f"record {i} overlaps or exceeds BNS")
        entries.append((lba, off, size))
        end = off+size
    return sha, entries

def identify(data):
    if not data:
        return "empty"
    if data[8:12] == b"3DMK":
        return "KMD/3DMK"
    if data[:4] == b"pBAV":
        return "VAB"
    if data[:4] == b"\x10\x00\x00\x00":
        return "possible TIM"
    return "unknown"

def parse_stage_tmd(data, number):
    """Tekken 3 36-object panorama profile, NOT generic PlayStation TMD."""
    if not 56 <= number <= 70 or len(data) < 0x3fc:
        raise FormatError("invalid stage TMD record")
    marker, cell, objects = struct.unpack_from("<III", data, 0)
    if marker != 0x41 or not 0 < cell < 10000 or objects != 36:
        raise FormatError("unrecognized stage header")
    vertices, faces = [], []
    for obj in range(36):
        vertex_pos, count, _, _, prim_pos, prim_count, _ = struct.unpack_from(
            "<7I", data, 12+obj*28)
        vertex_pos += 12
        prim_pos += 12
        if count > 4096 or prim_count > 4096 or vertex_pos+count*8 > len(data) \
                or not 0x3fc <= prim_pos <= len(data):
            raise FormatError("stage object table out of bounds")
        base = len(vertices)+1
        for j in range(count):
            x, y, z, pad = struct.unpack_from("<hhhh", data, vertex_pos+j*8)
            if pad:
                raise FormatError("nonzero panorama vertex padding")
            vertices.append((x, y, z))
        for j in range(prim_count):
            if prim_pos+8 > len(data):
                raise FormatError("truncated panorama polygon")
            op = data[prim_pos+3]
            corner_count, length = {
                0x24:(3,20), 0x28:(4,8), 0x2c:(4,20)
            }.get(op, (0,0))
            if not corner_count or prim_pos+length > len(data):
                raise FormatError("unrecognized panorama polygon")
            indices_start = prim_pos+(4 if op == 0x28 else 16)
            indices = data[indices_start:indices_start+corner_count]
            if any(k >= count for k in indices):
                raise FormatError("panorama vertex index out of range")
            faces.append((obj, [base+k for k in indices]))
            prim_pos += length
    return {
        "stage_id":number, "objects":objects, "vertices":len(vertices),
        "triangles":sum(len(v)==3 for _,v in faces),
        "quads":sum(len(v)==4 for _,v in faces),
        "materials":"unconverted (stage ARC/TIM step pending)"
    }, vertices, faces

def write_stage_obj(path, vertices, faces):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write("# PS1 stage panorama preview. Materials and screen placement not yet converted.\n")
        for x,y,z in vertices:
            f.write(f"v {x/4096:.7f} {y/4096:.7f} {z/4096:.7f}\n")
        for obj,indices in faces:
            f.write(f"f {' '.join(str(k) for k in indices)}\n")

def zip_track(z):
    candidates = [v for v in z.infolist() if not v.is_dir()]
    for v in candidates:
        path = PurePosixPath(v.filename.replace("\\", "/"))
        if path.is_absolute() or ".." in path.parts or v.file_size > 2*1024**3:
            raise FormatError("unsafe or oversized ZIP member")
    bins = [v for v in candidates if v.filename.lower().endswith((".bin", ".iso"))]
    first = [v for v in bins if re.search(r"track[ _-]*0?1", v.filename, re.I)]
    if len(first) == 1:
        return first[0]
    if len(bins) == 1:
        return bins[0]
    cues = [v for v in candidates if v.filename.lower().endswith(".cue")]
    if len(cues) == 1:
        last = None
        for line in z.read(cues[0]).decode("latin-1").splitlines():
            match = re.match(r'\s*FILE\s+"([^"]+)"', line, re.I)
            if match:
                last = PurePosixPath(match.group(1)).name.lower()
            if re.match(r'\s*TRACK\s+0?1\s+MODE[12]/2352', line, re.I):
                matched = [v for v in bins if PurePosixPath(v.filename).name.lower() == last]
                if len(matched) == 1:
                    return matched[0]
    raise FormatError("cannot find uncompressed PS1 data Track 1 in ZIP")

@contextmanager
def track(source, scratch=None):
    if source.suffix.lower() != ".zip":
        if source.suffix.lower() not in (".bin", ".iso"):
            raise FormatError("expected .zip, .bin, or .iso")
        yield source
        return
    if scratch:
        scratch.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as z, tempfile.TemporaryDirectory(
            prefix="tekken3-", dir=scratch) as tmp:
        member = zip_track(z)
        path = Path(tmp)/"track.bin"
        with z.open(member) as src, path.open("wb") as dest:
            shutil.copyfileobj(src, dest, 1024*1024)
        if path.stat().st_size != member.file_size:
            raise FormatError("incomplete extracted Track 1")
        yield path

def remote_zip(url, cache):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.hostname != "archive.org" \
            or not parsed.path.lower().endswith(".zip"):
        raise FormatError("only explicit HTTPS archive.org ZIP downloads allowed")
    if cache.exists():
        return cache
    cache.parent.mkdir(parents=True, exist_ok=True)
    tmp = cache.with_suffix(".zip.part")
    try:
        with urllib.request.urlopen(url, timeout=60) as response, tmp.open("wb") as out:
            total = 0
            while True:
                chunk = response.read(1024*1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > 2*1024**3:
                    raise FormatError("ZIP download exceeds 2 GiB")
                out.write(chunk)
        if not zipfile.is_zipfile(tmp):
            raise FormatError("remote response is not a ZIP file")
        tmp.replace(cache)
    finally:
        tmp.unlink(missing_ok=True)
    return cache

def import_game(source, out, extract=(), stages=(), scratch=None, expected_sha=US_EXE_SHA):
    with track(source, scratch) as image, Disc(image) as disc:
        exe_file = disc.find("TEKKEN3", "SLUS_004.02")
        exe = disc.span(exe_file, 0, exe_file[2])
        bns = disc.find("TEKKEN3", "TEKKEN3.BNS")
        exe_sha, entries = parse_bns_index(exe, bns[2], expected_sha)
        wanted = set(extract) | set(stages)
        out.mkdir(parents=True, exist_ok=True)
        inventory = []
        for i,(lba, off, size) in enumerate(entries):
            prefix = disc.span(bns, off, min(size,4096))
            row = {"id":i,"bns_lba":lba,"bytes":size,"kind":identify(prefix)}
            if 36 <= i <= 50:
                row["research_name"] = f"stg_{STAGE_NAMES[i-36]}.arc"
            if 56 <= i <= 70:
                row["research_name"] = f"stg_{STAGE_NAMES[i-56]}.tmd"
            if i in wanted and size:
                data = disc.span(bns, off, size)
                row["sha256"] = hashlib.sha256(data).hexdigest()
                raw = out/"raw"/f"{i:03}.bin"
                raw.parent.mkdir(parents=True,exist_ok=True)
                raw.write_bytes(data)
                if i in stages:
                    stats,verts,faces = parse_stage_tmd(data,i)
                    obj = out/"stages"/f"stg_{STAGE_NAMES[i-56]}.obj"
                    write_stage_obj(obj,verts,faces)
                    obj.with_suffix(".json").write_text(
                        json.dumps(stats,indent=2)+"\n", encoding="utf-8")
                    row["geometry"] = stats
            inventory.append(row)
        report = {"schema":1,"disc":"Tekken 3 USA SLUS-00402","exe_sha256":exe_sha,
                  "volume":disc.volume,"track_width":disc.width,
                  "payload_offset":disc.offset,"bns_bytes":bns[2],
                  "records":inventory,"selected":sorted(wanted),
                  "warning":"copyrighted game data; private/local use only"}
        (out/"inventory.json").write_text(
            json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf-8")
        return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, help="local BIN/ISO/ZIP or explicit archive.org ZIP URL")
    parser.add_argument("--cache", type=Path, default=Path("roms/tekken3_usa.zip"))
    parser.add_argument("--scratch-dir", type=Path, help="disk with enough space for the raw track")
    parser.add_argument("--output", type=Path, default=Path("assets/imported/tekken3-usa"))
    parser.add_argument("--extract-ids", default="")
    parser.add_argument("--stage-ids", default="")
    args = parser.parse_args()
    try:
        source = remote_zip(args.source,args.cache) if args.source.startswith("https://") else Path(args.source)
        extracted, stages = selected_ids(args.extract_ids), selected_ids(args.stage_ids)
        if any(not 56 <= i <= 70 for i in stages):
            raise FormatError("stage IDs must be in 56..70")
        report = import_game(source,args.output,extracted,stages,args.scratch_dir)
    except (OSError,FormatError,zipfile.BadZipFile,EOFError) as error:
        parser.exit(2,f"Tekken 3 disc import failed: {error}\n")
    print(f"Verified SLUS-00402: {len(report['records'])} BNS records; "
          f"{len(report['selected'])} records extracted; inventory in {args.output}")

if __name__=="__main__":
    main()
