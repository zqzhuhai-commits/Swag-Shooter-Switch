from pathlib import Path
import struct,json
class Reader:
 def __init__(self,b,p=0):self.b=b;self.p=p
 def get(self,f):
  v=struct.unpack_from('<'+f,self.b,self.p);self.p+=struct.calcsize('<'+f);return v[0] if len(v)==1 else v
 def z(self):
  p=self.b.index(0,self.p);s=self.b[self.p:p].decode();self.p=p+1;return s
 def align(self):self.p=(self.p+3)&~3
def asset(path):
 b=Path(path).read_bytes();meta,size,ver,data=struct.unpack_from('>4I',b)
 assert 17<=ver<=21 and b[16]==0
 r=Reader(b,20);unity=r.z();platform=r.get('i');tree=r.get('?');tc=r.get('i');types=[]
 for i in range(tc):
  cid=r.get('i');strip=r.get('?');script=r.get('h')
  if cid==114:r.p+=16
  r.p+=16
  if tree:
   nodes=r.get('i');ss=r.get('i');r.p+=nodes*24+ss
  if tree and ver>=21:
   deps=r.get('i');r.p+=deps*4
  types.append(cid)
 count=r.get('i');objects=[]
 for i in range(count):
  r.align();pid=r.get('q');entry=r.p;o=r.get('I');n=r.get('I');t=r.get('i')
  objects.append({'pid':pid,'entry':entry,'offset':data+o,'size':n,'type':types[t]})
 return b,objects,{'version':ver,'unity':unity,'tree':tree,'data':data,'meta':meta}
if __name__=='__main__':
 root=(Path(__file__).resolve().parents[1]/'romfs/game/bin/Data')
 for name in ['2e5f135ec90b47e4e8ca141d7c6c0e9b','816c3a86d731de047add7d988c3a88d8','level1']:
  b,objs,head=asset(root/name);print(name,head,len(objs))
  for obj in objs:
   raw=b[obj['offset']:obj['offset']+obj['size']]
   if b'FREE GOLD' in raw or b'LanguageSource' in raw:
    print(obj)
    strings=[]
    for p in range(0,len(raw)-4,4):
     n=struct.unpack_from('<I',raw,p)[0]
     if 0<n<500 and p+4+n<=len(raw):
      try:s=raw[p+4:p+4+n].decode('utf-8')
      except UnicodeError:continue
      if all(c.isprintable() or c in '\r\n\t' for c in s):strings.append((p,s))
    Path('work/unity-'+name+'-'+str(obj['pid'])+'.json').write_text(json.dumps(strings,ensure_ascii=False,indent=2),encoding='utf-8')
