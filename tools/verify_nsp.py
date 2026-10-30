"""Independently inspect NSP/NCA/IVFC/PFS0/NSO and compare every game asset."""
import argparse,hashlib,io,json,struct
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
from cryptography.hazmat.backends import default_backend
from PIL import Image

def digest(b):return hashlib.sha256(b).digest()
def unpack_pfs(b):
    assert b[:4]==b'PFS0';count,names_size,_=struct.unpack_from('<III',b,4)
    assert count<10000
    start=16+24*count+names_size;names=b[16+24*count:start];out={}
    for i in range(count):
        off,size,noff,_=struct.unpack_from('<QQII',b,16+24*i)
        assert noff<len(names) and start+off+size<=len(b)
        name=names[noff:names.index(0,noff)].decode();assert name not in out
        out[name]=b[start+off:start+off+size]
    return out

def decrypt_header(b,key):
    h=bytearray()
    for sector in range(6):
        d=Cipher(algorithms.AES(key),modes.XTS(sector.to_bytes(16,'big')),backend=default_backend()).decryptor()
        h+=d.update(b[sector*512:(sector+1)*512])+d.finalize()
    assert h[0x200:0x204]==b'NCA3' and struct.unpack_from('<Q',h,0x208)[0]==len(b)
    return h

def sections(b,h):
    result=[]
    for i in range(4):
        start,end=struct.unpack_from('<II',h,0x240+i*16)
        if not start and not end:continue
        assert 6<=start<end and end*512<=len(b)
        fs=h[0x400+i*512:0x600+i*512];assert digest(fs)==h[0x280+i*32:0x2a0+i*32]
        data=b[start*512:end*512];assert fs[4]==1,'unexpected encrypted section'
        if fs[2]==1:
            block,levels,ho,hs,po,ps=struct.unpack_from('<IIQQQQ',fs,0x28)
            assert levels==2 and po+ps<=len(data) and ho+hs<=len(data)
            hashes=data[ho:ho+hs];payload=data[po:po+ps]
            assert digest(hashes)==fs[8:40]
            assert hashes==b''.join(digest(payload[j:j+block]) for j in range(0,len(payload),block))
            result.append(('pfs',unpack_pfs(payload)))
        else:
            assert fs[3]==3 and fs[8:12]==b'IVFC'
            levels=[]
            for j in range(6):
                off,size,power,_=struct.unpack_from('<QQII',fs,0x18+j*24)
                assert power==14 and off+size<=len(data)
                block=1<<power;payload=data[off:off+size]
                padded=payload+bytes((-len(payload))%block);levels.append((payload,padded))
            assert digest(levels[0][0])==fs[0xc8:0xe8]
            for j in range(5):
                child=levels[j+1][1];expected=b''.join(digest(child[k:k+16384]) for k in range(0,len(child),16384))
                parent=levels[j][0];assert parent[:len(expected)]==expected and not any(parent[len(expected):])
            result.append(('romfs',levels[-1][0]))
    return result

def romfs_files(b):
    h=struct.unpack_from('<10Q',b);assert h[0]==80
    for off,size in zip(h[1:9:2],h[2:9:2]):assert off+size<=len(b)
    out={};seen_dirs=set();seen_files=set()
    def walk(d,prefix):
        assert d not in seen_dirs and d+24<=h[4];seen_dirs.add(d)
        _,_,child,f,_,n=struct.unpack_from('<6I',b,h[3]+d)
        assert d+24+n<=h[4]
        name=b[h[3]+d+24:h[3]+d+24+n].decode();base=prefix+name+('/' if name else '')
        while f!=0xffffffff:
            assert f not in seen_files and f+32<=h[8];seen_files.add(f)
            _,nextf,off,size,_,n=struct.unpack_from('<IIQQII',b,h[7]+f)
            assert f+32+n<=h[8] and h[9]+off+size<=len(b)
            name=b[h[7]+f+32:h[7]+f+32+n].decode();path=base+name;assert path not in out
            out[path]=b[h[9]+off:h[9]+off+size];f=nextf
        while child!=0xffffffff:
            walk(child,base);child=struct.unpack_from('<I',b,h[3]+child+4)[0]
    walk(0,'');return out

