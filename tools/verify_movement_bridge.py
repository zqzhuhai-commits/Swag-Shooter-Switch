"""Check the final movement bridge preserves live registers and FP/control state."""
from pathlib import Path
import re,struct,subprocess,random,json
root=Path(__file__).resolve().parents[1];prefix='C:/devkitPro/devkitA64/bin/aarch64-none-elf-';elf=root/'swag-switch.elf'
nm=subprocess.check_output([prefix+'nm.exe','-n',str(elf)],text=True)
syms={m[2]:int(m[1],16) for m in re.finditer(r'^([0-9a-f]+) [A-Za-z] (local_movement_\w+)$',nm,re.M)}
a=syms['local_movement_bridge']
dis=subprocess.check_output([prefix+'objdump.exe','-d','--start-address='+hex(a),'--stop-address='+hex(a+420),str(elf)],text=True)
ops=[]
for line in dis.splitlines():
 m=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(\w+)\s+(.*)',line)
 if m:
  ops.append((m[2],m[3].split('//')[0].strip()))
  if m[2]=='br':break
rng=random.Random(81)
for case in range(64):
 regs={'x'+str(i):rng.getrandbits(64) for i in range(31)}
 regs.update({'q'+str(i):rng.getrandbits(128) for i in range(32)})
 regs.update(sp=0x800000,x19=0x120000,x22=0x130000,nzcv=rng.getrandbits(32),fpcr=rng.getrandbits(32),fpsr=rng.getrandbits(32))
 before=regs.copy();mem={0x130000:0x140000,0x120000+500:0x3f800001,0x120000+504:0x40000002,0x120000+508:0x40400003,syms['local_movement_resume']:0xc3d39c}
 def read(r):return regs[r]
 def put(r,v):
  if r.startswith('s'):regs['q'+r[1:]]=v&0xffffffff
  else:regs[r]=v
 for op,arg in ops:
  if op in ['add','sub']:
   d,s,n=re.match(r'(\w+), (\w+), #(\w+)',arg).groups();put(d,read(s)+(1 if op=='add' else -1)*int(n,0))
  elif op in ['stp','ldp']:
   r1,r2,base,offset=re.match(r'(\w+), (\w+), \[(\w+)(?:, #([^\]]+))?\]',arg).groups();at=read(base)+int(offset or '0',0);stride=16 if r1.startswith('q') else 8
   if op=='stp':mem[at]=read(r1);mem[at+stride]=read(r2)
   else:put(r1,mem[at]);put(r2,mem[at+stride])
  elif op in ['str','ldr']:
   r,base,offset=re.match(r'(\w+), \[(\w+)(?:, #([^\]]+))?\]',arg).groups();at=read(base)+int(offset or '0',0)
   if op=='str':mem[at]=read(r)
   else:put(r,mem[at])
  elif op=='mov':d,s=arg.split(', ');put(d,read(s))
  elif op=='mrs':d,s=arg.lower().split(', ');put(d,read(s))
  elif op=='msr':d,s=arg.lower().split(', ');put(d,read(s))
  elif op=='bl':
   assert 'local_movement_prepare' in arg and regs['x0']==before['x19'] and regs['sp']%16==0
   for i in list(range(19))+[30]:regs['x'+str(i)]=rng.getrandbits(64)
   for i in range(32):regs['q'+str(i)]=rng.getrandbits(128)
   for f in ['nzcv','fpcr','fpsr']:regs[f]=rng.getrandbits(32)
  elif op=='adrp':put(arg.split(', ')[0],int(arg.split(', ')[1].split()[0],16))
  elif op=='br':assert read(arg)==0xc3d39c
  else:raise AssertionError((op,arg))
 assert regs['sp']==before['sp'] and regs['x0']==0x140000
 assert regs['q11']==0x3f800001 and regs['q10']==0x40000002 and regs['q8']==0x40400003
 for reg,value in before.items():
  if reg not in ['x0','x16','q8','q10','q11']:assert regs[reg]==value,(case,reg)
# Independently check the guest instructions overwritten by this bridge.
b=(root/'romfs/game/libil2cpp.so').read_bytes();phoff=struct.unpack_from('<Q',b,32)[0];count=struct.unpack_from('<H',b,56)[0]
for i in range(count):
 typ,flags,start,va,_,size,memsz,align=struct.unpack_from('<II6Q',b,phoff+56*i)
 if typ==1 and va<=0xc3d38c<va+size:
  assert struct.unpack_from('<4I',b,start+0xc3d38c-va)==(0xf94002c0,0xbd41f66b,0xbd41fa6a,0xbd41fe68);break
else:raise AssertionError('guest patch site missing')
report={'version':'0.14.90','status':'PASS','random_register_cases':64,'stack_alignment':16,'flags_fpcr_fpsr_preserved':True,'displaced_loads_replayed':True,'resume':'0xc3d39c','hardware':'PENDING'}
(root/'audit/movement-bridge-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
