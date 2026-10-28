"""Use the ARM assembler as an independent oracle for emitted trampoline words.
No Switch runtime tests are claimed by this host-side check.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import subprocess


def branch(delta):
    if delta % 4 or not -134217728 <= delta <= 134217724:
        raise ValueError('branch out of range')
    return 0x14000000 | ((delta // 4) & 0x03ffffff)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, default=Path('C:/devkitPro'))
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    scratch = (project / 'build' / 'encoding-tests').resolve()
    assert project in scratch.parents
    scratch.mkdir(parents=True, exist_ok=True)
    assembly = ['.text', '.align 2']
    expected = []
    for offset in (0x180, 0x1f8):
        for reg in range(31):
            assembly.extend(['mrs x{},tpidrro_el0'.format(reg),
                             'ldr x{},[x{},#{}]'.format(reg, reg, offset)])
            expected.extend([0xd53bd060 | reg,
                             0xf9400000 | ((offset // 8) << 10) | (reg << 5) | reg])
    for delta in (4, -4, 134217724, -134217728):
        assembly.append('b .{:+d}'.format(delta))
        expected.append(branch(delta))
    assembly.append('nop')
    expected.append(0xd503201f)
    asm, obj, binary = (scratch / ('golden.' + ext) for ext in ('S', 'o', 'bin'))
    asm.write_text('\n'.join(assembly) + '\n', encoding='ascii')
    tools = args.sdk / 'devkitA64' / 'bin'
    subprocess.run([str(tools / 'aarch64-none-elf-gcc.exe'), '-c', str(asm), '-o', str(obj)], check=True)
    subprocess.run([str(tools / 'aarch64-none-elf-objcopy.exe'), '-O', 'binary', '-j', '.text',
                    str(obj), str(binary)], check=True)
    actual = list(struct.unpack('<{}I'.format(len(expected)), binary.read_bytes()))
    assert actual == expected, 'trampoline encoding differs from ARM assembler'
    for bad in (2, 134217728, -134217732):
        try:
            branch(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('bad branch accepted')
    spec = importlib.util.spec_from_file_location('elf_audit', project / 'tools' / 'audit_elf.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    total_sites = 0
    for name in ('libunity.so', 'libil2cpp.so', 'libmain.so', '../tests/tls-fixture.so'):
        path = project / 'romfs' / 'game' / name
        report, _ = audit.inspect(path)
        sites = sorted(int(x, 16) for x in report['instruction_patterns']['tpidr_el0_read_candidates'])
        assert len(sites) == len(set(sites)), 'duplicate TLS site'
        image = report['image_bytes']
        for i, site in enumerate(sites):
            trampoline = image + i * 12
            branch(trampoline - site)
            branch(site + 4 - (trampoline + 8))
        print('{}: {} TLS sites; all branch distances fit'.format(path.name, len(sites)))
        total_sites += len(sites)
    print('PASS: {} assembler words and {} TLS site branch ranges; runtime execution NOT tested'.format(
        len(expected), total_sites))


if __name__ == '__main__':
    main()
