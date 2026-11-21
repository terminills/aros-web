#!/usr/bin/env python3
"""Link and compact only the native x64 CEF engine; preserve its final ELF/map.

The ordinary AROS linker resolves library sets and runtime requirements first.
A final image at zero lets relative references survive one image relocation.
Keep unwind bytes unchanged: LLD must not deduplicate/rewrite .eh_frame.
"""
import argparse
import errno
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile

LAYOUT = '''SECTIONS {
 . = 0;
 .text : { *(.text .text.* .ltext .ltext.*) }
 .rodata ALIGN(4096) : { *(.rodata .rodata.* .lrodata .lrodata.*) }
 .aros_unwind ALIGN(8) : { *(.aros_unwind) }
 .gcc_except_table ALIGN(8) : { *(.gcc_except_table .gcc_except_table.*) }
 .data ALIGN(4096) : { *(.data .data.* .ldata .ldata.*) *(.malloc_hook .malloc_hook.* .flags_help_cold .protodesc_cold) }
 .bss ALIGN(4096) : { *(.bss .bss.* .lbss .lbss.*) *(COMMON) }
}
'''

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def publish_module(source, destination):
    """Publish atomically even when link scratch storage is on another volume."""
    try:
        os.replace(source, destination)
        return
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
    # The temporary copy must be beside the destination for atomic replacement.
    # Keep the old module and packed source intact if copying or validation fails.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=destination.name + '.',
                                         suffix='.tmp', dir=destination.parent,
                                         delete=False) as output:
            temporary = Path(output.name)
            with source.open('rb') as input_stream:
                shutil.copyfileobj(input_stream, output)
            output.flush()
            os.fsync(output.fileno())
        shutil.copymode(source, temporary)
        if sha(temporary) != sha(source):
            raise OSError('packed module copy failed SHA-256 verification')
        os.replace(temporary, destination)
        temporary = None
        source.unlink()
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lld', type=Path, required=True)
    parser.add_argument('--objcopy', type=Path, required=True)
    parser.add_argument('--metadata-dir', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or '-o' not in command:
        parser.error('expected compiler link command with -o')
    output_index = command.index('-o') + 1
    destination = Path(command[output_index]).resolve()
    metadata = args.metadata_dir.resolve()
    metadata.mkdir(parents=True, exist_ok=True)
    # Failed attempt directories retain inputs and logs for diagnosis.
    attempt = Path(tempfile.mkdtemp(prefix='x64-attempt-', dir=metadata))
    original = attempt / 'ordinary.library'
    command[output_index] = str(original)
    with (attempt / 'collect.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    renamed = attempt / 'unwind-input.elf'
    subprocess.run([str(args.objcopy), '--rename-section', '.eh_frame=.aros_unwind',
                    str(original), str(renamed)], check=True)
    script = attempt / 'native-final.ld'
    script.write_text(LAYOUT)
    linked = attempt / 'native-final.elf'
    arguments = [str(args.lld), '-m', 'elf_x86_64', '--emit-relocs',
                 '--strip-debug', '--no-relax', '--threads=8', '-e', '0',
                 '-T', str(script), str(renamed), '-o', str(linked),
                 '-Map=' + str(attempt / 'native-final.map')]
    with (attempt / 'native-final.log').open('w') as log:
        subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True)
    spec = importlib.util.spec_from_file_location('x64_image',
        Path(__file__).with_name('aros-x64-engine-image.py'))
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    packed = attempt / 'packed.library'
    result = packer.package(linked, packed)
    result.update(ordinary_input_sha256=sha(original), final_elf_sha256=sha(linked),
                  output_sha256=sha(packed), compiler_command=command,
                  final_arguments=arguments, destination=str(destination))
    (attempt / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    publish_module(packed, destination)
    for name in ['native-final.elf', 'native-final.map', 'native-final.ld',
                 'native-final.log', 'collect.log', 'result.json']:
        os.replace(attempt / name, metadata / name)
    original.unlink()
    renamed.unlink()
    attempt.rmdir()
    print('CEF x64 compact image: ' + json.dumps(result, sort_keys=True))

if __name__ == '__main__':
    main()
