"""Write UTF-8 NACP labels explicitly; Windows tool argv can use the ANSI codepage."""
from pathlib import Path
import struct
import sys

def fix(project):
    values={}
    for line in (project/'Makefile').read_text(encoding='utf-8').splitlines():
        if ':=' in line:
            key,value=line.split(':=',1)
            if key.strip() in ('APP_TITLE','APP_AUTHOR'): values[key.strip()]=value.strip()
    title=values['APP_TITLE'].encode('utf-8'); author=values['APP_AUTHOR'].encode('utf-8')
    assert len(title)<512 and len(author)<256
    labels=(title.ljust(512,b'\0')+author.ljust(256,b'\0'))*16
    nacp=project/'swag-switch.nacp'
    assert nacp.stat().st_size>=0x4000
    with nacp.open('r+b') as f: f.write(labels)
    nro=project/'swag-switch.nro'
    with nro.open('r+b') as f:
        f.seek(24); size=struct.unpack('<I',f.read(4))[0]
        f.seek(size); aset=f.read(56)
        assert aset[:4]==b'ASET'
        offset,length=struct.unpack_from('<QQ',aset,24)
        assert length==0x4000 and offset>=56 and size+offset+length<=nro.stat().st_size
        f.seek(size+offset); f.write(labels)
        f.seek(size+offset); assert f.read(len(labels))==labels
    print('PASS: all 16 NACP title/author labels encoded as UTF-8, standalone and embedded')

if __name__=='__main__': fix(Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[1])