def lz4_block(b):
    out=bytearray();p=0
    while p<len(b):
        token=b[p];p+=1;literal=token>>4
        if literal==15:
            while True:
                extra=b[p];p+=1;literal+=extra
                if extra!=255:break
        assert p+literal<=len(b);out+=b[p:p+literal];p+=literal
        if p==len(b):break
        distance=int.from_bytes(b[p:p+2],'little');p+=2;assert 0<distance<=len(out)
        length=(token&15)+4
        if (token&15)==15:
            while True:
                extra=b[p];p+=1;length+=extra
                if extra!=255:break
        pattern=out[-distance:];out+=(pattern*((length+distance-1)//distance))[:length]
    return bytes(out)

def verify(path,project,keys):
    key=None
    for line in keys.read_text(encoding='utf-8').splitlines():
        name,sep,value=line.partition('=')
        if sep and name.strip()=='header_key':key=bytes.fromhex(value.strip());break
    assert key is not None and len(key)==32
    outer=unpack_pfs(path.read_bytes());assert len(outer)==3
    nc={};tid=0x01af534741470000
    for name,data in outer.items():
        assert digest(data)[:16].hex()==name.split('.')[0]
        h=decrypt_header(data,key);assert struct.unpack_from('<Q',h,0x210)[0]==tid
        kind=h[0x205];assert kind not in nc;nc[kind]=(data,sections(data,h))
    assert set(nc)=={0,1,2}
    exefs=next(s[1] for s in nc[0][1] if s[0]=='pfs')
    assert set(exefs)=={'main','main.npdm'}
    nso=exefs['main'];assert nso[:4]==b'NSO0'
    flags=struct.unpack_from('<I',nso,12)[0]
    for i,o in enumerate([0x10,0x20,0x30]):
        off,mem,size=struct.unpack_from('<III',nso,o);stored=struct.unpack_from('<I',nso,0x60+4*i)[0]
        assert off+stored<=len(nso)
        raw=lz4_block(nso[off:off+stored]) if flags&(1<<i) else nso[off:off+stored]
        assert len(raw)==size and digest(raw)==nso[0xa0+32*i:0xc0+32*i]
    npdm=exefs['main.npdm'];assert npdm[:4]==b'META' and (npdm[12]>>1)&7==3
    assert struct.unpack_from('<I',npdm,0x1c)[0]>=0x100000
    aci,acis,acid,acids=struct.unpack_from('<4I',npdm,0x70)
    assert npdm[aci:aci+4]==b'ACI0' and npdm[acid+512:acid+516]==b'ACID'
    assert struct.unpack_from('<Q',npdm,aci+16)[0]==tid
    assert struct.unpack_from('<QQ',npdm,acid+0x210)==(tid,tid)
    for base,sizeoff in [(aci,0x30),(acid,0x230)]:
        off,size=struct.unpack_from('<II',npdm,base+sizeoff);caps=struct.unpack_from('<'+'I'*(size//4),npdm,base+off)
        masks={c>>29: (c>>5)&0xffffff for c in caps if c&31==15}
        for svc in [0x40,0x43,0x73,0x77,0x78]:assert masks.get(svc//24,0)&(1<<(svc%24))
        apps=[c>>14&7 for c in caps if c&0x3fff==0x1fff];assert apps==[1]
    assets=romfs_files(next(s[1] for s in nc[0][1] if s[0]=='romfs'))
    actual={name:digest(data).hex() for name,data in assets.items()}
    expected={p.relative_to(project/'romfs').as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (project/'romfs').rglob('*') if p.is_file()}
    assert actual==expected and len(actual)==1866
    assert not any(name.endswith('.nro') for name in assets),'unexpected forwarder payload'
    control=romfs_files(next(s[1] for s in nc[2][1] if s[0]=='romfs'))
    nacp=control['control.nacp'];assert len(nacp)==16384
    for i in range(16):
        entry=nacp[i*768:(i+1)*768]
        assert entry[:512].split(b'\0')[0].decode()=='绝地精英'
        assert entry[512:].split(b'\0')[0].decode()=='Xiao Fan'
    assert nacp[0x3060:0x3070].split(b'\0')[0]==b'0.14.58'
    icons=[data for name,data in control.items() if name.startswith('icon_')];assert len(icons)==16
    for icon in icons:assert Image.open(io.BytesIO(icon)).size==(256,256) and len(icon)<=0x20000
    meta=next(s[1] for s in nc[1][1] if s[0]=='pfs');assert len(meta)==1
    cnmt=next(iter(meta.values()));assert struct.unpack_from('<QI',cnmt)==(tid,58) and cnmt[12]==0x80
    assert digest(cnmt[:-32])==cnmt[-32:]
    ext,count=struct.unpack_from('<HH',cnmt,14);assert count==2
    for i in range(count):
        record=cnmt[32+ext+i*56:32+ext+(i+1)*56];content_type=record[54]
        data=nc[{1:0,3:2}[content_type]][0]
        assert record[:32]==digest(data) and record[32:48]==digest(data)[:16]
        assert int.from_bytes(record[48:54],'little')==len(data)
    report={'status':'PASS','checks':['three content-addressed NCAs','AES-XTS headers','NCA filesystem-header hashes','PFS0 block hashes','all six IVFC levels','NSO decompressed segment hashes','NPDM title/39-bit/application/memory syscalls','1866 game files match SHA-256','16 UTF-8 name/author labels','16 256x256 JPEG icons','CNMT content hashes/sizes/version/digest'],'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'hardware_install_launch':'PENDING'}
    (project/'audit/nsp-verification-0.14.58.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('nsp',type=Path);ap.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[1]);ap.add_argument('--keys',type=Path,default=Path.home()/'.switch/prod.keys');a=ap.parse_args();verify(a.nsp,a.project,a.keys)
