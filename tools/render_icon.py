#!/usr/bin/env python3
"""Rasterize an SVG to a square PNG.

Usage: uv run --with cairosvg tools/render_icon.py <in.svg> <out.png> <size> [#bg]
"""
import sys

import cairosvg


def main() -> None:
    src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
    bg = sys.argv[4] if len(sys.argv) > 4 else None
    cairosvg.svg2png(url=src, write_to=dst, output_width=size, output_height=size,
                     background_color=bg)


if __name__ == "__main__":
    main()
