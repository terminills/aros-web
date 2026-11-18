#!/usr/bin/env python3
"""Package a statically final-linked AArch64 engine as one AROS ELF hunk.

Input must be linked at zero with --emit-relocs and LLD --pic-veneer.
Final linking creates branch
veneers; one page-aligned hunk preserves their layout when DOS relocates it.
Only absolute image pointers need runtime relocation. This is deliberately an
engine packaging step, not a replacement for the SDK linker or DOS loader.
"""
import argparse
import collections
import json
import mmap
from pathlib import Path
import struct

SH = struct.Struct('<IIQQQQIIQQ')
SYM = struct.Struct('<IBBHQQ')
RELA = struct.Struct('<QQq')
# Static address/branch expressions unchanged by a page-aligned image slide.
RELATIVE = {260, 261, 262, 273, 274, 275, 276, 277, 278,
            279, 280, 282, 283, 284, 285, 286, 299}
ABSOLUTE = {257: 8, 258: 4, **{kind: 4 for kind in range(263, 270)}}


def package(source, destination):
    with source.open('rb') as stream:
        data = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
        if data[:6] != b'\x7fELF\x02\x01':
            raise ValueError('expected little-endian ELF64')
        typ, machine = struct.unpack_from('<HH', data, 16)
        if (typ, machine) != (2, 183):
            raise ValueError('expected final-linked AArch64 ET_EXEC')
        offset = struct.unpack_from('<Q', data, 40)[0]
        size, count, names_index = struct.unpack_from('<HHH', data, 58)
        if size != SH.size:
            raise ValueError('unexpected section-header size')
        first = SH.unpack_from(data, offset)
        count = count or first[5]
        names_index = first[6] if names_index == 65535 else names_index
        sections = [SH.unpack_from(data, offset + i * size) for i in range(count)]
        names = sections[names_index]
        for symbol_table in sections:
            if symbol_table[1] == 2:
                strings = sections[symbol_table[6]]
                if data.find(b'\0__AArch64AbsLongThunk_', strings[4], strings[4] + strings[5]) != -1:
                    raise ValueError('absolute linker thunks require LLD --pic-veneer')

        def name(section):
            at = names[4] + section[0]
            return data[at:data.find(b'\0', at)].decode()

        allocated = [(i, s) for i, s in enumerate(sections) if s[2] & 2 and s[5]]
        if not allocated or min(s[3] for _, s in allocated) != 0:
            raise ValueError('image must start at address zero')
        if struct.unpack_from('<Q', data, 24)[0] != 0:
            raise ValueError('image entry must be zero')
        end = max(s[3] + s[5] for _, s in allocated)
        if end >= 1 << 32:
            raise ValueError('engine image exceeds four GiB')
        image = bytearray(end)
        previous_end = 0
        for _, section in sorted(allocated, key=lambda pair: pair[1][3]):
            label = name(section)
            if section[3] < previous_end:
                raise ValueError('overlapping allocated sections')
            if section[2] & 0x400 or section[1] not in (1, 8):
                raise ValueError('unsupported allocated section: ' + label)
            if label.startswith(('.got', '.plt', '.dynamic', '.dynsym')):
                raise ValueError('dynamic/GOT linkage requires a separate audit: ' + label)
            if section[1] != 8:
                image[section[3]:section[3] + section[5]] = data[section[4]:section[4] + section[5]]
            previous_end = section[3] + section[5]
        relocations = bytearray()
        counts = collections.Counter()
        seen = set()
        for section in sections:
            if section[1] != 4 or not sections[section[7]][2] & 2:
                continue
            target = sections[section[7]]
            table = sections[section[6]]
            extended = next((s for s in sections if s[1] == 18 and s[6] == section[6]), None)
            for at in range(section[4], section[4] + section[5], RELA.size):
                where, info, addend = RELA.unpack_from(data, at)
                kind, index = info & 0xffffffff, info >> 32
                if kind == 0:
                    continue
                symbol = SYM.unpack_from(data, table[4] + index * SYM.size)
                shindex = symbol[3]
                if shindex == 65535:
                    if extended is None:
                        raise ValueError('missing extended symbol indices')
                    shindex = struct.unpack_from('<I', data, extended[4] + index * 4)[0]
                weak_undefined = shindex == 0 and symbol[1] >> 4 == 2 and symbol[4] == 0
                if weak_undefined and kind in (282, 283):
                    # LLD resolves an absent optional weak call to the next
                    # instruction. Preserve only that verified encoding.
                    instruction = struct.unpack_from('<I', image, where)[0]
                    expected = 0x94000001 if kind == 283 else 0x14000001
                    if instruction != expected:
                        raise ValueError('unexpected undefined-weak branch encoding')
                    counts[kind] += 1
                    continue
                absolute = shindex == 65521 or weak_undefined
                if not absolute and (shindex == 0 or shindex >= count or not sections[shindex][2] & 2):
                    raise ValueError(f'relocation {kind} at {where:#x} references symbol index {index}, section {shindex}, value {symbol[4]:#x}')
                if where < target[3] or where >= target[3] + target[5]:
                    raise ValueError('relocation outside its target section')
                counts[kind] += 1
                if kind in RELATIVE and not absolute:
                    # Keep final-link instructions, including redirected veneer calls.
                    continue
                if kind in ABSOLUTE and absolute:
                    continue
                if kind not in ABSOLUTE:
                    raise ValueError(f'unsupported slide relocation {kind} at {where:#x}')
                if kind == 258:
                    raise ValueError('32-bit image pointer cannot support arbitrary AArch64 placement')
                if where in seen or where + ABSOLUTE[kind] > end:
                    raise ValueError('duplicate or out-of-bounds relocation')
                seen.add(where)
                value = symbol[4] + addend
                if not -(1 << 63) <= value < 1 << 63:
                    raise ValueError('relocation addend overflow')
                relocations += RELA.pack(where, (1 << 32) | kind, value)
        if not counts:
            raise ValueError('no retained relocation evidence; use --emit-relocs')
        # Symbol 1 denotes the start of the single loaded image.
        symbols = bytes(SYM.size) + SYM.pack(1, 3, 0, 1, 0, end)
        strings = b'\0engine_image\0'
        section_names = b'\0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0'
        definitions = [
            ('.text', 1, 7, image, 0, 0, 4096, 0),
            ('.rela.text', 4, 0x40, relocations, 3, 1, 8, RELA.size),
            ('.symtab', 2, 0, symbols, 4, 2, 8, SYM.size),
            ('.strtab', 3, 0, strings, 0, 0, 1, 0),
            ('.shstrtab', 3, 0, section_names, 0, 0, 1, 0),
        ]
        header = bytearray(data[:64])
        struct.pack_into('<H', header, 16, 1)
        struct.pack_into('<QQ', header, 24, 0, 0)
        struct.pack_into('<HHHHHH', header, 52, 64, 0, 0, SH.size, 6, 5)
        temporary = destination.with_name(destination.name + '.tmp')
        with temporary.open('wb') as out:
            out.write(header)
            headers = [bytes(SH.size)]
            for label, typ, flags, content, link, info, alignment, entsize in definitions:
                padding = (-out.tell()) % alignment
                out.write(bytes(padding))
                pos = out.tell()
                out.write(content)
                headers.append(SH.pack(section_names.index(label.encode() + b'\0'), typ,
                                       flags, 0, pos, len(content), link, info, alignment, entsize))
            out.write(bytes((-out.tell()) % 8))
            section_offset = out.tell()
            for h in headers:
                out.write(h)
            out.seek(40)
            out.write(struct.pack('<Q', section_offset))
        temporary.chmod(source.stat().st_mode & 0o777)
        temporary.replace(destination)
        return {'image_bytes': end, 'runtime_relocations': len(relocations) // RELA.size,
                'input_relocation_counts': dict(sorted(counts.items())),
                'output_bytes': destination.stat().st_size}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.source, args.destination), indent=2))
