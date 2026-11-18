#!/usr/bin/env python3
"""Verify packing preserves runtime contents and AROS memory suffixes."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('packer', Path(__file__).with_name('aros-engine-section-names.py'))
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)


def sections(data):
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count, names = struct.unpack_from('<HHH', data, 58)
    first = packer.SECTION.unpack_from(data, offset)
    count = count or first[5]
    names = first[6] if names == 0xffff else names
    return offset, names, [packer.SECTION.unpack_from(data, offset + i * size) for i in range(count)]


class PackingTest(unittest.TestCase):
    def test_names(self):
        self.assertEqual(packer.compact_name(b'.text.long_function.MEMF_CHIP'), b'.text.MEMF_CHIP')
        self.assertEqual(packer.compact_name(b'.rela.text.long_function'), b'.rela.text')
        self.assertEqual(packer.compact_name(b'.rela.ltext.long_rust_function'), b'.rela.ltext')
        self.assertEqual(packer.compact_name(b'.aros.set.INIT'), b'.aros.set.INIT')
        self.assertEqual(packer.compact_name(b'.init_array.00101'), b'.init_array.00101')

    def test_relocations_symbols_and_extended_indices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample.c'
            original = root / 'original.o'
            source.write_text('extern int external_value; int function_one(void) { return external_value; }\n'
                              'int function_two(void) { return function_one() + 2; }\n')
            subprocess.run(['cc', '-c', '-ffunction-sections', '-fdata-sections', str(source), '-o', str(original)], check=True)
            for extended in (False, True):
                before = bytearray(original.read_bytes())
                offset, names, headers = sections(before)
                if extended:
                    struct.pack_into('<H', before, 60, 0)
                    struct.pack_into('<H', before, 62, 0xffff)
                    struct.pack_into('<Q', before, offset + 32, len(headers))
                    struct.pack_into('<I', before, offset + 40, names)
                incoming = root / f'input-{extended}.o'
                outgoing = root / f'output-{extended}.o'
                incoming.write_bytes(before)
                report = packer.compact(incoming, outgoing)
                after = outgoing.read_bytes()
                self.assertGreater(report['name_bytes_saved'], 0)
                self.assertEqual(len(before), len(after))
                _, _, expected = sections(before)
                _, _, actual = sections(after)
                for i, (old, new) in enumerate(zip(expected, actual)):
                    self.assertEqual(old[1:5], new[1:5])
                    self.assertEqual(old[6:], new[6:])
                    if i != names:
                        self.assertEqual(old[5], new[5])
                        if old[1] not in (0, 8):
                            self.assertEqual(before[old[4]:old[4]+old[5]], after[new[4]:new[4]+new[5]])
                # A normal ELF reader must still accept the resulting symbols/relocs.
                subprocess.run(['readelf', '-r', '-s', str(outgoing)], stdout=subprocess.DEVNULL, check=True)
                with self.assertRaises(ValueError):
                    packer.compact(incoming, outgoing)
            bad = root / 'bad.o'
            bad.write_bytes(b'not ELF')
            with self.assertRaises(ValueError):
                packer.compact(bad, root / 'bad-output.o')
            shared_names = bytearray(original.read_bytes())
            offset, names, headers = sections(shared_names)
            sym_index = next(i for i, header in enumerate(headers) if header[1] == 2)
            struct.pack_into('<I', shared_names, offset + sym_index * packer.SECTION.size + 40, names)
            bad.write_bytes(shared_names)
            with self.assertRaises(ValueError):
                packer.compact(bad, root / 'shared-symbol-names.o')
            wrapper = Path(__file__).with_name('aros-engine-section-link.py')
            output = root / 'linked.library'
            invocation = [sys.executable, str(wrapper), '--metadata-dir', str(root / 'metadata'),
                          '--strip-command', 'strip --strip-unneeded', '--']
            subprocess.run(invocation + ['cc', '-r', str(original), '-o', str(output)], check=True)
            self.assertTrue((root / 'metadata/linked.library.full-symbols').exists())
            self.assertTrue((root / 'metadata/linked.library.before-section-names').exists())
            working = output.read_bytes()
            failed = subprocess.run(invocation + ['false', '-o', str(output)], stderr=subprocess.PIPE)
            self.assertNotEqual(failed.returncode, 0)
            self.assertEqual(output.read_bytes(), working)
            bad_strip = invocation.copy()
            bad_strip[bad_strip.index('--strip-command') + 1] = 'false'
            failed = subprocess.run(bad_strip + ['cc', '-r', str(original), '-o', str(output)],
                                    stderr=subprocess.PIPE)
            self.assertNotEqual(failed.returncode, 0)
            self.assertEqual(output.read_bytes(), working)


if __name__ == '__main__':
    unittest.main()
