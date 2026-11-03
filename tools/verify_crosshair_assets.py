"""Independently verify built reticle resources and settings references."""
from pathlib import Path
import sys,struct,json
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'work/unitypy'))
import UnityPy
root=Path(__file__).resolve().parents[1];p=root/'romfs/game/font-assets'
name='6bd14b6762a7cf844a3600249ad112aa'
f=next(iter(UnityPy.load(str(p/name)).files.values()))
image=f.objects[1].read().image.convert('RGBA');assert image.size==(256,64)
assert all(a==0 or (r,g,b)==(255,255,255) for r,g,b,a in image.getdata()),'unexpected outline or black pixels'
styles=[image.crop((i*64,0,i*64+64,64)).tobytes() for i in range(4)]
assert len(set(styles))==4
for i in range(4):
 t=f.objects[i+2].read_typetree();assert t['m_Name']=='JF_Crosshair_'+str(i)
 assert t['m_RD']['texture']=={'m_FileID':0,'m_PathID':1}
 assert t['m_RD']['textureRect']=={'x':float(i*64),'y':0.,'width':64.,'height':64.}
 uv=struct.unpack_from('<8f',t['m_RD']['m_VertexData']['m_DataSize'],48)
 assert uv==(i/4,0,(i+1)/4,0,(i+1)/4,1,i/4,1),'sprite UVs must address its own style'
level=next(iter(UnityPy.load(str(p/'level1')).files.values()))
for i,pid in enumerate([15756,15494,15457,15720]):
 raw=level.objects[pid].get_raw_data();fid,sid=struct.unpack_from('<iq',raw,72)
 assert level.externals[fid-1].path==name and sid==i+2
 assert struct.unpack_from('<4f',raw,44)==(1,1,1,1)
 raw=level.objects[[15525,15749,15765,15552][i]].get_raw_data()
 assert struct.unpack_from('<iq',raw,72)==(fid,sid),'selected highlight must retain the same shape'
assert not level.objects[3533].read_typetree()['m_IsActive']
report={'version':'0.14.90','status':'PASS','distinct_styles':4,'black_or_outline_pixels':0,'settings_references_verified':8,'fifth_black_option_hidden':True,'four_style_uv_regions_verified':True,'hardware':'PENDING'}
(root/'audit/crosshair-verification-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
