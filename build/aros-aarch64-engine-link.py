#!/usr/bin/env python3
"""Opt-in CEF link wrapper; leave the SDK and other modules unchanged.

collect-aros supplies the native symbol-set/library-requirement script. LLVM
then final-links the original inputs with PIC veneers. The image packager
returns the native ET_REL module, retaining only required runtime fixups.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def normalized_script(text):
    text = re.sub(r'(?m)^(\s*\.[\w.]+)\s+0\s*:', r'\1 :', text)
    text = text.replace('SECTIONS\n{', 'SECTIONS\n{\n  . = 0;', 1)
    # GCC uses zero-padded priorities; collect-aros may spell the same
    # priority without zeros. Collect every spelling in numeric order.
    matches = list(re.finditer(r'KEEP\(\*\(\.init_array(?:\.\d+)?\)\)', text))
    for index, match in reversed(list(enumerate(matches))):
        replacement = ('KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))\n'
                       '    KEEP(*(.init_array))') if index == 0 else ''
        text = text[:match.start()] + replacement + text[match.end():]
    return text.replace('*(.gcc_except_table)', '*(.gcc_except_table .gcc_except_table.*)')


def final_arguments(original, lld, script, output, map_file):
    result = [str(lld)]
    index = 1
    while index < len(original):
        arg = original[index]
        if arg in ('-plugin', '-o', '-Map'):
            index += 2
            continue
        if arg == '-m':
            if original[index + 1] != 'aarch64elf_aros':
                raise ValueError('unexpected linker emulation')
            result += ['-m', 'aarch64elf']
            index += 2
            continue
        if arg == '-r' or arg.startswith(('-plugin-opt', '-maarch64elf_aros')):
            index += 1
            continue
        result.append(arg)
        index += 1
    return result + ['--emit-relocs', '--strip-debug', '--pic-veneer',
                     '--threads=8', '-e', '0', '-T', str(script), '-o', str(output),
                     '-Map=' + str(map_file)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lld', required=True, type=Path)
    parser.add_argument('--metadata-dir', required=True, type=Path)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    options = parser.parse_args()
    command = options.command
    if command and command[0] == '--':
        command = command[1:]
    if not command or '-o' not in command:
        parser.error('expected compiler link command with -o')
    output_index = command.index('-o') + 1
    destination = Path(command[output_index]).resolve()
    options.metadata_dir.mkdir(parents=True, exist_ok=True)
    # Keep the previously working module untouched until all stages pass.
    with tempfile.TemporaryDirectory(prefix='aros-arm-engine-') as temporary:
        temporary = Path(temporary)
        command[output_index] = str(temporary / 'ordinary.library')
        preview = subprocess.run(command + ['-###'], text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, check=True).stdout
        candidates = []
        for line in preview.splitlines():
            if 'collect-aros' not in line:
                continue
            parsed = shlex.split(line)
            if parsed and Path(parsed[0]).name == 'collect-aros':
                candidates.append(parsed)
        if len(candidates) != 1:
            raise ValueError('could not identify the compiler-selected AROS linker')
        environment = dict(os.environ, COLLECT_AROS_DEBUG='1')
        log_path = options.metadata_dir / 'collect.log'
        with log_path.open('w') as log:
            subprocess.run(command, env=environment, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        log = log_path.read_text()
        match = re.search(r'keeping intermediates: tempoutput=(.*?) ldscript=(.*?) ldresponse=(.*?)\n', log)
        if match is None:
            raise ValueError('collect-aros must support COLLECT_AROS_DEBUG retained intermediates')
        script = options.metadata_dir / 'native-final.ld'
        script.write_text(normalized_script(Path(match[2]).read_text()))
        linked = options.metadata_dir / 'native-final.elf'
        arguments = final_arguments(candidates[0], options.lld, script, linked,
                                    options.metadata_dir / 'native-final.map')
        (options.metadata_dir / 'native-final-arguments.json').write_text(json.dumps(arguments, indent=2) + '\n')
        with (options.metadata_dir / 'native-final.log').open('w') as log_stream:
            subprocess.run(arguments, stdout=log_stream, stderr=subprocess.STDOUT, check=True)
        helper = Path(__file__).with_name('aros-aarch64-engine-image.py')
        spec = importlib.util.spec_from_file_location('aros_engine_image', helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        result = module.package(linked, destination)
        (options.metadata_dir / 'image.json').write_text(json.dumps(result, indent=2) + '\n')
        print('[AROS ARM engine] ' + json.dumps(result, sort_keys=True))
        # These are the three private files created by this invocation of
        # collect-aros. The complete final ELF/map/log stay in metadata-dir.
        for name in match.groups():
            if name != '(none)':
                Path(name).unlink(missing_ok=True)


if __name__ == '__main__':
    main()
