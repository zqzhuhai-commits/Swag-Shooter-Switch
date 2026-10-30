"""Package the native SWAG ELF and complete game RomFS as one homebrew NSP.
Local prod.keys is read only for the NCA header key and is never copied.
"""
import argparse,base64,hashlib,json,struct,subprocess
from pathlib import Path
import nsp_container as n

TITLE='绝地精英'
AUTHOR='Xiao Fan'
TITLE_ID=0x01af534741470000
VERSION='0.14.38'
LANGUAGES=['AmericanEnglish','BritishEnglish','Japanese','French','German','LatinAmericanSpanish','Spanish','Italian','Dutch','CanadianFrench','Portuguese','Russian','Korean','TraditionalChinese','SimplifiedChinese','BrazilianPortuguese']

def read_header_key(path):
    for line in path.read_text(encoding='utf-8',errors='replace').splitlines():
        name,sep,value=line.partition('=')
        if sep and name.strip().lower()=='header_key':
            key=bytes.fromhex(value.strip()); assert len(key)==32; return key
    raise ValueError('Local key file has no valid NCA header key')

def check_npdm(b):
    assert b[:4]==b'META' and b[0xc]&1 and (b[0xc]>>1)&7==3
    aci,acis,acid,acids=struct.unpack_from('<4I',b,0x70)
    assert b[aci:aci+4]==b'ACI0' and b[acid+0x200:acid+0x204]==b'ACID'
    assert struct.unpack_from('<Q',b,aci+0x10)[0]==TITLE_ID
    assert struct.unpack_from('<QQ',b,acid+0x210)==(TITLE_ID,TITLE_ID)
    assert aci+acis<=len(b) and acid+acids<=len(b)
    # All three loader memory syscalls must be permitted in ACI0 and ACID.
    for base,sizeoff in [(aci,0x30),(acid,0x230)]:
        offset,size=struct.unpack_from('<II',b,base+sizeoff)
        caps=struct.unpack_from('<'+'I'*(size//4),b,base+offset)
        masks={c>>29: (c>>5)&0xffffff for c in caps if c&0x1f==0xf}
        for svc in [0x40,0x43,0x73,0x77,0x78]: assert masks.get(svc//24,0)&(1<<(svc%24)),hex(svc)


def control_metadata(original):
    b=bytearray(original); assert len(b)==0x4000
    label=TITLE.encode().ljust(512,b'\0')+AUTHOR.encode().ljust(256,b'\0')
    b[:0x3000]=label*16
    struct.pack_into('<I',b,0x302c,0xffff)
    b[0x3025]=0; b[0x3026]=0; b[0x3027]=1
    b[0x3034]=0; b[0x3035]=0 # screenshots allowed, no video-memory reservation
    b[0x3060:0x3070]=VERSION.encode().ljust(16,b'\0')
    struct.pack_into('<Q',b,0x3038,TITLE_ID)
    struct.pack_into('<QQ',b,0x3070,TITLE_ID+0x1000,TITLE_ID)
    b[0x3080:0x30a0]=bytes(32); b[0x3148:0x3168]=bytes(32)
    b[0x30a8:0x30b0]=b'swag-nx\0'
    struct.pack_into('<8Q',b,0x30b0,*([TITLE_ID]*8))
    struct.pack_into('<Q',b,0x30f8,TITLE_ID)
    return bytes(b)

def build(project,nro,key_path,devkit,out,work):
    work.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(devkit/'tools/bin/elf2nso.exe'),str(project/'swag-switch.elf'),str(work/'main')],check=True)
    subprocess.run([str(devkit/'tools/bin/npdmtool.exe'),str(project/'packaging/npdm.json'),str(work/'main.npdm')],check=True)
    main=(work/'main').read_bytes(); assert main[:4]==b'NSO0'
    npdm=(work/'main.npdm').read_bytes();check_npdm(npdm)
    source=nro.read_bytes(); assert source[16:20]==b'NRO0'
    size=struct.unpack_from('<I',source,24)[0]; assert source[size:size+4]==b'ASET'
    offset,length=struct.unpack_from('<QQ',source,size+40)
    image=source[size+offset:size+offset+length]; assert len(image)==length and struct.unpack_from('<Q',image)[0]==80
    key=read_header_key(key_path)
    program=n.Nca(n.CONTENT_PROGRAM,TITLE_ID)
    program.add_pfs0([('main',main),('main.npdm',npdm)],n.EXEFS_BLOCK)
    program.add_romfs_image(image);program=program.build(key)
    control=n.Nca(n.CONTENT_CONTROL,TITLE_ID)
    metadata=control_metadata((project/'swag-switch.nacp').read_bytes())
    icon=base64.b64decode((project/'artwork/icon-256.jpg.b64').read_text())
    assert icon[:2]==b'\xff\xd8' and len(icon)<=0x20000
    control.add_romfs([('control.nacp',metadata)]+[(f'icon_{language}.dat',icon) for language in LANGUAGES]);control=control.build(key)
    meta=n.Nca(n.CONTENT_META,TITLE_ID)
    cnmt=bytearray(n.cnmt(TITLE_ID,[(n.NCM_PROGRAM,program),(n.NCM_CONTROL,control)]))
    struct.pack_into('<I',cnmt,8,38);cnmt[-32:]=n.sha256(cnmt[:-32])
    meta.add_pfs0([(f'Application_{TITLE_ID:016x}.cnmt',cnmt)],n.META_BLOCK);meta=meta.build(key)
    files=[(n.sha256(program)[:16].hex()+'.nca',program),(n.sha256(control)[:16].hex()+'.nca',control),(n.sha256(meta)[:16].hex()+'.cnmt.nca',meta)]
    out.parent.mkdir(parents=True,exist_ok=True)
    temporary=out.with_suffix('.nsp.partial')
    temporary.write_bytes(n.pfs0(files));temporary.replace(out)
    report={'version':VERSION,'title':TITLE,'author':AUTHOR,'title_id':f'{TITLE_ID:016X}','artifact':out.name,'bytes':out.stat().st_size,'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'native_nso':True,'external_nro_required':False,'romfs_bytes':len(image),'source_nro_sha256':hashlib.sha256(source).hexdigest(),'native_nso_sha256':hashlib.sha256(main).hexdigest(),'verification':'pending independent verify_nsp.py','device_launch':'pending'}
    (project/f'audit/nsp-{VERSION}.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))

if __name__=='__main__':
    root=Path(__file__).resolve().parents[1]
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project',type=Path,default=root);ap.add_argument('--nro',type=Path,default=root/'swag-switch.nro')
    ap.add_argument('--keys',type=Path,default=Path.home()/'.switch/prod.keys')
    ap.add_argument('--devkitpro',type=Path,default=Path('C:/devkitPro'))
    ap.add_argument('--out',type=Path,default=root.parent/f'{TITLE} [01AF534741470000][{VERSION}].nsp')
    ap.add_argument('--work',type=Path,default=root/'build/nsp')
    a=ap.parse_args();build(a.project.resolve(),a.nro.resolve(),a.keys.resolve(),a.devkitpro.resolve(),a.out.resolve(),a.work.resolve())
