"""Encode approved localized artwork. Requires UnityPy and Pillow on Python 3.13.
Original game resources are read only; output uses the existing asset overlay.
"""
from pathlib import Path
import json
import UnityPy
from PIL import Image

root=Path(__file__).resolve().parents[1]
entries=[('Loadingscreen_00','ca1702706a2a1624c892d8bc3d92ada4'),
 ('Loadingscreen_01','1d97c211220422741a6766f203feb2f3'),
 ('Loadingscreen_02','9f1a47f5bcd0d01439e24095f6b7771b'),
 ('Loadingscreen_03','6b932317805ea184cb8e8cc4f778a2b8'),
 ('Loadingscreen_04','23a4e96697f552c40b3fa5b1ca2ef0b7'),
 ('MainmenuHeadding','d318b18682588384e9588f232fd48763')]
report=[]
for name,file in entries:
 env=UnityPy.load(str(root/'romfs/game/bin/Data'/file))
 obj=next(o for o in env.objects if o.path_id==1);tex=obj.read()
 size=(tex.m_Width,tex.m_Height)
 image=Image.open(root/'artwork/brand-localized'/(name+'.png')).convert('RGBA')
 image=image.resize(size,Image.Resampling.LANCZOS)
 fmt=4 if name=='MainmenuHeadding' else 34
 tex.set_image(image,target_format=fmt,mipmap_count=1);tex.save()
 dest=root/'romfs/game/font-assets'/file;dest.write_bytes(env.file.save())
 check=UnityPy.load(str(dest));again=next(o for o in check.objects if o.path_id==1).read()
 assert (again.m_Width,again.m_Height)==size and again.m_TextureFormat==fmt
 again.image.save(root/'artwork/brand-localized'/(name+'-encoded.png'))
 if fmt==4:
  assert again.image.getchannel('A').getextrema()==(0,255)
 report.append({'file':file,'name':name,'size':size,'format':fmt,'original_preserved':True})
(root/'audit/brand-images-0.14.90.json').write_text(json.dumps(report,indent=2))
print('PASS six textures round-trip; original dimensions; transparent menu logo')
