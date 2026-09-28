#!/usr/bin/env python3
"""imgprobe.py - tiny PNG region probe for visual test asserts (PIL only).

Usage:
  imgprobe.py stats <png> <x0,y0,x1,y1>
      prints "MEAN_R MEAN_G MEAN_B" (0..255 floats) of the region
  imgprobe.py diff <a.png> <b.png> [x0,y0,x1,y1]
      prints "MEAN_ABS_DIFF MAX_ABS_DIFF FRAC_GT_8" (frac in 0..1)
  imgprobe.py frac_nonblue <png> <x0,y0,x1,y1>
      prints "FRAC" - fraction of pixels where R/B > 0.55 (cloud proxy)
  imgprobe.py rb_ratio <png> <x0,y0,x1,y1>
      prints "MEAN_R/B" - mean per-pixel R/B ratio (warmth proxy)
  imgprobe.py pick_cloudy <png> <x0,y0,x1,y1> <cols,rows>
      prints "X0,Y0,X1,Y1" of the grid cell with the highest frac_nonblue

Region is inclusive-exclusive. Exit 1 on bad input.
"""
import sys
from PIL import Image


def parse_region(s):
    parts = [int(p) for p in s.split(",")]
    if len(parts) != 4:
        raise ValueError("region must be x0,y0,x1,y1")
    return parts


def crop(img, region):
    x0, y0, x1, y1 = region
    if x0 < 0 or y0 < 0 or x1 > img.width or y1 > img.height or x0 >= x1 or y0 >= y1:
        raise ValueError("region %s outside %dx%d" % (region, img.width, img.height))
    return img.crop((x0, y0, x1, y1)).convert("RGB")


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 1
    cmd = argv[1]

    if cmd == "stats":
        img = Image.open(argv[2])
        reg = crop(img, parse_region(argv[3]))
        px = list(reg.getdata())
        n = len(px)
        mr = sum(p[0] for p in px) / n
        mg = sum(p[1] for p in px) / n
        mb = sum(p[2] for p in px) / n
        print("%.3f %.3f %.3f" % (mr, mg, mb))
        return 0

    if cmd == "diff":
        a = Image.open(argv[2])
        b = Image.open(argv[3])
        if a.size != b.size:
            sys.stderr.write("diff: size mismatch %s vs %s\n" % (a.size, b.size))
            return 1
        if len(argv) > 4:
            a = crop(a, parse_region(argv[4]))
            b = crop(b, parse_region(argv[4]))
        else:
            a = a.convert("RGB")
            b = b.convert("RGB")
        pa = a.getdata()
        pb = b.getdata()
        n = a.width * a.height
        total = 0
        mx = 0
        over = 0
        for ca, cb in zip(pa, pb):
            d0 = abs(ca[0] - cb[0])
            d1 = abs(ca[1] - cb[1])
            d2 = abs(ca[2] - cb[2])
            d = d0 + d1 + d2
            total += d
            m = d0
            if d1 > m:
                m = d1
            if d2 > m:
                m = d2
            if m > mx:
                mx = m
            if m > 8:
                over += 1
        print("%.4f %d %.6f" % (total / (3.0 * n), mx, over / float(n)))
        return 0

    if cmd == "frac_nonblue":
        img = Image.open(argv[2])
        reg = crop(img, parse_region(argv[3]))
        px = reg.getdata()
        n = reg.width * reg.height
        cnt = 0
        for p in px:
            if p[2] > 0 and (p[0] / float(p[2])) > 0.55:
                cnt += 1
        print("%.6f" % (cnt / float(n)))
        return 0

    if cmd == "rb_ratio":
        img = Image.open(argv[2])
        reg = crop(img, parse_region(argv[3]))
        px = reg.getdata()
        n = reg.width * reg.height
        total = 0.0
        for p in px:
            if p[2] > 0:
                total += p[0] / float(p[2])
        print("%.6f" % (total / n))
        return 0

    if cmd == "pick_cloudy":
        img = Image.open(argv[2])
        x0, y0, x1, y1 = parse_region(argv[3])
        cols, rows = [int(p) for p in argv[4].split(",")]
        best = None
        best_frac = -1.0
        cw = (x1 - x0) // cols
        ch = (y1 - y0) // rows
        for r in range(rows):
            for c in range(cols):
                reg = crop(img, (x0 + c * cw, y0 + r * ch,
                                 x0 + (c + 1) * cw, y0 + (r + 1) * ch))
                px = reg.getdata()
                n = reg.width * reg.height
                cnt = 0
                for p in px:
                    if p[2] > 0 and (p[0] / float(p[2])) > 0.55:
                        cnt += 1
                frac = cnt / float(n)
                if frac > best_frac:
                    best_frac = frac
                    best = (x0 + c * cw, y0 + r * ch,
                            x0 + (c + 1) * cw, y0 + (r + 1) * ch)
        print("%d,%d,%d,%d" % best)
        return 0

    sys.stderr.write("unknown command: %s\n" % cmd)
    sys.stderr.write(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
