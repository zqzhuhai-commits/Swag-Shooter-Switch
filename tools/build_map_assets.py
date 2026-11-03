"""Encode localized map artwork made with the built-in image-generation tool.
Python is used for Unity asset encoding and required size conversion only.
"""
from pathlib import Path
import json,hashlib,sys
import UnityPy
from PIL import Image
from read_unity_assets import asset
root=Path(__file__).resolve().parents[1]
source=root/'romfs/game/bin/Data/sharedassets2.assets'
output=root/'romfs/game/font-assets/sharedassets2.assets'
env=UnityPy.load(str(source))
tex=next(o for o in env.objects if o.path_id==189).read()
assert tex.m_Name=='minimap' and (tex.m_Width,tex.m_Height)==(1024,1024)
image=Image.open(root/'artwork/map-localized/minimap-zh.png').convert('RGBA').resize((1024,1024),Image.Resampling.LANCZOS)
tex.set_image(image,target_format=34,mipmap_count=1);tex.save();output.write_bytes(env.file.save())
check=UnityPy.load(str(output));again=next(o for o in check.objects if o.path_id==189).read()
assert (again.m_Width,again.m_Height,again.m_TextureFormat)==(1024,1024,34)
again.image.save(root/'artwork/map-localized/minimap-zh-encoded.png')
a,ao,_=asset(source);b,bo,_=asset(output);lookup={o['pid']:o for o in bo}
for o in ao:
 if o['pid']==189:continue
 q=lookup[o['pid']];assert a[o['offset']:o['offset']+o['size']]==b[q['offset']:q['offset']+q['size']],o['pid']
report={'status':'PASS','file':source.name,'texture':189,'dimensions':[1024,1024],'format':34,'all_other_serialized_objects_unchanged':True,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'artwork_method':'built-in image_gen text localization','map_overlay_alignment_device_validation':'PENDING'}
(root/'audit/map-localization-0.14.90.json').write_text(json.dumps(report,indent=2))
print('PASS map texture encoded; all other sharedassets2 objects byte-identical')
