import struct, sys, os, zlib
src, out = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
assert d[:4] == b'qres', d[:4]
ver, tree, data, names = struct.unpack('>IIII', d[4:20])
flags_hdr = struct.unpack('>I', d[20:24])[0] if ver >= 3 else 0
esz = 22 if ver >= 2 else 14
def name(off):
    off += names
    ln = struct.unpack('>H', d[off:off+2])[0]
    return d[off+6:off+6+ln*2].decode('utf-16-be')
def walk(idx, path):
    e = tree + idx*esz
    noff, fl = struct.unpack('>IH', d[e:e+6])
    nm = name(noff) if idx else ''
    p = os.path.join(path, nm) if nm else path
    if fl & 2:
        cnt, first = struct.unpack('>II', d[e+6:e+14])
        os.makedirs(p, exist_ok=True)
        for i in range(cnt): walk(first+i, p)
    else:
        doff = struct.unpack('>I', d[e+10:e+14])[0] + data
        sz = struct.unpack('>I', d[doff:doff+4])[0]
        blob = d[doff+4:doff+4+sz]
        if fl & 1: blob = zlib.decompress(blob[4:])
        elif fl & 4:
            import zstandard; blob = zstandard.ZstdDecompressor().decompress(blob, max_output_size=1<<28)
        open(p, 'wb').write(blob)
walk(0, out)
