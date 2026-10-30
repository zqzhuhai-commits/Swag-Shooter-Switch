from parse_localization import parse
from read_unity_assets import asset
from pathlib import Path
import struct,json,re
root=(Path(__file__).resolve().parents[1]/'romfs/game')
fix={
 'Alert Loot hole opens in 20 secs See minimap to locate it':'物资洞穴将在20秒后开启，请查看小地图确定位置。\r',
 'Save yourself Loot hole closes in 10 secs':'物资洞穴将在10秒后关闭，请尽快离开！',
 'Restricting the Play Area':'缩小游戏区！',
 'Restricting play area in 1 Min':'{[VALUE]}分钟后缩小游戏区域！',
 'Restricting play area in 1 sec':'{[VALUE]}秒后缩小游戏区域！',
 'Playzone shrink countdown has started Tap the map to view the new playzone':'缩小游戏区域的倒计时已开始，打开地图查看新的安全区。\r\n',
 'You were killed outside the Playzone':'你在安全区外被淘汰。\r\n',
 'Restrict Play Area':'缩小游戏区域',
 'Restrict Playarea':'缩小游戏区域',
 'Server':'游戏难度','SERVER':'游戏难度','Magnet Aim':'辅助瞄准',
 'EXIT':'退出','Exit':'退出','exit':'退出',
 'All vs All':'单人','ALL VS ALL':'单人','Team':'组队','TEAM':'组队','SwagShooter':'绝地精英','SWAG SHOOTER':'绝地精英',
 'OK':'确定','Yes':'确认','CLOSE':'关闭','CONNECT':'连接','Connect':'连接',
 'Collected':'已领取','Disabled':'已禁用','EARN':'获取','Finish':'完成',
 'PLAY':'开始游戏','Play':'开始游戏','Watch':'查看',
 'FEMALE':'女性','MALE':'男性','Kill':'击杀','RANK':'排名','VERSION':'版本',
 'WINS':'胜场','Wins':'胜场',
 'CHARACTER':'角色',
 'SWAGGER':'绝地精英','Swagger Trophy':'精英奖杯',
 'Class':'类别：','POWER':'威力','CLIP':'弹匣容量','RELOAD':'换弹速度','FIRE RATE':'射速',
 'Watch Video Ad':'直接领取奖励','WATCH A VIDEO':'领取奖励','Watch Ads':'领取奖励',
 'CRATE':'宝箱','Crate':'宝箱',
 'Free':'（免费）','FREE':'免费','FREE CASH':'免费现金','Free Cash':'免费现金',
 'FREE GOLD':'免费金币','FREE DIAMOND':'免费钻石','FREE CRATE':'免费宝箱',
 'SHOP':'商店','Shop':'商店','Cash':'现金','Gold':'金币','Diamond':'钻石','Diamonds':'钻石',
 'Activate Now':'立即激活','ACTIVATE NOW':'立即激活','ACTIVATED':'已激活','Activated':'已激活','ACTIVE':'生效中','Active':'生效中',
 'Upgrade Now':'立即升级','BUY NOW':'立即购买','Buy Now':'立即购买',
 'CLAIM':'领取','Claim':'领取','CLAIM BONUS':'领取奖励','Claim Bonus':'领取奖励',
 '2X CLAIM':'双倍领取','2x Claim':'双倍领取','2x Claim Bonus':'领取双倍奖励','2x Claim Reward':'领取双倍奖励',
 'Claim 2x Bonus':'领取双倍奖励','CLAIM LEVEL 2X BONUS':'领取双倍升级奖励','CLAIM ROUND 2X BONUS':'领取双倍对局奖励',
 'Claim Your Free Gifts':'领取免费礼包','Nothing to Claim':'暂无可领取奖励','LATER':'稍后',
 'Back':'返回','Alive':'存活','Sniper':'狙击枪','Loading':'加载中',
 'Auto Pickup':'自动拾取','Get 2 Free':'免费领取两个','GET 1 DIAMOND BONUS':'领取1颗钻石',
 'Extra MediPack':'额外医疗包','Health Booster':'生命强化','Boost EXP':'经验加成','Buy EXP Booster':'购买经验加成',
 'POWER PACK':'强化礼包','Activate Power Pack':'激活强化礼包','Power Pack Not Activated':'强化礼包\n未激活',
 'EXP BOOSTER PACK':'经验加成礼包','Survivor Package':'生存者礼包','Boombastic Pack':'爆破礼包',
 'ADS CRATE':'奖励宝箱','claimFreeCrate':'领取免费宝箱','Open Free Crate every 12 hours for bonuses':'每12小时可开启一次免费宝箱。',
 'REVEAL GUN':'揭晓武器','Best Surprise Gun':'惊喜武器','Rent for Free':'免费租用',
 'Unlock at Level 3':'3级解锁','Unlock at Level 5':'5级解锁','Fuel Vehicle':'为载具加油',
 'Ad':'广告','Ok':'确定','GRAB IT NOW':'立即领取',
 'NOT ENOUGH CASH':'现金不足','NOT ENOUGH GOLD':'金币不足','NOT ENOUGH DIAMONDS':'钻石不足',
 'Increase in Armour Health':'护甲值提升%','Increase in Health':'生命值提升%','increase in Speed':'速度提升%',
 'less Headshot Damage':'受到的爆头伤害降低%','less damage of Pulse Pistol':'受到的脉冲手枪伤害降低%',
 'After each kill 10 less damage for 3s':'每次击杀后，受到的伤害降低10%，持续3秒。',
 'After each kill 20 less damage for 3s':'每次击杀后，受到的伤害降低20%，持续3秒。',
 'Make sure you activate your Power Pack to kill your rivals easily and be the last survivor':'激活强化礼包，提升战斗能力，争取成为最后的生存者。',
 'This is a great opportunity  Grab the double Cash Gold and Exp   Dont miss it':'领取双倍现金、金币和经验奖励！',
 'This is a unique opportunity  Dont miss it  Get free Propack EXP Booster Remove 2x Claim  Exit Ads':'免费获取强化礼包和经验加成，免除双倍领奖和退出时的广告。',
 'You have chance to rebirth Still hope to be 1 Respawn Now':'你还有重生机会！\n重返战场，争夺第一！',
 'You have chance to rebirthStill hope to be 1Respawn Now':'你还有重生机会！\n重返战场，争夺第一！',
}
def enc(s):
 b=s.encode();return struct.pack('<I',len(b))+b+b'\0'*((-len(b))%4)
