"""Check binary entry guards, the native API name, and entry-point geometry.

Geometry uses expressions extracted from production C with host math. This is
not execution of the compiled ARM64 movement hook or a Unity navigation test.
"""
from pathlib import Path
import json,math,re,struct,random
r=Path(__file__).resolve().parents[1]
s=(r/'source/zone_ai.inc').read_text(encoding='utf-8')
b=(r/'romfs/game/libil2cpp.so').read_bytes()
assert b'UnityEngine.AI.NavMesh::SamplePosition_Injected(UnityEngine.Vector3&,UnityEngine.AI.NavMeshHit&,System.Single,System.Int32)' in b
phoff=struct.unpack_from('<Q',b,32)[0]
segments=[struct.unpack_from('<II6Q',b,phoff+i*56) for i in range(struct.unpack_from('<H',b,56)[0])]
for a,words in [(0x16a8fec,[0x6dbb33ed,0x6d012beb,0x6d0223e9,0xa9034ff4]),
                (0x16a9330,[0xf81e0ff3,0xa9017bfd,0x910043fd,0xaa0003f3]),
                (0x16a9374,[0xfc1d0fe8,0xa9014ff4,0xa9027bfd,0x910083fd])]:
 p=next(p for p in segments if p[0]==1 and p[3]<=a and a+16<=p[3]+p[5]);offset=p[2]+a-p[3]
 assert b[offset:offset+16]==struct.pack('<4I',*words),hex(a)
fraction=re.search(r'float fraction=([^;]+);',s)[1].replace('bot->personality','personality')
ratio=re.search(r'float ratio=([^;]+);',s)[1].replace('sqrtf','sqrt')
def expression(text,values):
 return eval(re.sub(r'(\d+(?:\.\d+)?)f\b',r'\1',text),{'__builtins__':{},'sqrt':math.sqrt},values)
rng=random.Random(53)
for i in range(5000):
 radius=rng.uniform(1,3000);angle=rng.uniform(-math.pi,math.pi);d=radius*rng.uniform(.901,10)
 values={'r':radius,'distance':d*d,'personality':rng.uniform(.9,1.1)}
 values['fraction']=expression(fraction,values);scale=expression(ratio,values)
 x,z=d*math.cos(angle)*scale,d*math.sin(angle)*scale
 assert .5999*radius<=math.hypot(x,z)<=.7001*radius
 assert math.hypot(x,z)<.9*radius
for required in ['zone_distance_squared(point,zone_ai.center)>r*r*0.81f',
                 'api.write(behaviour,field,&previous)', 'if(!found)return 0;',
                 'get_RestrictedAreaApplicable','usingDynamicObject','inParkour','isDodging']:
 assert required in s,required
report={'status':'PASS','binary_entry_guards':3,'native_navmesh_api_name':True,
        'geometry_expression_model_cases':5000,'compiled_hook_and_unity_navigation':'DEVICE_TEST_PENDING'}
(r/'audit/ai-zone-navigation-0.14.58.json').write_text(json.dumps(report,indent=2))
print(report)
