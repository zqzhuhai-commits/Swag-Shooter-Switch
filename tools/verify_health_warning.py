"""Check the production shader's gradient formula; not an OpenGL driver test."""
from pathlib import Path
import json,re,math
r=Path(__file__).resolve().parents[1]
s=(r/'source/health_warning.c').read_text()
start,end=map(float,re.search(r'smoothstep\(vec2\(([\d.]+)\),vec2\(([\d.]+)\),abs\(position\)\)',s).groups())
assert 0<start<end==1
assert 'float mask=1.0-(1.0-edge.x)*(1.0-edge.y)' in s
def smooth(p):
 t=max(0,min(1,(abs(p)-start)/(end-start)))
 return t*t*(3-2*t)
def opacity(x,y):return .45*(1-(1-smooth(x))*(1-smooth(y)))
assert opacity(0,0)==0 and opacity(start,start)==0
assert abs(opacity(1,0)-.45)<1e-9 and abs(opacity(1,1)-.45)<1e-9
previous=opacity(0,0)
for i in range(1,1001):
 value=opacity(i/1000,0)
 assert previous<=value<=.45 and value-previous<.002
 previous=value
assert opacity(start+.0001,0)<1e-6
base,amplitude,angle=map(float,re.search(r'float pulse=([\d.]+)f-([\d.]+)f\*cosf\(cycle\*([\d.]+)f\)',s).groups())
assert 'now%freq' in s and abs(angle-2*math.pi)<1e-9
for i in range(1001):
 t=i/1000;p=base-amplitude*math.cos(t*angle)
 assert .59999<=p<=1.00001
 assert abs(p-(base-amplitude*math.cos((t+1)*angle)))<1e-9
assert start==.35
poll=(r/'source/ai_profile.c').read_text(encoding='utf-8')
for marker in ['playing!=1','value<=0','max<=0','_isAlive','_isLocalPlayer','_health','maxHealth']:
 assert marker in poll
report={'status':'PASS','gradient_expression_model_samples':1001,'center_alpha':0,
        'max_alpha':.45,'gradient_start_normalized':start,'pulse_hz':1,'pulse_brightness_range':[.6,1],
        'no_step_at_gradient_start':True,'actual_gl_shader_and_device_visual':'PENDING',
        'health_threshold_and_fade_compiled_test':'see squad-rules-0.14.58.json'}
(r/'audit/health-warning-0.14.58.json').write_text(json.dumps(report,indent=2))
print(report)
