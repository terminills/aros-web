#!/usr/bin/env python3
"""Compact post-link ELF section names without stripping function symbols.

AROS debug.library retains .shstrtab. Per-function section suffixes duplicate
symbol names and can cost tens of MiB in large engines. This opt-in packaging
step changes only sh_name, the section-name table, and that table's sh_size.
Section indices, offsets, sizes of all other sections, symbols, and relocations
are unchanged. Keep the original ELF for external debugger section matching.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import shutil
import stat
import struct

SECTION = struct.Struct('<IIQQQQIIQQ')
PREFIXES = (b'.text.', b'.rodata.', b'.data.', b'.bss.',
            b'.rela.text.', b'.rela.rodata.', b'.rela.data.', b'.rela.bss.',
            b'.ltext.', b'.lrodata.', b'.ldata.', b'.lbss.',
            b'.rela.ltext.', b'.rela.lrodata.', b'.rela.ldata.', b'.rela.lbss.',
            b'.gcc_except_table.', b'.rela.gcc_except_table.')
MEMORY_SUFFIXES = (b'.MEMF_CHIP', b'.MEMF_LOCAL', b'.MEMF_KICK',
                   b'.MEMF_FAST', b'.MEMF_PUBLIC')


def compact_name(name):
    for prefix in PREFIXES:
        if name.startswith(prefix):
            suffix = next((s for s in MEMORY_SUFFIXES if name.endswith(s)), b'')
            return prefix[:-1] + suffix
    return name


def compact(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve() or destination.exists():
        raise ValueError('destination must be a new file, distinct from source')
    with source.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if data[:7] != b'\x7fELF\x02\x01\x01':
            raise ValueError('expected ELF64 little endian')
        if struct.unpack_from('<H', data, 16)[0] != 1:
            raise ValueError('expected final native ET_REL module')
        offset = struct.unpack_from('<Q', data, 40)[0]
        entry_size, count, names_index = struct.unpack_from('<HHH', data, 58)
        if entry_size != SECTION.size or offset + entry_size > len(data):
            raise ValueError('invalid section header table')
        first = SECTION.unpack_from(data, offset)
        count = count or first[5]
        names_index = first[6] if names_index == 0xffff else names_index
        if not 0 < names_index < count or offset + count * entry_size > len(data):
            raise ValueError('invalid extended section indices')
        sections = [SECTION.unpack_from(data, offset + i * entry_size) for i in range(count)]
        names = sections[names_index]
        start, size = names[4:6]
        if names[1] != 3 or names[2] & 2 or start + size > len(data):
            raise ValueError('invalid nonallocated section-name string table')
        if start < offset + count * entry_size and offset < start + size:
            raise ValueError('section names overlap section headers')
        for i, section in enumerate(sections):
            if section[1] in (2, 11) and section[6] == names_index:
                raise ValueError('section names also back symbol names; cannot rewrite')
            if i != names_index and section[1] not in (0, 8) and section[5]:
                if start < section[4] + section[5] and section[4] < start + size:
                    raise ValueError('section names overlap another section')
        table = bytearray(b'\0')
        offsets = {b'': 0}
        new_names = []
        renamed = 0
        for section in sections:
            index = section[0]
            if index >= size:
                raise ValueError('section name out of range')
            end = data.find(b'\0', start + index, start + size)
            if end < 0:
                raise ValueError('unterminated section name')
            old = data[start + index:end]
            new = compact_name(old)
            renamed += new != old
            if new not in offsets:
                offsets[new] = len(table)
                table.extend(new + b'\0')
            new_names.append(offsets[new])
        if len(table) > size:
            # Small ELF files can already exploit suffix sharing better than
            # this deduplicated table. Leave those files byte-for-byte intact.
            table = data[start:start + size]
            new_names = [section[0] for section in sections]
            renamed = 0
        source_hash = hashlib.sha256(data).hexdigest()
    shutil.copy2(source, destination)
    mode = stat.S_IMODE(destination.stat().st_mode)
    destination.chmod(mode | stat.S_IWUSR)
    with destination.open('r+b') as stream, mmap.mmap(stream.fileno(), 0) as data:
        data[start:start + size] = table + bytes(size - len(table))
        for i, name in enumerate(new_names):
            struct.pack_into('<I', data, offset + i * entry_size, name)
        struct.pack_into('<Q', data, offset + names_index * entry_size + 32, len(table))
        data.flush()
        output_hash = hashlib.sha256(data).hexdigest()
    destination.chmod(mode)
    return {'source': str(source), 'destination': str(destination),
            'source_sha256': source_hash, 'destination_sha256': output_hash,
            'section_count': count, 'renamed_sections': renamed,
            'old_name_bytes': size, 'new_name_bytes': len(table),
            'name_bytes_saved': size - len(table),
            'file_size_unchanged': True,
            'changes': 'section name offsets/table and table size only; original ELF retained'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--manifest', type=Path)
    args = parser.parse_args()
    result = json.dumps(compact(args.source, args.destination), indent=2) + '\n'
    if args.manifest:
        args.manifest.write_text(result)
    print(result, end='')
