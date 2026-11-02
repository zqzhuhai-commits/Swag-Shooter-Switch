"""Replace the actual boot splash sprite texture; requires UnityPy and Pillow."""
from pathlib import Path
import UnityPy
from PIL import Image

root=Path(__file__).resolve().parents[1]
env=UnityPy.load(str(root/'romfs/game/bin/Data/globalgamemanagers.assets'))
obj=next(o for o in env.objects if o.path_id==3)
tex=obj.read()
assert tex.m_Name=='bloodrivals' and (tex.m_Width,tex.m_Height)==(512,512)
image=Image.open(root/'artwork/brand-localized/startup-logo.png').convert('RGBA')
tex.set_image(image.resize((512,512),Image.Resampling.LANCZOS),target_format=4,mipmap_count=1)
tex.save()
(root/'romfs/game/font-assets/globalgamemanagers.assets').write_bytes(env.file.save())
print('PASS boot splash texture replacement')
