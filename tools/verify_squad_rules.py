"""Execute the compiled integer-only ARM64 squad rules fixture on the host.

The small interpreter accepts only the instructions emitted for this fixture;
unknown instructions fail rather than silently approximating game execution.
This checks real compiled C rules, not Unity/IL2CPP gameplay integration.
"""
from pathlib import Path
import json,re,struct,subprocess,tempfile

root=Path(__file__).resolve().parents[1]
prefix='C:/devkitPro/devkitA64/bin/aarch64-none-elf-'
with tempfile.TemporaryDirectory(prefix='swag-squad-') as temporary:
    elf=Path(temporary)/'rules.elf'
    source=(root/'source/squad_mode.inc').read_text(encoding='utf-8')
    start=source.index('static void *squad_int_array(')
    array_builder=source[start:source.index('static void squad_sync_lives',start)]
    extra=Path(temporary)/'array.c'
    extra.write_text('''#include <stdint.h>
#include <string.h>
typedef struct {uint64_t header[3],length;int values[12];} Array;
static Array arrays[26];static unsigned next_array;
static const void *corlib(void) {return (void *)1;}
static void *klass(const void *image,const char *ns,const char *name) {(void)image;(void)ns;(void)name;return (void *)2;}
static void *new_array(void *k,uintptr_t length) {(void)k;if(next_array>=26)return 0;Array *a=&arrays[next_array++];a->length=length;return a;}
static uintptr_t array_length(void *a) {return ((Array *)a)->length;}
static struct {
 const void *(*corlib)(void);
 void *(*class_named)(const void *,const char *,const char *);
 void *(*array_new)(void *,uintptr_t);
 uintptr_t (*array_length)(void *);
} api={corlib,klass,new_array,array_length};
''' + array_builder + '''
extern int run_squad_rules_tests(void);
int run_all_tests(void) {
 int result=run_squad_rules_tests();if(result)return result;
 for(unsigned team=1;team<=13;++team) {
  Array *enemies=squad_int_array(team,0),*allies=squad_int_array(team,1);
  if(!enemies || enemies->length!=12 || !allies || allies->length!=1 || allies->values[0]!=(int)team)return 40;
  unsigned seen=0;
  for(unsigned i=0;i<12;++i) {
   int id=enemies->values[i];
   if(id<1 || id>13 || id==(int)team || (seen&(1u<<id)))return 41;
   seen|=1u<<id;
  }
 }
 return 0;
}
''',encoding='utf-8')
    subprocess.run([prefix+'gcc.exe','-O0','-ffreestanding','-fno-builtin',
        '-fno-stack-protector','-mgeneral-regs-only','-nostdlib','-I',str(root/'include'),
        '-Wl,-e,run_all_tests',str(root/'tests/squad_rules_fixture.c'),str(extra),'-o',str(elf)],check=True)
    binary=elf.read_bytes()
    entry=struct.unpack_from('<Q',binary,24)[0]
    dis=subprocess.check_output([prefix+'objdump.exe','-d',str(elf)],text=True)
instructions={}
for line in dis.splitlines():
    m=re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(\S+)\s*(.*)',line)
    if m:instructions[int(m[1],16)]=(m[2],m[3].split('//')[0].strip())
regs={'x'+str(i):0 for i in range(31)}
regs['sp']=0x80000000
stop=0x12340000
regs['x30']=stop
memory={}
phoff=struct.unpack_from('<Q',binary,32)[0]
phsize,phnum=struct.unpack_from('<HH',binary,54)
for i in range(phnum):
    typ,_,offset,vaddr,_,size,_,_=struct.unpack_from('<II6Q',binary,phoff+i*phsize)
    if typ==1:
        for j,value in enumerate(binary[offset:offset+size]):memory[vaddr+j]=value
flags={'N':0,'Z':0,'C':0,'V':0}
mask64=(1<<64)-1
def read(r):
    r=r.strip()
    if r.startswith('#'):return int(r[1:],0)
    if r in ('xzr','wzr'):return 0
    return regs['x'+r[1:]]&0xffffffff if r.startswith('w') else regs[r]
def write(r,v):
    if r in ('xzr','wzr'):return
    regs['x'+r[1:] if r.startswith('w') else r]=v&((1<<32)-1 if r.startswith('w') else mask64)
