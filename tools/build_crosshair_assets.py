"""Build four code-defined UI reticles and patch settings previews."""
from pathlib import Path
import copy,hashlib,json,struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'work/unitypy'))
import UnityPy
from UnityPy.files.SerializedFile import FileIdentifier
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1];data=root/'romfs/game/bin/Data';out=root/'romfs/game/font-assets'
name='6bd14b6762a7cf844a3600249ad112aa'
target=next(iter(UnityPy.load(str(data/name)).files.values()))
# These are code-native HUD symbols. Shared geometry is used for previews and play.
atlas=Image.new('RGBA',(256,64))
for i in range(4):
 tile=Image.new('RGBA',(64,64));d=ImageDraw.Draw(tile)
 def cross():
  for xy in [(32,10,32,24),(32,40,32,54),(10,32,24,32),(40,32,54,32)]:
   d.line(xy,fill='white',width=2)
 if i in [0,3]:cross()
 if i in [1,3]:d.ellipse((29,29,35,35),fill='white')
 if i==2:d.ellipse((20,20,44,44),outline='white',width=2)
 atlas.paste(tile,(i*64,0))
 tile.save(root/'audit'/('crosshair-style-'+str(i)+'.png'))
texture=target.objects[1].read();texture.m_Name='JF_Crosshair_Atlas';texture.m_ColorSpace=1
texture.set_image(atlas,target_format=4);texture.save()
base=target.objects[2].read_typetree();reader=target.objects[2]
for i in range(4):
 sprite=copy.deepcopy(base);sprite['m_Name']='JF_Crosshair_'+str(i)
 sprite['m_Rect']={'x':float(i*64),'y':0.,'width':64.,'height':64.};sprite['m_PixelsToUnits']=100.
 sprite['m_PhysicsShape']=[];sprite['m_RenderDataKey']=({'data[0]':0,'data[1]':0,'data[2]':0,'data[3]':i+1},21300000+i)
 rd=sprite['m_RD'];rd['texture']={'m_FileID':0,'m_PathID':1}
 rd['textureRect']=dict(sprite['m_Rect']);rd['textureRectOffset']={'x':0.,'y':0.}
 rd['uvTransform']={'x':100.,'y':i*64+32.,'z':100.,'w':32.}
 rd['m_SubMeshes'][0].update(firstByte=0,indexCount=6,firstVertex=0,vertexCount=4)
 rd['m_IndexBuffer']=list(struct.pack('<6H',0,1,2,2,3,0))
 rd['m_VertexData']['m_VertexCount']=4
 rd['m_VertexData']['m_DataSize']=struct.pack('<12f',-.32,-.32,0,.32,-.32,0,.32,.32,0,-.32,.32,0)+struct.pack('<8f',i/4,0,(i+1)/4,0,(i+1)/4,1,i/4,1)
 obj=copy.copy(reader);obj.path_id=i+2;target.objects[i+2]=obj;obj.save_typetree(sprite)
(out/name).write_bytes(target.save())
check=next(iter(UnityPy.load(str(out/name)).files.values()))
assert check.objects[1].read().image.convert('RGBA').tobytes()==atlas.tobytes()
assert [check.objects[i+2].read_typetree()['m_Name'] for i in range(4)]==['JF_Crosshair_'+str(i) for i in range(4)]
# Apply after the Chinese/gameplay asset builders, preserving their changes.
level=next(iter(UnityPy.load(str(out/'level1')).files.values()))
external=object.__new__(FileIdentifier);external.path=name;external.temp_empty='';external.guid=bytes(16);external.type=0
level.externals.append(external);fid=len(level.externals)
for i,pid in enumerate([15756,15494,15457,15720]):
 raw=bytearray(level.objects[pid].get_raw_data());struct.pack_into('<4f',raw,44,1,1,1,1);struct.pack_into('<iq',raw,72,fid,i+2)
 level.objects[pid].set_raw_data(raw)
 selected=[15525,15749,15765,15552][i]
 raw=bytearray(level.objects[selected].get_raw_data());struct.pack_into('<4f',raw,44,1,.8,.2,1);struct.pack_into('<iq',raw,72,fid,i+2)
 level.objects[selected].set_raw_data(raw)
t=level.objects[3533].read_typetree();t['m_IsActive']=False;level.objects[3533].save_typetree(t)
(out/'level1').write_bytes(level.save())
report={'version':'0.14.90','status':'PASS','styles':4,'previews_match_game':True,'texture':'RGBA32','sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()}
(root/'audit/crosshair-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
