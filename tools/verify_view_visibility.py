"""Compile and execute production visibility restoration with managed-object mocks.
Does not simulate Unity rendering or establish hardware correctness.
"""
from pathlib import Path
import json,re,struct,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
prefix='C:/devkitPro/devkitA64/bin/aarch64-none-elf-'
source=(root/'source/ai_profile.c').read_text(encoding='utf-8')
start=source.index('typedef struct {uint32_t handle;unsigned char previous,component;} ViewVisibility;')
production=source[start:source.index('static void driving_visibility(',start)]
assert 'object_field(object_field(player,"carInteractionHandler"),"drivingCar")' not in source
assert source.index('if(driving) {',source.index('static void third_person_test_poll(')) < source.index('"get_cameraMode"',source.index('static void third_person_test_poll('))
with tempfile.TemporaryDirectory(prefix='swag-view-') as temporary:
    fixture=Path(temporary)/'view.c';elf=Path(temporary)/'view.elf'
    fixture.write_text((root/'tests/view_visibility_fixture.c').read_text(encoding='utf-8').replace('/* PRODUCTION_VISIBILITY */',production),encoding='utf-8')
    subprocess.run([prefix+'gcc.exe','-O0','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only','-nostdlib','-Wl,-e,run_visibility_tests',str(fixture),'-o',str(elf)],check=True)
    binary=elf.read_bytes();entry=struct.unpack_from('<Q',binary,24)[0]
    dis=subprocess.check_output([prefix+'objdump.exe','-d',str(elf)],text=True)
# Reuse the strict ARM64 fixture executor; unknown instructions fail closed.
runner=(root/'tools/verify_squad_rules.py').read_text(encoding='utf-8')
exec(runner[runner.index('instructions={}'):runner.index('report={')])
report={'version':'0.14.90','status':'PASS','compiled_arm64_instructions':steps,
'checks':['camera component rendering disabled without disabling weapon scripts','reapply after native re-enable','repeated hide preserves original visibility','replacement restores prior object','native dismount restoration preserved','expired weak handles ignored','crosshair gameobject visibility restored','driving checked before camera-mode override','drivingCar read as Boolean'],
'unity_rendering':'DEVICE_TEST_PENDING'}
(root/'audit/view-visibility-0.14.90.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
