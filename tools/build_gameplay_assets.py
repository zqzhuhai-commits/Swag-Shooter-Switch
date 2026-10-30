"""Build the inspected offline match scene override without changing source assets."""
from pathlib import Path
import struct,json,hashlib
from read_unity_assets import asset
root=Path(__file__).resolve().parents[1]
source=root/'romfs/game/bin/Data/level2'
b,objects,head=asset(source)
o=next(o for o in objects if o['pid']==31554)
assert o['type']==114 and o['size']==268
p=o['offset']
assert struct.unpack_from('<iq',b,p+16)==(1,140)
assert struct.unpack_from('<3f',b,p+156)==(15.0,1.0,10.0)
out=bytearray(b)
# RestrictedArea: percentageDecrement stays 15%; interval is in minutes.
struct.pack_into('<2f',out,p+160,3.0,90.0)
assert out[:p+160]==b[:p+160] and out[p+168:]==b[p+168:]
target=root/'romfs/game/font-assets/level2';target.write_bytes(out)
check,parsed,_=asset(target)
assert len(parsed)==len(objects)
assert struct.unpack_from('<3f',check,p+156)==(15.0,3.0,90.0)
report={'version':'0.14.58','scene':'level2','object':31554,'old_interval_seconds':60,'interval_seconds':180,'old_shrink_seconds':10,'shrink_seconds':90,'progressive_wait_seconds':[180,120,90,75,60,45],'progressive_shrink_seconds':[90,75,60,45,30,30],'fallback_percentage_decrement':15,'runtime_shrink_current_percent':[25,35,40,45,50,50],'unrelated_bytes_unchanged':True,'sha256':hashlib.sha256(check).hexdigest(),'device_validation':'PENDING'}
(root/'audit/gameplay-timing-0.14.58.json').write_text(json.dumps(report,indent=2))
print('PASS scene timing: interval 60 -> 180 seconds; first shrink 10 -> 90 seconds; native hook advances later stages; other scene bytes unchanged')
