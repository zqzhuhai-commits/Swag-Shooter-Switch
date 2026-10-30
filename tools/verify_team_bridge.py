"""Verify removal of the incorrect match-wide 7-bot cap.
The historical 4v4 bridge check remains in work/verify_team_bridge.py.
This does not certify 13-squad integration.
"""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
source=(root/'source/ai_profile.c').read_text(encoding='utf-8')
for old in ['team_spawn_limit','team_spawn_bridge','team_spawn_resume','0x15f9b4c','bots=7']:
 assert old not in source, old
nm=subprocess.check_output(['C:/devkitPro/devkitA64/bin/aarch64-none-elf-nm.exe',str(root/'swag-switch.elf')],text=True)
assert 'team_spawn_' not in nm
print('PASS: incorrect 7-bot cap removed from source and compiled ELF; 13-squad integration requires device testing')
