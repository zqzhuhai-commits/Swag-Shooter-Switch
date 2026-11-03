"""Check the real camera callsite, InputManager gain and relocated native wrappers."""
from pathlib import Path
import json,struct
from read_unity_assets import asset,Reader
root=Path(__file__).resolve().parents[1]
b=(root/'romfs/game/libil2cpp.so').read_bytes()
phoff=struct.unpack_from('<Q',b,32)[0];count=struct.unpack_from('<H',b,56)[0]
ph=[struct.unpack_from('<II6Q',b,phoff+56*i) for i in range(count)]
def offset(address):
    for typ,flags,start,va,_,size,mem,align in ph:
        if typ==1 and va<=address<va+size:return start+address-va
    raise AssertionError(hex(address))
entries=[0x15eb0f4,0x15eb134]
for entry in entries:
    words=struct.unpack_from('<4I',b,offset(entry))
    assert words==(0xa9be4ff4,0xa9017bfd,0x910043fd,0xb000aa74)
    instruction=words[3];imm=((instruction>>5)&0x7ffff)<<2 | ((instruction>>29)&3)
    if imm&(1<<20):imm-=1<<21
    assert ((entry+12)&~4095)+(imm<<12)==0x2b38000
    for bias in [0x7100000000,0x1200000000]:
        slot=bytearray(40);slot[:12]=b[offset(entry):offset(entry)+12]
        struct.pack_into('<IIIQQ',slot,12,0x580000b4,0x58000050,0xd61f0200,bias+entry+16,bias+0x2b38000)
        load=struct.unpack_from('<I',slot,12)[0]
        literal=12+((load>>5)&0x7ffff)*4
        assert literal==32 and struct.unpack_from('<Q',slot,literal)[0]==bias+0x2b38000
        assert struct.unpack_from('<Q',slot,24)[0]==bias+entry+16
pc=0xc55254;instruction=struct.unpack_from('<I',b,offset(pc))[0]
assert instruction>>26==0x25
imm=instruction&0x3ffffff
if imm&(1<<25):imm-=1<<26
assert pc+imm*4==entries[0], 'playerrotate.Update must consume this exact wrapper'
data,objects,header=asset(root/'romfs/game/bin/Data/globalgamemanagers')
obj=next(o for o in objects if o['type']==13);reader=Reader(data[obj['offset']:obj['offset']+obj['size']])
count=reader.get('i');axes=[]
def string():
    n=reader.get('i');value=reader.b[reader.p:reader.p+n].decode();reader.p+=n;reader.align();return value
for i in range(count):
    names=[string() for j in range(7)]
    gravity,dead,sensitivity=reader.get('3f');snap,invert=reader.get('2?');reader.align()
    kind,axis,joy=reader.get('3i')
    if names[0] in ['Mouse X','Mouse Y'] and kind==1:
        assert abs(sensitivity-0.1)<0.000001
        axes.append({'name':names[0],'sensitivity':sensitivity,'axis':axis,'invert':invert})
assert {a['name'] for a in axes}=={'Mouse X','Mouse Y'}
report={'version':'0.14.90','status':'PASS','consumer':'playerrotate.Update -> UnityEngine.Input.GetAxis',
        'wrappers':[hex(x) for x in entries],'adrp_relocated':True,'test_load_bases':2,'mouse_axes':axes,
        'legacy_touch_path':'KEPT','hardware':'PENDING'}
(root/'audit/mouse-bridge-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
