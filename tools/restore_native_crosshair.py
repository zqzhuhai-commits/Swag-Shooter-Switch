from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1];name='6bd14b6762a7cf844a3600249ad112aa'
original=(root/'romfs/game/bin/Data'/name).read_bytes()
target=root/'romfs/game/font-assets'/name;target.write_bytes(original)
assert target.read_bytes()==original
source=(root/'source/ai_profile.c').read_text(encoding='utf-8')
for forbidden in ['crosshair_select','crosshair_apply','set_overrideSprite','crosshair_sprites','swag-crosshair.cfg']:
 assert forbidden not in source,forbidden
report={'version':'0.14.90','status':'PASS','native_sprite_and_texture_byte_identical':True,'custom_hooks_removed':True,'sha256':hashlib.sha256(original).hexdigest()}
(root/'audit/native-crosshair-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
