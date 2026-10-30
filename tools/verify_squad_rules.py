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
    finish_builder=source[source.index('static int squad_allow_finish('):source.index('static int squad_register(')]
    zone_source=(root/'source/zone_ai.inc').read_text(encoding='utf-8')
    assert '"get_Item"' not in zone_source and 'squad_ref(game,"playerList")' not in zone_source
    collector=zone_source[zone_source.index('static unsigned zone_collect_registered('):zone_source.index('static void zone_actors_poll(')]
    extra=Path(temporary)/'array.c'
    extra.write_text('''#include <stdint.h>
#include <string.h>
typedef struct {uint64_t header[3],length;int values[12];} Array;
static Array arrays[26];static unsigned next_array;
static const void *corlib(void) {return (void *)1;}
static void *klass(const void *image,const char *ns,const char *name) {(void)image;(void)ns;(void)name;return (void *)2;}
static void *new_array(void *k,uintptr_t length) {(void)k;if(next_array>=26)return 0;Array *a=&arrays[next_array++];a->length=length;return a;}
static uintptr_t array_length(void *a) {return ((Array *)a)->length;}
static void *mock_target(uint32_t h) {
 if(h>=1 && h<=52)return (void *)(uintptr_t)(0x10000+16*h);
 if(h>=1001 && h<=1052)return (void *)(uintptr_t)(0x20000+16*(h-1000));
 return 0;
}
static struct {
 const void *(*corlib)(void);
 void *(*class_named)(const void *,const char *,const char *);
 void *(*array_new)(void *,uintptr_t);
 uintptr_t (*array_length)(void *);
 void *(*target)(uint32_t);
} api={corlib,klass,new_array,array_length,mock_target};
''' + array_builder + '''
#include "squad_rules.h"
#define fprintf(...) (0)
static void *output;
static struct {SquadRules rules;int ready,finished,end_blocked;struct {uint32_t player;} actors[52];} squads;
static int active=1,end_calls,round_calls,death_calls,rank_calls;
static int squad_mode_active(void) {return active;}
static void squad_apply_ranks(void) {++rank_calls;}
static void end_original(void *s,void *m) {(void)s;(void)m;++end_calls;}
static void round_original(void *s,void *m) {(void)s;(void)m;++round_calls;}
static void died_original(void *s,void *m) {(void)s;(void)m;++death_calls;}
static int won_original(void *s,void *m) {(void)s;(void)m;return 17;}
static void (*squad_end_time_original)(void *,void *)=end_original;
static void (*squad_round_end_original)(void *,void *)=round_original;
static void (*squad_player_died_original)(void *,void *)=died_original;
static int (*squad_local_won_original)(void *,void *)=won_original;
''' + finish_builder + '''
static struct {uint32_t player;} zone_actors[128];
static struct {uint32_t base;} bots[128];
static void *object_field(void *base,const char *field) {
 (void)field;uintptr_t p=(uintptr_t)base;
 return p>=0x20010 && p<=0x20340 ? (void *)(p-0x10000) : 0;
}
''' + collector + '''
static int test_registry(void) {
 void *players[128];active=1;squads.ready=1;
 for(unsigned i=0;i<52;++i) {squads.actors[i].player=i+1;bots[i].base=1001+i;}
 for(unsigned i=0;i<16;++i)zone_actors[i].player=i+1;
 zone_actors[16].player=999;bots[52].base=999;
 for(unsigned capacity=1;capacity<=52;++capacity) {
  players[capacity]=(void *)0xdead;
  if(zone_collect_registered(players,capacity)!=capacity || players[capacity]!=(void *)0xdead)return 80;
  for(unsigned i=0;i<capacity;++i)if(players[i]!=mock_target(i+1))return 81;
 }
 squads.ready=0;active=0;
 if(zone_collect_registered(players,128)!=52)return 82;
 for(unsigned i=0;i<52;++i)if(players[i]!=mock_target(i+1))return 83;
 if(zone_collect_registered(players,0) || zone_collect_registered(0,128))return 84;
 return 0;
}
static int test_finish_hooks(void) {
 squads.ready=1;squad_rules_join(&squads.rules,1);
 for(unsigned i=1;i<52;++i)squad_rules_join(&squads.rules,0);
 for(unsigned i=4;i<24;++i)squad_rules_death(&squads.rules,i);
 squad_end_time(0,0);squad_round_end(0,0);squad_player_died(0,0);
 if(end_calls || round_calls || death_calls || rank_calls || squad_local_won(0,0))return 50;
 for(unsigned i=24;i<52;++i)squad_rules_death(&squads.rules,i);
 squad_end_time(0,0);squad_round_end(0,0);squad_round_end(0,0);squad_player_died(0,0);
 if(end_calls!=1 || round_calls!=1 || rank_calls!=1 || squad_local_won(0,0)!=1)return 51;
 active=0;squad_end_time(0,0);squad_round_end(0,0);squad_player_died(0,0);
 if(end_calls!=2 || round_calls!=2 || death_calls!=1 || squad_local_won(0,0)!=17)return 52;
 return 0;
}
extern int run_squad_rules_tests(void);
extern int run_population_tests(void);
int run_all_tests(void) {
 int result=run_squad_rules_tests();if(result)return result;
 result=run_population_tests();if(result)return result;
 result=test_finish_hooks();if(result)return result;
 result=test_registry();if(result)return result;
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
    population=Path(temporary)/'population.c'
    population.write_text((root/'tests/squad_population_fixture.c').read_text(encoding='utf-8').replace('/* PRODUCTION_WRAPPERS */', source[source.index('static void squad_population_apply('):source.index('static void squad_hud_alive(')]),encoding='utf-8')
    subprocess.run([prefix+'gcc.exe','-O0','-ffreestanding','-fno-builtin',
        '-fno-stack-protector','-mgeneral-regs-only','-nostdlib','-I',str(root/'include'),
        '-Wl,-e,run_all_tests',str(root/'tests/squad_rules_fixture.c'),str(extra),str(population),'-o',str(elf)],check=True)
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
        parts=[v.strip() for v in arg.split(',')];d,a,b=parts[:3];av,bv=read(a),read(b)
        if len(parts)>3:
            extend=parts[3];shift=int(extend.split('#')[1]) if '#' in extend else 0
            if extend.startswith('uxtw'):bv&=0xffffffff
            elif extend.startswith('sxtw'):bv=(bv&0xffffffff)-(1<<32 if bv&(1<<31) else 0)
            elif not extend.startswith('lsl'):raise AssertionError((hex(pc),op,arg))
            bv<<=shift
        value={'add':lambda:av+bv,'sub':lambda:av-bv,'and':lambda:av&bv,
            'lsl':lambda:av<<bv,'lsr':lambda:av>>bv}[op]()
        write(d,value)
    elif op=='udiv':
        d,a,b=[v.strip() for v in arg.split(',')];write(d,read(a)//read(b) if read(b) else 0)
    elif op in ('mul','umull'):
        d,a,b=[v.strip() for v in arg.split(',')];write(d,read(a)*read(b))
    elif op=='msub':
        d,a,b,c=[v.strip() for v in arg.split(',')];write(d,read(c)-read(a)*read(b))
    elif op=='orr':
        d,a,b=[v.strip() for v in arg.split(',')];write(d,read(a)|read(b))
    elif op=='movk':
        d,a,shift=arg.split(', ');bits=int(shift.split('#')[1]);write(d,(read(d)&~(65535<<bits))|(read(a)<<bits))
    elif op=='sxtw':
        d,a=arg.split(', ');v=read(a)&0xffffffff;write(d,v-(1<<32) if v&(1<<31) else v)
    elif op=='mov':d,a=arg.split(', ');write(d,read(a))
    elif op in ('adr','adrp'):
        d,a=arg.split(', ',1);write(d,int(a.split()[0],16))
    elif op in ('cmp','cmn'):
        a,b=arg.split(', ');compare(read(a),read(b),32 if a.startswith('w') else 64,op=='cmn')
    elif op=='cset':d,c=arg.split(', ');write(d,int(condition(c)))
    elif op=='csel':
        d,a,b,c=arg.split(', ');write(d,read(a) if condition(c) else read(b))
    elif op in ('ldr','str','ldrb','strb','ldrsw'):
        register,operand=arg.split(', ',1);at,base,post=address(operand)
        size=1 if op.endswith('b') else 4 if op=='ldrsw' or register.startswith('w') else 8
        if op.startswith('ld'):
            value=load(at,size)
            if op=='ldrsw' and value&(1<<31):value-=1<<32
            write(register,value)
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
    'checks':['partial roster shows actual players only','deferred bot spawning enforces 51 bots after overwritten defaults','local HUD binds own team and corrects false death icons','roster capacity','human first/last/every intermediate arrival',
    'duplicate death ignored','whole-squad elimination ranks','last surviving squad wins','round reset',
    'production managed-array builder: 13 independent enemy lists and allied lists',
    '32 survivors cannot end match or win', 'one enemy survivor blocks victory',
    'local elimination permits loss without a provisional winner',
    'production finish hooks reject premature callbacks and duplicate settlement',
    'non-classic modes retain original callbacks', 'worker CPU masks exclude system core',
    'health warning 30-percent threshold, monotonic opacity, smooth fade and upper bound',
    'zone damage fallback grace and native damage suppression',
    '36-survivor fixture: outside AI health depletion updates squad winner',
    'production actor collector: roster priority, deduplication, expired handles, capacities 1..52',
    'no dictionary indexer invocation in zone code'],
    'unity_integration':'DEVICE_TEST_PENDING'}
(root/'audit/squad-rules-0.14.58.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
