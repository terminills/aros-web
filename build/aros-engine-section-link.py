#!/usr/bin/env python3
"""Link CEF with full symbols, retain that ELF, then compact section names."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata-dir', type=Path, required=True)
    parser.add_argument('--strip-command', default='', help='normal SDK strip command, run before name packing')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    options = parser.parse_args()
    command = options.command
    if command and command[0] == '--':
        command = command[1:]
    if not command or '-o' not in command:
        parser.error('expected compiler link command with -o')
    index = command.index('-o') + 1
    destination = Path(command[index]).resolve()
    metadata = options.metadata_dir.resolve()
    metadata.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location(
        'section_names', Path(__file__).with_name('aros-engine-section-names.py'))
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    # Preserve the old working output until linking and packaging both succeed.
    with tempfile.TemporaryDirectory(prefix='section-link-', dir=metadata) as directory:
        root = Path(directory)
        original, packed = root / 'full.library', root / 'packed.library'
        command[index] = str(original)
        subprocess.run(command, check=True)
        packing_input = original
        if options.strip_command:
            packing_input = root / 'stripped.library'
            shutil.copy2(original, packing_input)
            subprocess.run(shlex.split(options.strip_command) + [str(packing_input)], check=True)
        result = packer.compact(packing_input, packed)
        retained = metadata / (destination.name + '.full-symbols')
        if packing_input != original:
            stripped = metadata / (destination.name + '.before-section-names')
            os.replace(packing_input, stripped)
            result['source'] = str(stripped)
        else:
            result['source'] = str(retained)
        result['full_symbols'] = str(retained)
        result['destination'] = str(destination)
        os.replace(original, retained)
        # Build metadata and output normally share a filesystem. Explicitly
        # refuse a cross-device move rather than partially replacing a library.
        os.replace(packed, destination)
        (metadata / (destination.name + '.section-names.json')).write_text(
            json.dumps(result, indent=2) + '\n')
        print('CEF section names: %d -> %d bytes; original ELF: %s' %
              (result['old_name_bytes'], result['new_name_bytes'], retained))


if __name__ == '__main__':
    main()