def rebuild(b,objs,head,changes):
 out=bytearray(b[:head['data']])
 for o in objs:
  out.extend(b'\0'*((-len(out))%8));start=len(out)
  raw=changes.get(o['pid'],b[o['offset']:o['offset']+o['size']]);out.extend(raw)
  struct.pack_into('<II',out,o['entry'],start-head['data'],len(raw))
 struct.pack_into('>I',out,4,len(out));return bytes(out)
name='2e5f135ec90b47e4e8ca141d7c6c0e9b'
b,objs,head,terms,end=parse(root/'bin/Data'/name)
obj=objs[0];raw=b[obj['offset']:obj['offset']+obj['size']];edits=[];mapping={};revised=[]
for t in terms:
 zh=fix.get(t['key'],t['chinese'])
 zh=zh.replace('限制游戏区域','缩小游戏区域').replace('限制游戏区','缩小游戏区') if zh else zh
 if zh:
  mapping[t['english']]=zh;mapping[t['key']]=zh
 if zh!=t['chinese']:
  a,z,_=t['langs'][10];edits.append((a,z,enc(zh)));revised.append({'key':t['key'],'before':t['chinese'],'after':zh})
  if len(t['touch'])==15:
   a,z,_=t['touch'][10]
   if _ :edits.append((a,z,enc(zh)))
