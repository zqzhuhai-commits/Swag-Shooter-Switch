from read_unity_assets import asset,Reader
from pathlib import Path
import json
def string(r):
 start=r.p;n=r.get('I');assert n<100000
 value=r.b[r.p:r.p+n].decode();r.p+=n;r.align();return (start,r.p,value)
def parse(path):
 b,objs,head=asset(path);obj=next(o for o in objs if o['type']==114)
 raw=b[obj['offset']:obj['offset']+obj['size']];r=Reader(raw,56)
 count=r.get('I');terms=[]
 for i in range(count):
  term=string(r);typ=r.get('i');desc=string(r);n=r.get('I');assert n==15,(i,r.p,n)
  langs=[string(r) for _ in range(n)]
  nf=r.get('I');flags=raw[r.p:r.p+nf];r.p+=nf;r.align()
  nt=r.get('I');touch=[string(r) for _ in range(nt)]
  terms.append({'key':term[2],'type':typ,'english':langs[0][2],'chinese':langs[10][2],'langs':langs,'touch':touch})
 return b,objs,head,terms,r.p
if __name__=='__main__':
 p=(Path(__file__).resolve().parents[1]/'romfs/game/bin/Data/2e5f135ec90b47e4e8ca141d7c6c0e9b')
 b,objs,head,terms,end=parse(p)
 Path('work/localization-terms.json').write_text(json.dumps(terms,ensure_ascii=False,indent=2),encoding='utf-8')
 print('terms',len(terms),'end',end)
 print(json.dumps([(t['key'],t['english'],t['chinese']) for t in terms],ensure_ascii=True))
