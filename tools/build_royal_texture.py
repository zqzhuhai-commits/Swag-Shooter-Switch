"""Convert the identified Royal Knight banner without changing decoded pixels.
Requires UnityPy. Run on original resources before packaging.
"""
from pathlib import Path
import UnityPy

root=Path(__file__).resolve().parents[1]
name='4b23bbc02e9cbd541ae85e30d63e1c2c'
env=UnityPy.load(str(root/'romfs/game/bin/Data'/name))
obj=next(o for o in env.objects if o.path_id==1)
tex=obj.read()
assert tex.m_TextureFormat==65 and (tex.m_Width,tex.m_Height)==(1920,1080)
image=tex.image
tex.set_image(image,target_format=4,mipmap_count=1)
tex.save()
dest=root/'romfs/game/font-assets'/name
dest.write_bytes(env.file.save())
check=UnityPy.load(str(dest))
decoded=next(o for o in check.objects if o.path_id==1).read()
assert decoded.m_TextureFormat==4 and decoded.image.tobytes()==image.tobytes()
print('PASS Royal Knight banner: pixel-identical RGBA32 conversion')