for a,z,v in sorted(edits,reverse=True):raw=raw[:a]+v+raw[z:]
out=root/'font-assets'/name;out.write_bytes(rebuild(b,objs,head,{obj['pid']:raw}))
_,_,_,check,_=parse(out);assert len(check)==574
assert all(t['chinese']==fix[t['key']] for t in check if t['key'] in fix)
mapping.update({
 'SwagShooter':'绝地精英','Swag Shooter':'绝地精英','SWAGSHOOTER':'绝地精英',
 'CASH':'现金','GOLD':'金币','DIAMOND':'钻石','Cash & Gold':'现金和金币','ARMORY':'军械库','GUNS':'武器',
 'GET FREE CASH':'领取免费现金','GET FREE GOLD':'领取免费金币','GET FREE DIAMONDS':'领取免费钻石',
 'Power Pack':'强化礼包','Propack':'强化礼包','EXP BOOSTER IS DISABLED':'经验加成未启用','EXP Booster':'经验加成','EXP Booster Pack':'经验加成礼包',
 'BRONZE CRATE':'青铜宝箱','COPPER CRATE':'铜制宝箱','SILVER CRATE':'白银宝箱','PLATINUM CRATE':'铂金宝箱','ROYAL GOLD CRATE':'皇家黄金宝箱',
 'REMOVE ALL ADS':'移除全部广告','COLLECT':'领取','Claim Round 2X Bonus':'领取双倍对局奖励','All':'全部','Today':'今天','Tomorrow':'明天',
 'Ammo Catagory':'弹药类别','Assult Rifle':'突击步枪','SHOTGUN':'霰弹枪','Rifle':'步枪','Knife':'匕首','Axe':'斧头','Crossbow':'弩','Ballistic Shield':'防弹盾',
 'Informations':'信息','Achivements':'成就','HEADSHOT':'爆头','Player Id :':'玩家编号：','Unlock at Level. 5':'5级解锁','Write your signature...':'输入个性签名……',
 'Forward/Back':'前进/后退','Left/Right':'左转/右转','Primary Gun Name':'主武器名称',
 'STORM SURVIVAL':'风暴生存','SNIPER MODE':'狙击模式','KNIFE MODE':'匕首模式','AXE MODE':'斧头模式','FREE MODE':'自由模式','CLASSIC MODE':'经典模式',
 'Checking Update Data...':'正在检查更新……','Enjoy Playing with New rolled out Update.':'体验新版本内容。',
 'Restricting play area in 10 sec!':'安全区将在10秒后缩小！',
 'Playzone shrink countdown has started. Tap the map to view the new playzone.\n':'安全区缩小倒计时已开始，打开地图查看新的安全区。\n',
 'MediPack & Soundwaves':'医疗包和声波装置','Smoke & Soundwave':'烟雾弹和声波装置',
 'ASIA':'亚洲','Asia':'亚洲','English':'英语','Chinese':'中文','Chinese (Simplified)':'简体中文',
 'Thai':'泰语','Portuguese':'葡萄牙语','German':'德语','French':'法语','Spanish':'西班牙语','Polish':'波兰语',
 'Turkish':'土耳其语','Russian':'俄语','Arabic':'阿拉伯语','Japanese':'日语','Korean':'韩语','Indonesian':'印度尼西亚语','Vietnamese':'越南语',
})
def label_translation(text):
 if '限制游戏区域' in text or '限制游戏区' in text:
  return text.replace('限制游戏区域','缩小游戏区域').replace('限制游戏区','缩小游戏区')
 if text in mapping:return mapping[text]
 result=text
 for english,chinese in [('EnemyDetector','敌人探测器'),('DIAMONDS','钻石'),('DIAMOND','钻石'),('Diamonds','钻石'),('Diamond','钻石'),('CASH','现金'),('Cash','现金'),('GOLD','金币'),('Gold','金币'),('MINUTE','分钟'),('MediPack','医疗包'),('Grenades','手雷'),('Grnade','手雷'),('Grenade','手雷'),('SmokeBomb','烟雾弹'),('Smokebomb','烟雾弹'),('Smoke','烟雾弹'),('FlashBomb','闪光弹'),('Flashbomb','闪光弹'),('SoundWave','声波装置'),('Soundwave','声波装置'),('EXP','经验值')]:
  result=re.sub(r'\b'+english+r'\b',chinese,result)
 result=re.sub(r'\bGET\s+(\d+)\s*钻石',r'领取\1颗钻石',result)
 result=re.sub(r'(\d+)%\s*(?:OFF|Off)',lambda m:format((100-int(m[1]))/10,'g')+'折',result)
 result=result.replace('经验值 Booster','经验加成').replace('1 Day','1天').replace('2 Hours','2小时').replace('24 Hours','24小时')
 result=result.replace('Scope4X','4倍镜').replace('Scope6X','6倍镜').replace('Scope8X','8倍镜')
 result=result.replace('You received 2000 现金.','你获得了2000现金。')
 return result if result!=text else None
# Unity UI Text has a fixed 2019 layout here. Match the known script reference,
# m_Text offset and exact end, rather than replacing localization keys/names.
b,objs,head=asset(root/'bin/Data/level1');sample=next(o for o in objs if o['pid']==11412)
script=b[sample['offset']+16:sample['offset']+28];changes={};labels=[];unknown=set()
for o in objs:
 if o['type']!=114 or o['size']<132:continue
 raw=b[o['offset']:o['offset']+o['size']]
 if raw[16:28]!=script:continue
 n=struct.unpack_from('<I',raw,128)[0]
 if 132+((n+3)&~3)!=len(raw):continue
 try:text=raw[132:132+n].decode()
 except UnicodeError:continue
 translated=label_translation(text)
 if o['pid']==11890:translated='退出游戏'
 # Inspected store purchase/price labels only, not category tabs. The offline
 # product handler grants these items locally, so no remote prices are shown.
 if o['pid'] in {10883,12048,12310,13225,13382,13768,14244}:
  translated='免费解锁' if o['pid']==14244 else '免费获取'
  raw=bytearray(raw);struct.pack_into('<iq',raw,72,7,3);raw=bytes(raw)
 if translated and translated!=text:
  changes[o['pid']]=raw[:128]+enc(translated);labels.append({'pid':o['pid'],'before':text,'after':translated})
 elif re.search('[A-Za-z]{3}',text):unknown.add(text)
# This popup button has a dedicated localizer; prevent it restoring Settings.
localizer=next(o for o in objs if o['pid']==11889)
local_raw=bytearray(b[localizer['offset']:localizer['offset']+localizer['size']]);local_raw[12]=0;changes[11889]=bytes(local_raw)
(root/'font-assets/level1').write_bytes(rebuild(b,objs,head,changes))
_,newobjs,_=asset(root/'font-assets/level1');assert len(newobjs)==len(objs)
report={'terms_revised':revised,'fixed_scene_labels':labels,'remaining_scene_english':sorted(unknown),'device_validation':'pending'}
(root.parents[1]/'audit/chinese-0.14.58.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS',len(revised),'translation corrections;',len(labels),'fixed scene labels; remaining English:',len(unknown))
