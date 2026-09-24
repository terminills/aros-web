#!/usr/bin/env python3
"""Renders Workbench icon images for the Electron apps AROS ships.

Each app already ships its own mark as a large PNG.  The Workbench default
set (Gorilla) draws tools at 64x64 with a dark outline and a soft drop
shadow, so the mark is fitted into that idiom rather than pasted in raw:
downscaled with a box filter, given a 1px dark outline traced from its own
alpha, and dropped onto a blurred shadow.  That keeps VS Code and GitHub
Desktop looking like they belong beside Chromium, whose icon is rendered the
same way (chrome/app/theme/aros/make_workbench_icon.py).

    make-app-icons.py <out-dir>

writes VSCode.png, GitHubDesktop.png and BothApps.png (the two marks side by
side, for the one-engine-two-apps launcher).  `ilbmtoicon <name>.info.src
<name>.png <name>.info` turns each into the .info that stage-app-icons.sh
places in SYS:Applications/Development.
"""

import os
import sys

from PIL import Image, ImageChops, ImageFilter

SIZE = 64
SCALE = 8
OUTLINE = (0x10, 0x10, 0x10)
SHADOW_ALPHA = 0x60

HERE = os.path.dirname(os.path.abspath(__file__))
GITHUB = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))

SOURCES = {
    "VSCode": os.path.join(GITHUB, "vscode-aros", "resources", "linux",
                           "code.png"),
    "GitHubDesktop": os.path.join(GITHUB, "github-desktop-aros", "app",
                                  "static", "logos", "256x256.png"),
}


def load(path, px):
    """The app's own mark, RGBA, contained in a px*px box."""
    mark = Image.open(path).convert("RGBA")
    mark.thumbnail((px, px), Image.LANCZOS)
    out = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    out.paste(mark, ((px - mark.width) // 2, (px - mark.height) // 2))
    return out


def outlined(mark, width):
    """mark over a dark silhouette of itself, grown by `width`."""
    alpha = mark.getchannel("A")
    grown = alpha.filter(ImageFilter.MaxFilter(_odd(width)))
    body = Image.new("RGBA", mark.size, OUTLINE + (0,))
    body.putalpha(grown)
    return Image.alpha_composite(body, mark)


def shadowed(mark, offset, blur):
    alpha = mark.getchannel("A").point(lambda v: (v * SHADOW_ALPHA) // 255)
    alpha = alpha.filter(ImageFilter.GaussianBlur(blur))
    shadow = Image.new("RGBA", mark.size, (0, 0, 0, 0))
    shadow.putalpha(ImageChops.offset(alpha, offset, offset))
    return Image.alpha_composite(shadow, mark)


def _odd(n):
    n = max(3, int(round(n)))
    return n if n % 2 else n + 1


def render(path, size=SIZE, scale=SCALE):
    px = size * scale
    # Leave room for the outline and the shadow, the way the Gorilla tools do.
    mark = load(path, int(px * 0.86))
    canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    canvas.paste(mark, ((px - mark.width) // 2, (px - mark.height) // 2))
    canvas = outlined(canvas, 1.6 * scale)
    canvas = shadowed(canvas, int(1.1 * scale), 1.3 * scale)
    return canvas.resize((size, size), Image.BOX)


def render_pair(left_path, right_path, size=SIZE, scale=SCALE):
    """Both marks, overlapping, for the two-apps-one-engine launcher."""
    px = size * scale
    canvas = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    back = load(right_path, int(px * 0.60))
    front = load(left_path, int(px * 0.60))
    canvas.paste(back, (int(px * 0.34), int(px * 0.06)), back)
    canvas.paste(front, (int(px * 0.04), int(px * 0.30)), front)
    canvas = outlined(canvas, 1.6 * scale)
    canvas = shadowed(canvas, int(1.1 * scale), 1.3 * scale)
    return canvas.resize((size, size), Image.BOX)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    out_dir = argv[1]
    os.makedirs(out_dir, exist_ok=True)

    for name, src in SOURCES.items():
        if not os.path.exists(src):
            print("missing artwork: %s" % src, file=sys.stderr)
            return 2

    for name, src in SOURCES.items():
        out = os.path.join(out_dir, name + ".png")
        render(src).save(out)
        print(out)

    out = os.path.join(out_dir, "BothApps.png")
    render_pair(SOURCES["VSCode"], SOURCES["GitHubDesktop"]).save(out)
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
