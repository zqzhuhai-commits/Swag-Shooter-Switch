"""Check the built NRO and independently hash every embedded RomFS file."""
import hashlib
from pathlib import Path
import struct
import sys


def verify(project):
    b = (project / 'swag-switch.nro').read_bytes()
    def u32(o): return struct.unpack_from('<I', b, o)[0]
    def u64(o): return struct.unpack_from('<Q', b, o)[0]
    assert b[16:20] == b'NRO0', 'missing NRO header'
    nro_size = u32(24)
    assert 128 <= nro_size <= len(b) - 56
    for o in (32, 40, 48):
        assert u32(o) + u32(o + 4) <= nro_size, 'NRO segment outside image'
    assert b[nro_size:nro_size + 4] == b'ASET', 'missing asset header'
    romfs_offset = nro_size + u64(nro_size + 40)
    romfs_size = u64(nro_size + 48)
    assert romfs_offset + romfs_size <= len(b), 'truncated RomFS'
    hdr = struct.unpack_from('<10Q', b, romfs_offset)
    assert hdr[0] == 80
    for offset, size in zip(hdr[1:9:2], hdr[2:9:2]):
        assert offset + size <= romfs_size
    dirmeta, filemeta, data = (romfs_offset + hdr[i] for i in (3, 7, 9))
    found = {}
    seen_dirs, seen_files = set(), set()
    def visit(d, prefix):
        assert d not in seen_dirs and d + 24 <= hdr[4], 'invalid directory chain'
        seen_dirs.add(d)
        parent, sibling, child, firstfile, next_hash, n = struct.unpack_from('<6I', b, dirmeta + d)
        assert d + 24 + n <= hdr[4]
        name = b[dirmeta + d + 24:dirmeta + d + 24 + n].decode()
        path = prefix + name + ('/' if name else '')
        f = firstfile
        while f != 0xffffffff:
            assert f not in seen_files and f + 32 <= hdr[8], 'invalid file chain'
            seen_files.add(f)
            par, nxt, offset, size, nh, length = struct.unpack_from('<IIQQII', b, filemeta + f)
            assert f + 32 + length <= hdr[8]
            filename = b[filemeta + f + 32:filemeta + f + 32 + length].decode()
            assert hdr[9] + offset + size <= romfs_size
            found[path + filename] = hashlib.sha256(b[data + offset:data + offset + size]).hexdigest()
            f = nxt
        c = child
        while c != 0xffffffff:
            visit(c, path)
            c = struct.unpack_from('<I', b, dirmeta + c + 4)[0]
    visit(0, '')
    expected = {str(p.relative_to(project / 'romfs')).replace('\\', '/'):
                hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (project / 'romfs').rglob('*') if p.is_file()}
    assert found == expected, 'embedded assets differ from project files'
    assert len(found) >= 9, 'missing fixture/game files'
    print('PASS: NRO segments and RomFS bounds; {} embedded files match SHA-256'.format(len(found)))
    print('NRO bytes={} SHA-256={}'.format(len(b), hashlib.sha256(b).hexdigest()))


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]))
