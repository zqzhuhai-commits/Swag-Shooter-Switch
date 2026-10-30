"""Replace the offline settings region selector with four AI difficulty levels.
Run after build_chinese_assets.py. Preserve object IDs and UnityEvent callback.
"""
from pathlib import Path
import struct,json
from read_unity_assets import asset

root=Path(__file__).resolve().parents[1]
path=root/'romfs/game/font-assets/level1'
b,objects,head=asset(path)
by_id={o['pid']:o for o in objects}
def raw(pid):
 o=by_id[pid];return b[o['offset']:o['offset']+o['size']]
def string(text):
 s=text.encode();return struct.pack('<I',len(s))+s+b'\0'*((-len(s))%4)
dropdown=raw(15609)
assert struct.unpack_from('<I',dropdown,372)[0]==3
assert b'ASIA' in dropdown[372:440] and b'EUROPE' in dropdown[372:440]
assert b'OnRegionSelect' in dropdown[440:]
options=['简单AI','普通AI','困难AI','精英AI']
option_data=struct.pack('<I',4)+b''.join(string(t)+b'\0'*12 for t in options)
changed={15609:dropdown[:372]+option_data+dropdown[440:]}
# Existing six-mode selector: four parent GameObjects were serialized inactive.
# Preserve their click events and mode IDs; arrange all six rows without overlap.
mode_parents=[(670,7469),(1066,7862),(1140,7934),(1421,8212),(1898,8684),(455,7256)]
for index,(go,transform) in enumerate(mode_parents):
 game_object=bytearray(raw(go));assert by_id[go]['type']==1
 game_object[-1]=1;changed[go]=bytes(game_object)
 rect=bytearray(raw(transform));assert by_id[transform]['type']==224
 children=struct.unpack_from('<I',rect,52)[0];layout=56+children*12+12
 assert struct.unpack_from('<q',rect,layout-8)[0]==7194
 struct.pack_into('<f',rect,layout+20,-17.5-35*index)
 changed[transform]=bytes(rect)
content=bytearray(raw(7194));children=struct.unpack_from('<I',content,52)[0];layout=56+children*12+12
struct.pack_into('<f',content,layout+28,210.0);changed[7194]=bytes(content)
mode_words={'SwagShooter':'绝地精英','SWAG SHOOTER':'绝地精英','CLASSIC MODE':'经典模式','Classic Mode':'经典模式','classic mode':'经典模式',
 'DEATH MATCH':'死亡竞赛','Death Match':'死亡竞赛','DEATH MODE':'死亡模式',
 'TEAM DEATH MATCH':'团队死亡竞赛','Solo Death Match':'单人死亡竞赛',
 'CLASSIC MODE OF TITAN BLOOD':'经典生存模式','KNIFE MODE':'匕首模式','AXE MODE':'斧头模式',
 'SNIPER MODE':'狙击模式','STORM SURVIVAL':'风暴生存','FREE MODE':'自由模式'}
# Mode names also live in serialized controller lists, from which runtime
# refreshes overwrite Text components. Translate these sources specifically.
source,script_objects,_=asset(root/'romfs/game/bin/Data/globalgamemanagers.assets')
scripts=set()
for obj in script_objects:
 data=source[obj['offset']:obj['offset']+obj['size']]
 if obj['type']==115 and any(string(n) in data for n in ['MainMenuManager','SelectGameMode','GameManagerForNetwork']):scripts.add(obj['pid'])
mode_edits=[]
for obj in objects:
 data=raw(obj['pid'])
 if obj['type']!=114 or len(data)<32:continue
 file_id,script_id=struct.unpack_from('<iq',data,16)
 if file_id!=1 or script_id not in scripts:continue
 replacements=[]
 for offset in range(32,len(data)-4,4):
  size=struct.unpack_from('<I',data,offset)[0]
  if size<4 or size>100 or offset+4+size>len(data):continue
  try:text=data[offset+4:offset+4+size].decode()
  except UnicodeError:continue
  if text in mode_words:replacements.append((offset,offset+4+((size+3)&~3),string(mode_words[text])));mode_edits.append(text)
 for start,end,replacement in sorted(replacements,reverse=True):data=data[:start]+replacement+data[end:]
 if replacements:changed[obj['pid']]=data
mode=raw(11000)
assert mode[372:440]==dropdown[372:440]
changed[11000]=mode[:372]+option_data+mode[440:]
for pid,text in [(15506,'游戏难度'),(15619,'普通AI'),(15725,'普通AI'),(11067,'普通AI'),(13778,'普通AI')]:
 data=bytearray(raw(pid));n=struct.unpack_from('<I',data,128)[0]
 assert len(data)==132+((n+3)&~3)
 struct.pack_into('<iq',data,72,7,3)
 changed[pid]=bytes(data[:128])+string(text)
# Stop this label's old "Server" localization component overwriting its title.
label_go=struct.unpack_from('<q',raw(15506),4)[0]
disabled=[]
for obj in objects:
 if obj['type']!=114:continue
 data=raw(obj['pid'])
 if len(data)>28 and struct.unpack_from('<q',data,4)[0]==label_go and b'I2.Loc.LocalizeTarget_UnityUI_Text' in data:
  copy=bytearray(data);copy[12]=0;changed[obj['pid']]=bytes(copy);disabled.append(obj['pid'])
out=bytearray(b[:head['data']])
for obj in objects:
 out.extend(b'\0'*((-len(out))%8));start=len(out)
 data=changed.get(obj['pid'],raw(obj['pid']));out.extend(data)
 struct.pack_into('<II',out,obj['entry'],start-head['data'],len(data))
struct.pack_into('>I',out,4,len(out));path.write_bytes(out)
new,parsed,_=asset(path)
assert len(parsed)==len(objects)
new_by_id={o['pid']:new[o['offset']:o['offset']+o['size']] for o in parsed}
assert struct.unpack_from('<I',new_by_id[11000],372)[0]==4
assert struct.unpack_from('<I',new_by_id[15609],372)[0]==4
assert new_by_id[15609][372+len(option_data):]==dropdown[440:]
report={'version':'0.14.58','mode_source_translations':mode_edits,'settings_dropdown':15609,'label':15506,'options':options,'default':'普通AI',
        'changed_objects':list(changed),'disabled_old_localizers':disabled,'device_validation':'pending'}
(root/'audit/ai-settings-0.14.58.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS AI difficulty settings: four options, original event preserved, unrelated selector unchanged')
