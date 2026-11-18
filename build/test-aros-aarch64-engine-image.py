#!/usr/bin/env python3
"""Host regression for the actual linker/packager, not a native runtime test.

Uses a 140 MiB branch gap, verifies instruction targets at three load bases,
and ensures non-PIC linker thunks are refused. Retains evidence in --work-dir.
"""
import argparse
import importlib.util
import json
import mmap
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assembler', required=True)
    parser.add_argument('--lld', required=True)
    parser.add_argument('--work-dir', required=True, type=Path)
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = work / 'fixture.S'
    if source.exists():
        raise ValueError('use a fresh evidence directory')
    source.write_text('''.section .text.start,"ax"
.global _start
_start:
 bl far_target
 adrp x0, pointer
 add x0,x0,:lo12:pointer
 ret
.weak missing_optional
 bl missing_optional
 movz x2, #:abs_g3:pointer
 movk x2, #:abs_g2_nc:pointer
 movk x2, #:abs_g1_nc:pointer
 movk x2, #:abs_g0_nc:pointer
.section .text.gap,"ax"
.space 140*1024*1024
.section .text.end,"ax"
.global far_target
far_target:
 ret
.section .data,"aw"
.global pointer
pointer:
 .quad far_target
''')
    (work / 'fixture.ld').write_text('ENTRY(_start)\nSECTIONS { . = 0; .text : { *(.text.start) *(.text.gap) *(.text.end) } .data : { *(.data) } .bss : { *(.bss) } }\n')
    subprocess.run([args.assembler, 'fixture.S', '-o', 'fixture.o'], cwd=work, check=True)
    helper = Path(__file__).with_name('aros-aarch64-engine-image.py')
    spec = importlib.util.spec_from_file_location('engine_image', helper)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for variant, flags in [('absolute', []), ('pic', ['--pic-veneer'])]:
        linked = work / (variant + '.elf')
        subprocess.run([args.lld, '--emit-relocs', *flags, '-T', 'fixture.ld',
                        'fixture.o', '-o', str(linked)], cwd=work, check=True)
        if variant == 'absolute':
            try:
                module.package(linked, work / 'bad.library')
            except ValueError as error:
                assert 'absolute linker thunks' in str(error), str(error)
            else:
                raise AssertionError('absolute thunk accepted')
        else:
            module.package(linked, work / 'pic.library')
    with (work / 'pic.library').open('rb') as file:
        data = mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ)
        offset = struct.unpack_from('<Q', data, 40)[0]
        sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * 64) for i in range(6)]
        image, rel = sections[1:3]
        relocations = [struct.unpack_from('<QQq', data, at)
                       for at in range(rel[4], rel[4] + rel[5], 24)]
        def instruction(at):
            return struct.unpack_from('<I', data, image[4] + at)[0]
        def signed(value, bits):
            return value - (1 << bits) if value & (1 << (bits - 1)) else value
        def page_target(insn, pc):
            immediate = ((insn >> 29) & 3) | (((insn >> 5) & 0x7ffff) << 2)
            return (pc & ~4095) + (signed(immediate, 21) << 12)
        thunk = signed(instruction(0) & 0x3ffffff, 26) * 4
        assert instruction(thunk + 8) == 0xd61f0200  # BR x16
        assert instruction(16) == 0x94000001  # absent weak call skips forward
        for base in (0x1000, 0x12345000, 0x1000000000):
            far = page_target(instruction(thunk), base + thunk) + ((instruction(thunk + 4) >> 10) & 4095)
            pointer = page_target(instruction(4), base + 4) + ((instruction(8) >> 10) & 4095)
            wide = 0
            for where, info, addend in relocations:
                kind = info & 0xffffffff
                if kind == 257:
                    assert where + base == pointer and addend + base == far
                else:
                    shift = {264: 0, 266: 16, 268: 32, 269: 48}[kind]
                    insn = (instruction(where) & 0xffe0001f) | ((((base + addend) >> shift) & 65535) << 5)
                    wide |= ((insn >> 5) & 65535) << shift
            assert wide == pointer
            assert instruction(far - base) == 0xd65f03c0
        assert len(relocations) == 5
    result = {'result': 'PASS', 'evidence': 'host instruction/relocation validation',
              'branch_gap_MiB': 140, 'load_bases': ['0x1000', '0x12345000', '0x1000000000'],
              'absolute_thunk_rejected': True, 'runtime_relocations': 5}
    (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
