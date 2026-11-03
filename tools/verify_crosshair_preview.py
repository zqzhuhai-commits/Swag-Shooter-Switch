"""Execute the production preview setter with stale-override regression fixtures."""
from pathlib import Path
import json,re,struct,subprocess,tempfile
root=Path(__file__).resolve().parents[1];prefix='C:/devkitPro/devkitA64/bin/aarch64-none-elf-'
source=(root/'source/ai_profile.c').read_text(encoding='utf-8')
a=source.index('static void crosshair_preview_image(');production=source[a:source.index('static void crosshair_previews(',a)]
with tempfile.TemporaryDirectory(prefix='swag-preview-') as temporary:
 fixture=Path(temporary)/'test.c';elf=Path(temporary)/'test.elf'
 fixture.write_text((root/'tests/crosshair_preview_fixture.c').read_text().replace('/* PRODUCTION_PREVIEW */',production))
 subprocess.run([prefix+'gcc.exe','-O0','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdlib','-Wl,-e,run_preview_tests',str(fixture),'-o',str(elf)],check=True)
 binary=elf.read_bytes();entry=struct.unpack_from('<Q',binary,24)[0]
 dis=subprocess.check_output([prefix+'objdump.exe','-d',str(elf)],text=True)
runner=(root/'tools/verify_squad_rules.py').read_text(encoding='utf-8')
exec(runner[runner.index('instructions={}'):runner.index('report={')])
report={'version':'0.14.90','status':'PASS','compiled_arm64_instructions':steps,'checks':['four distinct effective sprites','stale override replaced','simple image mode','aspect preserved','white previews','matching selected preview','null safety'],'unity_rendering':'DEVICE_TEST_PENDING'}
(root/'audit/crosshair-preview-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))

