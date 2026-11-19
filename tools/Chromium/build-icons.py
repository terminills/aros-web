#!/usr/bin/env python3
"""Generate native .info files with the matching AROS host ilbmtoicon tool."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ilbmtoicon', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parent / 'icons'
args.output.mkdir(parents=True, exist_ok=True)
for name in ['Chromium', 'Start Chromium', 'ChromiumDrawer']:
    subprocess.run([str(args.ilbmtoicon.resolve()),
                    str(source / (name + '.info.src')),
                    str(source / 'Chromium.png'),
                    str(args.output / (name + '.info'))], check=True)
