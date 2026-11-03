"""Check the rendering reset API against the shipped stripped Unity metadata."""
from pathlib import Path
import struct,json
root=Path(__file__).resolve().parents[1];m=(root/'romfs/game/bin/Data/Managed/Metadata/global-metadata.dat').read_bytes()
def table(i):return struct.unpack_from('<II',m,8+i*8)
so,_=table(2);to,tn=table(19);mo,_=table(5)
def text(i):return m[so+i:m.index(0,so+i)].decode()
methods={}
for i in range(tn//92):
 t=to+i*92;name=text(struct.unpack_from('<I',m,t)[0]);namespace=text(struct.unpack_from('<I',m,t+4)[0])
 if (namespace,name) not in [('UnityEngine','CanvasRenderer'),('UnityEngine.UI','Image'),('UnityEngine.UI','Graphic')]:continue
 start=struct.unpack_from('<I',m,t+40)[0];count=struct.unpack_from('<H',m,t+68)[0]
 methods[name]=set()
 for j in range(count):
  z=mo+(start+j)*32;methods[name].add((text(struct.unpack_from('<I',m,z)[0]),struct.unpack_from('<H',m,z+30)[0]))
required={'CanvasRenderer':[('Clear',0),('SetAlphaTexture',1)],'Image':[('get_sprite',0),('get_overrideSprite',0),('set_sprite',1),('set_overrideSprite',1),('set_type',1),('set_preserveAspect',1)],'Graphic':[('get_material',0),('set_material',1),('get_canvasRenderer',0),('SetAllDirty',0),('Rebuild',1),('set_color',1)]}
for klass,calls in required.items():
 for call in calls:assert call in methods[klass],('stripped API',klass,call)
source=(root/'source/ai_profile.c').read_text(encoding='utf-8');assert 'api.method(klass,"Create",' not in source
report={'version':'0.14.90','status':'PASS','verified_methods':sum(map(len,required.values())),'Sprite_Create_dependency':False,'unity_rendering':'DEVICE_TEST_PENDING'}
(root/'audit/crosshair-api-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
