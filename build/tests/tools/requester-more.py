#!/usr/bin/env python3
"""Locate the "More..." gadget of an AROS failure requester.

Usage: requester-more.py screen.png

Prints "X Y" (window coordinates of the gadget centre) when a "Software
Failure!" or "Recoverable Alert!" requester is visible, or nothing. The
requester is recognised structurally rather than by text: a gadget is a
bevelled box of the background grey (0x99) -- a light (0xEE) line along its
top and left, a black line along its bottom and right, a text line tall --
and "More..." is the leftmost gadget in the requester's gadget row. The
screen background is the same grey, so nothing is inferred from the message
box; the bevel pair alone identifies a gadget.
"""
import sys

from PIL import Image

GREY = (153, 153, 153)
LIGHT = (238, 238, 238)
BLACK = (0, 0, 0)
MIN_GADGET_WIDTH = 30
MAX_GADGET_WIDTH = 200
MIN_GADGET_HEIGHT = 12
MAX_GADGET_HEIGHT = 40


def near(px, ref, tol=6):
    return all(abs(px[i] - ref[i]) <= tol for i in range(3))


def runs(px, w, y, colour):
    """Horizontal runs of `colour` on row y as (start, end) pairs."""
    out, start = [], None
    for x in range(w + 1):
        hit = x < w and near(px[x, y], colour)
        if hit and start is None:
            start = x
        elif not hit and start is not None:
            if MIN_GADGET_WIDTH <= x - start <= MAX_GADGET_WIDTH:
                out.append((start, x))
            start = None
    return out


def is_gadget(px, x0, x1, y_top, y_bottom):
    """True when (x0..x1, y_top..y_bottom) is bevelled like a gadget."""
    # Left edge light, right edge black, interior mostly grey.  The full
    # (post-More...) requester dithers its background, so the top bevel's
    # light run can start a pixel or two early on a dither pixel; the left
    # edge is whichever of the first columns is light all the way down.
    for x0 in range(x0, x0 + 3):
        if all(near(px[x0, yy], LIGHT) for yy in range(y_top + 1, y_bottom)):
            break
    else:
        return False
    for yy in range(y_top + 1, y_bottom):
        if not near(px[x1, yy], BLACK):
            return False
    grey = sum(1 for yy in range(y_top + 1, y_bottom)
               for x in range(x0 + 1, x1, 2) if near(px[x, yy], GREY))
    total = (y_bottom - y_top - 1) * ((x1 - x0) // 2)
    return grey >= total * 0.7


def same_span(a, b, slack=2):
    """The bottom bevel is drawn a pixel in from the top one; allow that."""
    return abs(a[0] - b[0]) <= slack and abs(a[1] - b[1]) <= slack


def main(path):
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    for y in range(h - MIN_GADGET_HEIGHT):
        for top in runs(px, w, y, LIGHT):
            for yb in range(y + MIN_GADGET_HEIGHT, min(h, y + MAX_GADGET_HEIGHT + 1)):
                for bottom in runs(px, w, yb, BLACK):
                    if same_span(top, bottom) and is_gadget(px, top[0], bottom[1] - 1, y, yb):
                        print(f'{(top[0] + bottom[1]) // 2} {(y + yb) // 2}')
                        return


if __name__ == '__main__':
    main(sys.argv[1])
