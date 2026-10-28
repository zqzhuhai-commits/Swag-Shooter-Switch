"""Independent, standard-library-only ELF inventory. Does not execute game code."""
import argparse
import collections
import csv
import hashlib
import json
import re
from pathlib import Path
import struct


def category(name):
    groups = [
        ('thread-sync', ('pthread_', 'sem_', '__pthread', '__tls', '__get_tls')),
        ('graphics', ('egl', 'gl', 'ANativeWindow', 'AChoreographer')),
        ('audio', ('AAudio', 'slCreate', 'SL_IID')),
        ('android', ('__android', 'AAsset', 'ALooper', 'AInput', 'ASensor', 'AMotion', 'AMedia')),
        ('loader', ('dlopen', 'dlsym', 'dlclose', 'dlerror', 'dladdr', 'dl_iterate')),
        ('memory', ('mmap', 'munmap', 'mprotect', 'madvise', 'syscall', 'prctl')),
        ('network', ('socket', 'connect', 'getaddrinfo', 'recv', 'send', 'setsockopt', 'gethost')),
    ]
    for group, prefixes in groups:
        if name.startswith(prefixes):
            return group
    return 'libc-math-other'


def inspect(path):
    b = path.read_bytes()
    def unpack(fmt, offset):
        n = struct.calcsize('<' + fmt)
        if offset < 0 or offset + n > len(b):
            raise ValueError('ELF structure outside file')
        return struct.unpack_from('<' + fmt, b, offset)
    if b[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('not little-endian ELF64')
    h = unpack('HHIQQQIHHHHHH', 16)
    if h[0] != 3 or h[1] != 183:
        raise ValueError('not AArch64 ET_DYN')
    phoff, shoff, phentsize, phnum, shentsize, shnum = h[4], h[5], h[8], h[9], h[10], h[11]
    if phentsize != 56 or shentsize != 64:
        raise ValueError('unexpected header size')
    ph = [unpack('IIQQQQQQ', phoff + i * phentsize) for i in range(phnum)]
    sh = [unpack('IIQQQQIIQQ', shoff + i * shentsize) for i in range(shnum)]
    def string(table, offset):
        if not 0 <= offset < len(table):
            raise ValueError('invalid string offset')
        end = table.find(b'\0', offset)
        if end < 0:
            raise ValueError('unterminated string')
        return table[offset:end].decode('utf-8', errors='replace')
    def va_to_file(va):
        for p in ph:
            if p[0] == 1 and p[3] <= va < p[3] + p[5]:
                return p[2] + va - p[3]
        raise ValueError('VA not backed by file')
    dyn = {}
    needed_offsets = []
    for p in ph:
        if p[0] != 2:
            continue
        for offset in range(p[2], p[2] + p[5], 16):
            tag, value = unpack('qQ', offset)
            if not tag:
                break
            dyn[tag] = value
            if tag == 1:
                needed_offsets.append(value)
    dynstr_off = va_to_file(dyn[5])
    dynstr = b[dynstr_off:dynstr_off + dyn[10]]
    symbols = []
    for s in sh:
        if s[1] != 11:
            continue
        if s[9] != 24:
            raise ValueError('unexpected symbol entry size')
        for offset in range(s[4], s[4] + s[5], 24):
            name, info, other, index, value, size = unpack('IBBHQQ', offset)
            symbols.append(dict(name=string(dynstr, name), bind=info >> 4,
                                type=info & 15, visibility=other & 3,
                                section=index, value=value, size=size))
    relocations = collections.Counter()
    references = collections.Counter()
    for s in sh:
        if s[1] != 4:
            continue
        if s[9] != 24:
            raise ValueError('unexpected RELA entry size')
        for offset in range(s[4], s[4] + s[5], 24):
            target, info, addend = unpack('QQq', offset)
            kind, symbol = info & 0xffffffff, info >> 32
            relocations[kind] += 1
            if symbol >= len(symbols):
                raise ValueError('relocation symbol out of bounds')
            if kind in (257, 1025, 1026) and symbols[symbol]['section'] == 0:
                references[symbols[symbol]['name']] += 1
    patterns = {'svc_candidates': [], 'tpidr_el0_read_candidates': []}
    segments = []
    for p in ph:
        if p[0] != 1:
            continue
        segments.append(dict(va=p[3], filesz=p[5], memsz=p[6], flags=p[1]))
    # PT_LOAD executable ranges also contain rodata/unwind tables in these
    # Android binaries. Restrict pattern scans to SHF_EXECINSTR sections.
    for s in sh:
        if s[2] & 4 and s[1] == 1:
            for off in range(s[4], s[4] + s[5] - 3, 4):
                word = unpack('I', off)[0]
                va = s[3] + off - s[4]
                if word & 0xffe0001f == 0xd4000001:
                    patterns['svc_candidates'].append(hex(va))
                if word & 0xffffffe0 == 0xd53bd040:
                    patterns['tpidr_el0_read_candidates'].append(hex(va))
    imports = [dict(s, category=category(s['name']), relocations=references[s['name']])
               for s in symbols if s['name'] and s['section'] == 0]
    exports = {s['name'] for s in symbols if s['section'] != 0 and
               s['bind'] in (1, 2) and s['visibility'] in (0, 3)}
    result = dict(file=path.name, bytes=len(b), sha256=hashlib.sha256(b).hexdigest(),
                  needed=[string(dynstr, i) for i in needed_offsets],
                  image_bytes=(max(p['va'] + p['memsz'] for p in segments) + 4095) & ~4095,
                  symbols=len(symbols), segments=segments,
                  tls_segments=sum(p[0] == 7 for p in ph),
                  relocation_types=dict(sorted(relocations.items())),
                  imports=imports, import_categories=dict(collections.Counter(i['category'] for i in imports)),
                  instruction_patterns=patterns,
                  note='Instruction patterns scanned in SHF_EXECINSTR sections; reachability not verified.')
    return result, exports


def main():
    p = argparse.ArgumentParser()
    p.add_argument('game_dir', type=Path)
    p.add_argument('output_dir', type=Path)
    args = p.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    reports, exports = [], set()
    binding_source = (Path(__file__).resolve().parents[1] / 'source' / 'bionic_basic.c').read_text(encoding='utf-8')
    binding_array = binding_source.split('static const Binding bindings[] = {', 1)[1].split('};', 1)[0]
    bindings = set(re.findall(r'BIND\((\w+)\)', binding_array))
    bindings.update(re.findall(r'\{"([^"]+)",', binding_array))
    thread_source = (Path(__file__).resolve().parents[1] / 'source' / 'bionic_threads.c').read_text(encoding='utf-8')
    thread_bindings = set(re.findall(r'THREAD_BIND\("([^"]+)"', thread_source))
    service_source = '\n'.join((Path(__file__).resolve().parents[1] / 'source' / f).read_text(encoding='utf-8')
                               for f in ('bionic_io.c', 'android_loader.c', 'guest_memory.c', 'guest_text.c', 'guest_system.c', 'android_events.c', 'android_graphics.c'))
    service_bindings = set(re.findall(r'(?:IO_BIND|RT_BIND|MEM_BIND|TEXT_BIND|SYS_BIND|EVENT_BIND|GRAPHICS_BIND)\("([^"]+)"', service_source))
    for name in ('libunity.so', 'libil2cpp.so', 'libmain.so'):
        r, e = inspect(args.game_dir / name)
        reports.append(r)
        exports |= e
    rows = []
    for report in reports:
        for s in report['imports']:
            s['provided_by_game_libraries'] = s['name'] in exports
            s['provided_by_basic_abi'] = s['name'] in bindings
            s['provided_by_thread_abi'] = s['name'] in thread_bindings
            s['provided_by_tls_abi'] = s['name'] == '__stack_chk_fail'
            s['provided_by_runtime_services'] = s['name'] in service_bindings
            rows.append(dict(library=report['file'], symbol=s['name'], category=s['category'],
                             weak=s['bind'] == 2, relocations=s['relocations'],
                             provided_by_game_libraries=s['provided_by_game_libraries'],
                             provided_by_basic_abi=s['provided_by_basic_abi'],
                             provided_by_thread_abi=s['provided_by_thread_abi'],
                             provided_by_tls_abi=s['provided_by_tls_abi'],
                             provided_by_runtime_services=s['provided_by_runtime_services']))
    (args.output_dir / 'elf-audit.json').write_text(json.dumps(reports, indent=2, ensure_ascii=False), encoding='utf-8')
    with (args.output_dir / 'imports.csv').open('w', newline='', encoding='utf-8-sig') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    for r in reports:
        missing = [s for s in r['imports'] if s['bind'] != 2 and
                   not s['provided_by_game_libraries'] and not s['provided_by_basic_abi'] and
                   not s['provided_by_thread_abi'] and not s['provided_by_tls_abi'] and
                   not s['provided_by_runtime_services'] and s['relocations']]
        print('{}: image={} symbols={} imports={} external-required={} relocs={}'.format(
            r['file'], r['image_bytes'], r['symbols'], len(r['imports']), len(missing), r['relocation_types']))
        print('  instruction candidates: SVC={} TPIDR_EL0={}'.format(
            len(r['instruction_patterns']['svc_candidates']),
            len(r['instruction_patterns']['tpidr_el0_read_candidates'])))


if __name__ == '__main__':
    main()
