#!/usr/bin/env python3
# Copyright (C) 2026, The AROS Development Team. All rights reserved.
#
# Blank a rectangle of a guest screenshot that a host window (a Teams
# notification, a menu) was overlapping when xwd read the AROS window, so
# no host-desktop content is kept in run evidence.
#
#   mask-host-overlay.py <png> <x0> <y0> <x1> <y1>
import sys
from PIL import Image, ImageDraw

path, x0, y0, x1, y1 = sys.argv[1], *map(int, sys.argv[2:6])
im = Image.open(path).convert("RGB")
d = ImageDraw.Draw(im)
d.rectangle((x0, y0, x1, y1), fill=(40, 40, 40))
d.text((x0 + 4, y0 + 4), "host overlay masked", fill=(200, 200, 200))
im.save(path)
