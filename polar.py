#!/usr/bin/env python3
"""
polar.py - convert an image into a polar .bin for the holospin POV display.

Usage:
    python polar.py logo.png                      (64 columns, 72 LEDs, crop auto)
    python polar.py logo.png -c 64 -l 72 --crop fit
    python polar.py logo.png --crop width --flip

Output: polar_<name>.bin next to the input file, laid out as
    0xAA 0x55, then [column][led][R,G,B]
which is exactly what the ESP32 receiver expects (ImageColums x LedAmount x 3).

Crop modes (the circle is always centered on the image):
    width   circle diameter = image width  (extra circle area is black)
    height  circle diameter = image height (left/right edges are cut off)
    auto    circle diameter = the smaller of width/height (largest circle that fits)
    fit     image corners touch the circle (whole image inside, extra area black)

Each polar cell (one column x one LED ring) is the average of every
cartesian pixel that falls inside that wedge. LED 0 is at the center,
angle 0 points right and increases counter-clockwise.
"""
import argparse
import math
import sys
from pathlib import Path

from PIL import Image

HEADER = b"\xAA\x55"
WORK_SIZE = 1024  # canvas is shrunk to at most this many px before sampling


def load_rgb(path):
    """Open an image as RGB, flattening any transparency onto black."""
    img = Image.open(path)
    if img.mode in ("RGBA", "LA", "PA") or "transparency" in img.info:
        rgba = img.convert("RGBA")
        bg = Image.new("RGB", rgba.size, (0, 0, 0))
        bg.paste(rgba, mask=rgba.getchannel("A"))
        return bg
    return img.convert("RGB")


def square_canvas(img, crop):
    """Square canvas whose inscribed circle is the polar disc."""
    w, h = img.size
    if crop == "width":
        d = w
    elif crop == "height":
        d = h
    elif crop == "auto":
        d = min(w, h)
    else:  # fit
        d = math.ceil(math.hypot(w, h))
    left = (w - d) // 2
    top = (h - d) // 2
    # crop() with a box that goes past the image fills the outside with black
    return img.crop((left, top, left + d, top + d))


def to_polar(canvas, cols, leds, flip, reverse, gamma):
    if canvas.width > WORK_SIZE:
        canvas = canvas.resize((WORK_SIZE, WORK_SIZE), Image.LANCZOS)
    size = canvas.width
    pix = canvas.load()
    R = size / 2
    two_pi = 2 * math.pi

    n = cols * leds
    sr = [0] * n
    sg = [0] * n
    sb = [0] * n
    cnt = [0] * n

    # Cartesian -> polar: drop every pixel into the cell it falls in
    for y in range(size):
        dy = R - (y + 0.5)
        for x in range(size):
            dx = (x + 0.5) - R
            rho = math.hypot(dx, dy)
            if rho >= R:
                continue
            led = min(int(rho / R * leds), leds - 1)
            if reverse:
                led = leds - 1 - led
            ang = math.atan2(dy, dx)
            if flip:
                ang = -ang
            col = int((ang % two_pi) / two_pi * cols) % cols
            k = col * leds + led
            r, g, b = pix[x, y]
            sr[k] += r
            sg[k] += g
            sb[k] += b
            cnt[k] += 1

    lut = [round(255 * (i / 255) ** gamma) for i in range(256)]
    out = bytearray(n * 3)

    for k in range(n):
        c = cnt[k]
        if c:
            r, g, b = sr[k] / c, sg[k] / c, sb[k] / c
        else:
            # tiny cell with no pixel inside: sample its center instead
            col, led = divmod(k, leds)
            if reverse:
                led = leds - 1 - led
            ang = (col + 0.5) / cols * two_pi
            if flip:
                ang = -ang
            rr = (led + 0.5) / leds * R
            x = min(max(int(R + rr * math.cos(ang)), 0), size - 1)
            y = min(max(int(R - rr * math.sin(ang)), 0), size - 1)
            r, g, b = pix[x, y]
        out[k * 3] = lut[int(round(r))]
        out[k * 3 + 1] = lut[int(round(g))]
        out[k * 3 + 2] = lut[int(round(b))]
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="Image -> polar .bin for the holospin display")
    ap.add_argument("file", help="input image (png, jpg, bmp, ...)")
    ap.add_argument("-c", "--cols", type=int, default=64, help="columns around the circle (ImageColums)")
    ap.add_argument("-l", "--leds", type=int, default=72, help="LEDs per strip (LedAmount)")
    ap.add_argument("--crop", choices=["width", "height", "auto", "fit"], default="auto")
    ap.add_argument("--gamma", type=float, default=2.2, help="LED gamma, 1.0 to disable")
    ap.add_argument("--flip", action="store_true", help="mirror the image")
    ap.add_argument("--reverse", action="store_true", help="LED 0 at the outer edge instead of the center")
    args = ap.parse_args()

    if args.cols < 1 or args.leds < 1:
        sys.exit("cols and leds must be at least 1")

    src = Path(args.file)
    img = load_rgb(src)
    canvas = square_canvas(img, args.crop)
    data = to_polar(canvas, args.cols, args.leds, args.flip, args.reverse, args.gamma)

    out = src.with_name(f"polar_{src.stem}.bin")
    out.write_bytes(HEADER + data)
    print(f"{src.name}: {img.width}x{img.height}, crop={args.crop}, circle={canvas.width}px")
    print(f"wrote {out} ({len(HEADER) + len(data)} bytes = 2 header + {args.cols}x{args.leds}x3)")


if __name__ == "__main__":
    main()