def load(address,size):return sum(memory.get(address+i,0)<<(8*i) for i in range(size))
def save(address,size,value):
    for i in range(size):memory[address+i]=(value>>(8*i))&255
def condition(name):
    n,z,c,v=(flags[k] for k in ('N','Z','C','V'))
    return {'eq':z,'ne':not z,'cc':not c,'lo':not c,'cs':c,'hs':c,
        'hi':c and not z,'ls':not c or z,'lt':n!=v,'ge':n==v,
        'le':z or n!=v,'gt':not z and n==v}[name]
def compare(a,b,bits,addition=False):
    limit=(1<<bits)-1;a&=limit;b&=limit
    result=(a+b if addition else a-b)&limit
    flags['N']=(result>>(bits-1))&1;flags['Z']=result==0
    flags['C']=a+b>limit if addition else a>=b
    flags['V']=bool(((~(a^b) if addition else a^b)&(a^result))&(1<<(bits-1)))
def address(operand):
    m=re.fullmatch(r'\[(\w+)(?:, (#[\-0-9a-fx]+|[wx]\d+)(?:, lsl #(\d+))?)?\](!?)(?:, #(-?(?:0x[0-9a-f]+|\d+)))?',operand)
    if not m:raise AssertionError(operand)
    base,offset,shift,pre,post=m.groups();at=read(base)+((read(offset)<<int(shift or 0)) if offset else 0)
    if pre:write(base,at)
    return at,base,int(post or '0',0)
pc=entry;steps=0
while pc!=stop:
    steps+=1
    if steps>20000000:raise AssertionError('fixture instruction budget exceeded')
    op,arg=instructions[pc];next_pc=pc+4
    if op in ('add','sub','and','lsl','lsr'):
        d,a,b=[v.strip() for v in arg.split(',')];av,bv=read(a),read(b)
        value={'add':lambda:av+bv,'sub':lambda:av-bv,'and':lambda:av&bv,
            'lsl':lambda:av<<bv,'lsr':lambda:av>>bv}[op]()
        write(d,value)
    elif op=='orr':
        d,a,b=[v.strip() for v in arg.split(',')];write(d,read(a)|read(b))
    elif op=='mov':d,a=arg.split(', ');write(d,read(a))
    elif op in ('adr','adrp'):
        d,a=arg.split(', ',1);write(d,int(a.split()[0],16))
    elif op in ('cmp','cmn'):
        a,b=arg.split(', ');compare(read(a),read(b),32 if a.startswith('w') else 64,op=='cmn')
    elif op=='cset':d,c=arg.split(', ');write(d,int(condition(c)))
    elif op in ('ldr','str','ldrb','strb'):
        register,operand=arg.split(', ',1);at,base,post=address(operand)
        size=1 if op.endswith('b') else 4 if register.startswith('w') else 8
        if op.startswith('ld'):write(register,load(at,size))
        else:save(at,size,read(register))
        if post:write(base,read(base)+post)
    elif op in ('stp','ldp'):
        r1,r2,operand=arg.split(', ',2);at,base,post=address(operand)
        size=4 if r1.startswith('w') else 8
        if op=='stp':save(at,size,read(r1));save(at+size,size,read(r2))
        else:write(r1,load(at,size));write(r2,load(at+size,size))
        if post:write(base,read(base)+post)
    elif op in ('b','bl') or op.startswith('b.'):
        target=int(arg.split()[0],16)
        if op=='bl':write('x30',pc+4)
        if op in ('b','bl') or condition(op[2:]):next_pc=target
    elif op=='blr':write('x30',pc+4);next_pc=read(arg)
    elif op=='ret':next_pc=read(arg or 'x30')
    elif op!='nop':raise AssertionError((hex(pc),op,arg))
    pc=next_pc
assert read('w0')==0,('compiled C fixture failed',read('w0'))
assert regs['sp']==0x80000000,hex(regs['sp'])
report={'status':'PASS','compiled_arm64_fixture':True,'instructions_executed':steps,
    'human_arrival_orders':52,'players':52,'squads':13,'members_per_squad':4,
    'checks':['roster capacity','human first/last/every intermediate arrival',
    'duplicate death ignored','whole-squad elimination ranks','last surviving squad wins','round reset',
    'production managed-array builder: 13 independent enemy lists and allied lists'],
    'unity_integration':'DEVICE_TEST_PENDING'}
(root/'audit/squad-rules-0.14.51.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
