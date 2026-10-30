import re,subprocess,random,json,struct
from pathlib import Path
root=Path(__file__).resolve().parents[1];elf=root/'swag-switch.elf'
exe='C:/devkitPro/devkitA64/bin/aarch64-none-elf-'
nm=subprocess.check_output([exe+'nm.exe','-n',str(elf)],text=True)
syms={m.group(2):int(m.group(1),16) for m in re.finditer(r'^([0-9a-f]+) [A-Za-z] (zone_phase_\w+)$',nm,re.M)}
a=syms['zone_phase_bridge']
dis=subprocess.check_output([exe+'objdump.exe','-d','--start-address='+hex(a),'--stop-address='+hex(a+260),str(elf)],text=True)
ops=[]
for line in dis.splitlines():
 m=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(\w+)\s+(.*)',line)
 if m:ops.append((m[2],m[3].split('//')[0].strip()))
assert len(ops)==65, len(ops)
rng=random.Random(48);cases=0
for limit in [180,120,90,60,30]:
 for index in [0,1,2,3,4,5,8,100]:
  regs={'x'+str(i):rng.getrandbits(64) for i in range(31)}
  regs.update({'q'+str(i):rng.getrandbits(128) for i in range(32)})
  regs.update(sp=0x800000,x8=0x100000,x20=0x120000,x19=0x130000)
  before=regs.copy();mem={0x120090:0,syms['zone_phase_resume']:0x1608b84};flags=None
  def read(r):return regs['x'+r[1:]]&0xffffffff if r.startswith('w') else regs[r]
  def put(r,v):regs['x'+r[1:] if r.startswith('w') else r]=v&((1<<(32 if r.startswith('w') else 128 if r.startswith('q') else 64))-1)
  def addr(base,offset):return regs[base]+int(offset or '0',0)
  for op,arg in ops:
   if op in ['sub','add']:
    d,s,n=re.match(r'(\w+), (\w+), #(\w+)',arg).groups();put(d,read(s)+(1 if op=='add' else -1)*int(n,0))
   elif op in ['stp','ldp']:
    r1,r2,base,offset=re.match(r'(\w+), (\w+), \[(\w+)(?:, #([^\]]+))?\]',arg).groups();at=addr(base,offset);stride=16 if r1[0]=='q' else 8
    if op=='stp':mem[at]=read(r1);mem[at+stride]=read(r2)
    else:put(r1,mem[at]);put(r2,mem[at+stride])
   elif op in ['str','ldr','ldur']:
    r,base,offset=re.match(r'(\w+), \[(\w+)(?:, #([^\]]+))?\]!?',arg).groups();at=addr(base,offset)
    if arg.endswith('!'):put(base,at)
    if op=='str':mem[at]=read(r)
    else:
     if r=='s0':regs['q0']=mem[at]
     else:put(r,mem[at])
   elif op=='bl':
    assert 'zone_phase_prepare' in arg
    assert regs['x0']==0x120000 and regs['x1']==0x130000 and regs['sp']%16==0
    mem[0x120090]=struct.unpack('<I',struct.pack('<f',limit/60))[0]
    for i in list(range(19))+[30]:regs['x'+str(i)]=rng.getrandbits(64)
    for i in range(32):regs['q'+str(i)]=rng.getrandbits(128)
    regs['x0']=limit
   elif op=='mov':d,s=arg.split(', ');put(d,read(s))
   elif op=='cmp':
    r1,r2=arg.split(', ');v1,v2=read(r1),read(r2);signed=lambda x:x-(1<<32) if x&(1<<31) else x
    flags=(signed(v1)>=signed(v2))
   elif op=='adrp':put(arg.split(', ')[0],int(arg.split(', ')[1].split()[0],16))
   elif op=='br':assert read(arg)==0x1608b84
   else:raise AssertionError((op,arg))
  assert regs['sp']==before['sp']
  assert regs['x0']==regs['x21']==0x1200c8
  assert mem[0x1200c8]==before['x0']
  assert regs['q0']==mem[0x120090]
  for r,v in before.items():
   if r not in ['x0','x16','x21','q0']:assert regs[r]==v,(r,hex(regs[r]),hex(v))
  cases+=1
report={'status':'PASS','bridge_register_stack_datetime_and_float_cases':cases,'wait_seconds':[180,120,90,60,30],'shrink_seconds':[60,45,40,30,30],'hardware_validation':'PENDING'}
(root/'audit/progressive-zone-0.14.51.json').write_text(json.dumps(report,indent=2));print(report)
