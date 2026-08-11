#!/usr/bin/env python3
"""Terrain + sky startup asset generator (stdlib only, deterministic).

Outputs (into --out):
  height.r16 / height.json / height_preview.png   16-bit heightmap + sidecar
  splat.png                          RGBA weights: R grass, G rock, B mud, A snow
  grass.png rock.png mud.png snow.png   tileable albedo, roughness in alpha
  cloud_noise.png                    tileable: R coverage, G detail, B billow
  moon.png                           albedo, alpha 255 (shader masks the disc)
"""

import argparse
import array
import hashlib
import json
import math
import os
import random
import shutil
import struct
import sys
import tempfile
import time
import zlib

ASSET_FILES = (
    "height.r16", "height.json", "height_preview.png", "splat.png",
    "grass.png", "rock.png", "mud.png", "snow.png",
    "cloud_noise.png", "moon.png",
)
TILEABLE_FILES = ("grass.png", "rock.png", "mud.png", "snow.png", "cloud_noise.png")
LAYER_NAMES = ("grass", "rock", "mud", "snow")


def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def clamp8(x):
    if x < 0.0:
        return 0
    if x > 255.0:
        return 255
    return int(x + 0.5)


def sstep(e0, e1, x):
    t = clamp01((x - e0) / (e1 - e0))
    return t * t * (3.0 - 2.0 * t)


# --------------------------------------------------------------------- png

def _chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_png(path, width, height, color_type, pixels):
    """color_type 6 = RGBA8, 0 = gray8. No interlace, filter 0 per row."""
    bpp = 4 if color_type == 6 else 1
    stride = width * bpp
    if len(pixels) != stride * height:
        raise ValueError("pixel buffer size mismatch for " + path)
    rows = []
    for r in range(height):
        rows.append(b"\x00")
        rows.append(bytes(pixels[r * stride:(r + 1) * stride]))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    data = (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(b"".join(rows), 6))
            + _chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(data)


def parse_png(path):
    """Minimal decoder for our own files: chunks, crc, filter bytes, zlib."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(path + ": bad signature")
    pos = 8
    width = height = ctype = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(tag + payload) & 0xFFFFFFFF != crc:
            raise ValueError("%s: crc mismatch in chunk %r" % (path, tag))
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, ctype, _c, _f, inter = struct.unpack(">IIBBBBB", payload)
            if depth != 8 or inter != 0:
                raise ValueError(path + ": unsupported IHDR")
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
    if width is None:
        raise ValueError(path + ": missing IHDR")
    raw = zlib.decompress(bytes(idat))
    bpp = 4 if ctype == 6 else 1
    stride = width * bpp
    if len(raw) != height * (stride + 1):
        raise ValueError(path + ": decompressed size mismatch")
    pixels = bytearray(height * stride)
    for r in range(height):
        if raw[r * (stride + 1)] != 0:
            raise ValueError(path + ": unexpected filter byte")
        pixels[r * stride:(r + 1) * stride] = raw[r * (stride + 1) + 1:(r + 1) * (stride + 1)]
    return width, height, ctype, bytes(pixels)


# --------------------------------------------------------------------- noise

class Perlin(object):
    """Classic 2D gradient noise, permutation table seeded by the caller RNG."""

    def __init__(self, rng):
        p = list(range(256))
        rng.shuffle(p)
        self.perm = p + p
        gx = (1.0, -1.0, 0.0, 0.0, 1.0, -1.0, 1.0, -1.0)
        gy = (0.0, 0.0, 1.0, -1.0, 1.0, 1.0, -1.0, -1.0)
        # 256-entry tables avoid a per-sample masking step
        self.gtx = [gx[i & 7] for i in range(256)]
        self.gty = [gy[i & 7] for i in range(256)]

    def fbm_grid(self, res, cells0, octaves, wrap=False, persistence=0.5):
        """Summed octaves on a res x res grid, returned as a flat list.

        With wrap=True each octave's lattice wraps at its own cell count, so
        the first and last row/column evaluate identical lattice points and
        the texture tiles exactly.
        """
        p = self.perm
        gtx = self.gtx
        gty = self.gty
        out = [0.0] * (res * res)
        denom = float(res - 1)
        amp = 1.0
        norm = 0.0
        cells = cells0
        for _oct in range(octaves):
            if wrap and cells > 256:
                raise ValueError("wrapped octave cell count exceeds 256")
            xi_t = [0] * res
            xj_t = [0] * res
            xf_t = [0.0] * res
            u_t = [0.0] * res
            for c in range(res):
                x = (c * cells) / denom
                xi = int(x)
                xf = x - xi
                if wrap:
                    xi %= cells
                    xj = (xi + 1) % cells
                else:
                    xj = xi + 1
                xi_t[c] = xi
                xj_t[c] = xj
                xf_t[c] = xf
                u_t[c] = xf * xf * xf * (xf * (xf * 6.0 - 15.0) + 10.0)
            # lattice column hashes are row-independent; hoist them
            px_t = [p[v] for v in xi_t]
            pxj_t = [p[v] for v in xj_t]
            idx = 0
            for r in range(res):
                y = (r * cells) / denom
                yi = int(y)
                yf = y - yi
                if wrap:
                    yi %= cells
                    yj = (yi + 1) % cells
                else:
                    yj = yi + 1
                v = yf * yf * yf * (yf * (yf * 6.0 - 15.0) + 10.0)
                yfm = yf - 1.0
                for c in range(res):
                    xf = xf_t[c]
                    xfm = xf - 1.0
                    pxc = px_t[c]
                    pxj = pxj_t[c]
                    h00 = p[pxc + yi]
                    h10 = p[pxj + yi]
                    h01 = p[pxc + yj]
                    h11 = p[pxj + yj]
                    n0 = gtx[h00] * xf + gty[h00] * yf
                    n1 = gtx[h10] * xfm + gty[h10] * yf
                    top = n0 + (n1 - n0) * u_t[c]
                    n0 = gtx[h01] * xf + gty[h01] * yfm
                    n1 = gtx[h11] * xfm + gty[h11] * yfm
                    bot = n0 + (n1 - n0) * u_t[c]
                    out[idx] += amp * (top + (bot - top) * v)
                    idx += 1
            norm += amp
            amp *= persistence
            cells *= 2
        inv = 1.0 / norm
        return [val * inv for val in out]

    def noise2(self, x, y, wrap=0):
        """Point-sample gradient noise; wrap>0 makes the lattice periodic
        with that period (in lattice cells)."""
        p = self.perm
        gtx = self.gtx
        gty = self.gty
        xi = math.floor(x)
        yi = math.floor(y)
        xf = x - xi
        yf = y - yi
        if wrap:
            xi %= wrap
            yi %= wrap
            xj = (xi + 1) % wrap
            yj = (yi + 1) % wrap
        else:
            xi &= 255
            yi &= 255
            xj = (xi + 1) & 255
            yj = (yi + 1) & 255
        u = xf * xf * xf * (xf * (xf * 6.0 - 15.0) + 10.0)
        v = yf * yf * yf * (yf * (yf * 6.0 - 15.0) + 10.0)
        h00 = p[p[xi] + yi]
        h10 = p[p[xj] + yi]
        h01 = p[p[xi] + yj]
        h11 = p[p[xj] + yj]
        yfm = yf - 1.0
        n0 = gtx[h00] * xf + gty[h00] * yf
        n1 = gtx[h10] * (xf - 1.0) + gty[h10] * yf
        top = n0 + (n1 - n0) * u
        n0 = gtx[h01] * xf + gty[h01] * yfm
        n1 = gtx[h11] * (xf - 1.0) + gty[h11] * yfm
        bot = n0 + (n1 - n0) * u
        return top + (bot - top) * v

    def fbm_pt(self, x, y, octaves, wrap_base=0, persistence=0.5):
        """Point-sampled fBm; octave k doubles frequency, wrap_base is the
        tile period in cells at the base octave (0 = no wrap)."""
        total = 0.0
        amp = 1.0
        norm = 0.0
        freq = 1
        wrap = wrap_base
        for _ in range(octaves):
            total += amp * self.noise2(x * freq, y * freq, wrap)
            norm += amp
            amp *= persistence
            freq *= 2
            wrap *= 2
        return total / norm


# ------------------------------------------------------------------ heightmap

def _terrace(v, levels):
    """Smooth terrace: flat treads, smoothstep risers (a hard floor() makes
    one-texel cliffs that alias into faceted triangle stripes)."""
    lv = v * levels
    i = math.floor(lv)
    f = lv - i
    f = f * f * (3.0 - 2.0 * f)
    return (i + f) / levels


def build_heightmap(args, perlin):
    """Normalized [0,1] height grid: fBm + ridged mix + terraces + flatten."""
    res = args.resolution
    raw = perlin.fbm_grid(res, 6, 6)
    # ridged-ish mix sharpens crests, then stretch to full range
    mixed = [0.65 * v + 0.35 * (1.0 - abs(v)) for v in raw]
    lo = min(mixed)
    hi = max(mixed)
    scale = 1.0 / (hi - lo)
    vals = [(v - lo) * scale for v in mixed]
    # light terracing: flat treads with smooth risers keep rocky cliffs
    # without one-texel steps
    levels = args.terrace_levels
    blend = args.terrace_blend
    vals = [(1.0 - blend) * v + blend * _terrace(v, levels)
            for v in vals]
    # the ridged mix skews the distribution high; recenter the median to 0.45
    # with a piecewise-affine map so lowlands stay grassy with a mud band and
    # only the high treads cross the snowline
    med = sorted(vals)[len(vals) // 2]
    lo2 = min(vals)
    hi2 = max(vals)
    target = 0.45
    if med - lo2 > 1e-9 and hi2 - med > 1e-9:
        a_lo = target / (med - lo2)
        a_hi = (1.0 - target) / (hi2 - med)
        vals = [(v - lo2) * a_lo if v <= med else target + (v - med) * a_hi
                for v in vals]
    flatten_disc(args, vals)
    return vals


def flatten_disc(args, vals):
    """Blend heights toward the disc-center height inside the flatten radius."""
    radius = args.flatten_radius
    if radius <= 0.0:
        return
    res = args.resolution
    n = res - 1
    wsx = args.world_size_x
    wsz = args.world_size_z
    fx = args.flatten_center_x
    fz = args.flatten_center_z
    cc = min(n, max(0, int(round((fx / wsx + 0.5) * n))))
    cr = min(n, max(0, int(round((fz / wsz + 0.5) * n))))
    target = vals[cr * res + cc]
    inner = radius * 0.7
    span = radius / wsz * n
    r0 = max(0, int(cr - span) - 1)
    r1 = min(n, int(cr + span) + 1)
    for r in range(r0, r1 + 1):
        wz = (r / n - 0.5) * wsz
        dz = wz - fz
        if abs(dz) >= radius:
            continue
        reach = math.sqrt(radius * radius - dz * dz)
        c0 = max(0, int(((fx - reach) / wsx + 0.5) * n) - 1)
        c1 = min(n, int(((fx + reach) / wsx + 0.5) * n) + 1)
        base = r * res
        for c in range(c0, c1 + 1):
            wx = (c / n - 0.5) * wsx
            d = math.hypot(wx - fx, dz)
            if d < radius:
                w = 1.0 - sstep(inner, radius, d)
                i = base + c
                vals[i] = vals[i] * (1.0 - w) + target * w


# -------------------------------------------------------------------- splat

def build_splat(args, hnorm):
    """RGBA weights from final height + slope. Returns (pixels, probes)."""
    res = args.resolution
    n = res - 1
    dx = args.world_size_x / n
    dz = args.world_size_z / n
    hs = args.height_scale
    water = args.water_level
    band = 0.035
    snow_line = args.snow_line
    th = args.slope_threshold
    pix = bytearray(res * res * 4)
    probes = {}
    best_steep = -1.0
    best_snow_slope = 2.0
    for r in range(res):
        rm = r - 1 if r > 0 else 0
        rp = r + 1 if r < n else n
        rowdz = (rp - rm) * dz
        base = r * res
        for c in range(res):
            cm = c - 1 if c > 0 else 0
            cp = c + 1 if c < n else n
            i = base + c
            dhdx = (hnorm[base + cp] - hnorm[base + cm]) * hs / ((cp - cm) * dx)
            dhdz = (hnorm[rp * res + c] - hnorm[rm * res + c]) * hs / rowdz
            slope = 1.0 - 1.0 / math.sqrt(dhdx * dhdx + dhdz * dhdz + 1.0)
            h = hnorm[i]
            # priority chain: rock, then snow, then mud, grass takes the rest
            w_rock = sstep(th - 0.08, th + 0.08, slope)
            rem = 1.0 - w_rock
            w_snow = sstep(snow_line - 0.05, snow_line + 0.05, h) * rem
            rem -= w_snow
            w_mud = (1.0 - sstep(water, water + band, h)) * rem
            rem -= w_mud
            g = int(rem * 255.0 + 0.5)
            rk = int(w_rock * 255.0 + 0.5)
            m = int(w_mud * 255.0 + 0.5)
            s = int(w_snow * 255.0 + 0.5)
            err = 255 - (g + rk + m + s)
            if err:
                # fold rounding error into the dominant channel
                quad = [g, rk, m, s]
                k = quad.index(max(quad))
                quad[k] += err
                g, rk, m, s = quad
            o = i * 4
            pix[o] = g
            pix[o + 1] = rk
            pix[o + 2] = m
            pix[o + 3] = s
            if slope > best_steep:
                best_steep = slope
                probes["steep"] = (r, c, h, slope, (g, rk, m, s))
            if h > snow_line + 0.05 and slope < best_snow_slope:
                best_snow_slope = slope
                probes["high"] = (r, c, h, slope, (g, rk, m, s))
            if ("flat" not in probes and slope < 0.08
                    and water + band + 0.03 < h < snow_line - 0.08):
                probes["flat"] = (r, c, h, slope, (g, rk, m, s))
    return bytes(pix), probes


# ------------------------------------------------------------------ textures

GROUNDS = (
    ("grass.png", (84.0, 102.0, 46.0), 0.16, (200, 230)),
    ("rock.png", (110.0, 102.0, 94.0), 0.18, (150, 190)),
    ("mud.png", (74.0, 56.0, 40.0), 0.15, (120, 160)),
    ("snow.png", (230.0, 234.0, 240.0), 0.05, (90, 140)),
)


def build_ground(name, base_col, lum, rough, perlin, size=256):
    """Tileable albedo: low-contrast luminance jitter around a base color,
    roughness variation packed into alpha."""
    low = perlin.fbm_grid(size, 4, 3, wrap=True)
    high = perlin.fbm_grid(size, 24, 2, wrap=True)
    rgh = perlin.fbm_grid(size, 12, 2, wrap=True)
    br, bg, bb = base_col
    rlo, rhi = rough
    rmid = 0.5 * (rlo + rhi)
    rspan = float(rhi - rlo)
    is_mud = name == "mud.png"
    pix = bytearray(size * size * 4)
    o = 0
    for i in range(size * size):
        lv = low[i]
        hv = high[i]
        v = 1.0 + lum * lv + 0.08 * hv
        if is_mud and lv > 0.1:
            v *= 1.0 - 0.18 * sstep(0.1, 0.4, lv)  # wet blotches
        pix[o] = clamp8(br * v + 3.0 * hv)
        pix[o + 1] = clamp8(bg * v + 1.0 * hv)
        pix[o + 2] = clamp8(bb * v - 2.0 * hv)
        t = clamp01(0.5 + 0.65 * rgh[i])
        pix[o + 3] = clamp8(rmid + rspan * (t - 0.5))
        o += 4
    return bytes(pix)


def build_cloud(perlin, warp_perlin, size=512):
    """Tileable: R coverage, G detail, B billow, A 255.

    The coverage channel is domain-warped fBm: sampling at p + k*w(p) breaks
    the Perlin lattice alignment that otherwise reads as square blobs at low
    coverage. Warp fields wrap at the same base period, so the tile stays
    exact (edges evaluate identical lattice points). 512px + 8 base cells
    keep a texel under ~3m at the demo's cloud scale - at low coverage each
    cloud is a noise PEAK, and at 256px the peaks resolve to single texels
    (square columns on screen).
    """
    W = 8        # base cells per tile edge
    KW = 0.8     # warp strength in cells
    det = perlin.fbm_grid(size, 16, 5, wrap=True)
    bil = perlin.fbm_grid(size, 9, 4, wrap=True)
    pix = bytearray(size * size * 4)
    inv = 1.0 / (size - 1)
    o = 0
    for j in range(size):
        v = j * inv * W
        for i in range(size):
            u = i * inv * W
            wu = warp_perlin.fbm_pt(u, v, 3, wrap_base=W)
            wv = warp_perlin.fbm_pt(u + 1.3, v + 7.9, 3, wrap_base=W)
            c = perlin.fbm_pt(u + KW * wu, v + KW * wv, 4, wrap_base=W)
            c = clamp01(0.5 + 0.9 * c)
            c = c * c * (3.0 - 2.0 * c)
            d = clamp01(0.5 + 0.7 * det[j * size + i])
            b = clamp01(1.0 - abs(1.45 * bil[j * size + i]))
            pix[o] = int(c * 255.0 + 0.5)
            pix[o + 1] = int(d * 255.0 + 0.5)
            pix[o + 2] = int(b * 255.0 + 0.5)
            pix[o + 3] = 255
            o += 4
    return bytes(pix)


def build_moon(perlin, size=128):
    """Gray albedo with darker maria and crater speckle; alpha 255."""
    maria = perlin.fbm_grid(size, 4, 4)
    grain = perlin.fbm_grid(size, 20, 2)
    crat = perlin.fbm_grid(size, 28, 2)
    pix = bytearray(size * size * 4)
    o = 0
    for i in range(size * size):
        m = 0.85 * sstep(0.05, 0.35, maria[i])
        f = 1.0 + 0.05 * grain[i]
        cv = crat[i]
        crater = sstep(0.42, 0.52, cv)
        rim = sstep(0.34, 0.39, cv) * (1.0 - sstep(0.39, 0.44, cv))
        f *= 1.0 - 0.38 * crater + 0.10 * rim
        pix[o] = clamp8((186.0 * (1.0 - m) + 104.0 * m) * f)
        pix[o + 1] = clamp8((189.0 * (1.0 - m) + 107.0 * m) * f)
        pix[o + 2] = clamp8((194.0 * (1.0 - m) + 114.0 * m) * f)
        pix[o + 3] = 255
        o += 4
    return bytes(pix)


# ----------------------------------------------------------------- generate

def generate(args, outdir):
    """Write all assets into outdir. Returns info used by the self-checks."""
    os.makedirs(outdir, exist_ok=True)
    rng = random.Random(args.seed)
    perl_h = Perlin(rng)
    perl_g = Perlin(rng)
    perl_c = Perlin(rng)
    perl_m = Perlin(rng)
    res = args.resolution
    info = {"pixels": {}, "probes": {}, "times": {}}

    t0 = time.perf_counter()
    hnorm = build_heightmap(args, perl_h)
    q = [int(v * 65535.0 + 0.5) for v in hnorm]
    arr = array.array("H", q)
    if sys.byteorder == "big":
        arr.byteswap()
    with open(os.path.join(outdir, "height.r16"), "wb") as f:
        f.write(arr.tobytes())
    sidecar = {
        "resolution": res,
        "worldSizeX": args.world_size_x,
        "worldSizeZ": args.world_size_z,
        "heightScale": args.height_scale,
    }
    with open(os.path.join(outdir, "height.json"), "w", newline="\n") as f:
        f.write(json.dumps(sidecar, indent=2, sort_keys=True) + "\n")
    gray = bytes(int(v * 255.0 + 0.5) for v in hnorm)
    write_png(os.path.join(outdir, "height_preview.png"), res, res, 0, gray)
    info["pixels"]["height_preview.png"] = gray
    info["times"]["heightmap"] = time.perf_counter() - t0

    t0 = time.perf_counter()
    splat, probes = build_splat(args, hnorm)
    write_png(os.path.join(outdir, "splat.png"), res, res, 6, splat)
    info["pixels"]["splat.png"] = splat
    info["probes"] = probes
    info["times"]["splat"] = time.perf_counter() - t0

    t0 = time.perf_counter()
    for name, base_col, lum, rough in GROUNDS:
        pix = build_ground(name, base_col, lum, rough, perl_g)
        write_png(os.path.join(outdir, name), 256, 256, 6, pix)
        info["pixels"][name] = pix
    info["times"]["grounds"] = time.perf_counter() - t0

    t0 = time.perf_counter()
    cloud = build_cloud(perl_c, Perlin(rng))
    cloud_size = int(math.sqrt(len(cloud) // 4))
    write_png(os.path.join(outdir, "cloud_noise.png"), cloud_size, cloud_size, 6, cloud)
    info["pixels"]["cloud_noise.png"] = cloud
    moon = build_moon(perl_m)
    write_png(os.path.join(outdir, "moon.png"), 128, 128, 6, moon)
    info["pixels"]["moon.png"] = moon
    info["times"]["sky"] = time.perf_counter() - t0
    return info


# --------------------------------------------------------------- self-checks

def sha256_path(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def edges_equal(pix, w, h):
    for r in range(h):
        a = pix[(r * w) * 4:(r * w) * 4 + 4]
        b = pix[(r * w + w - 1) * 4:(r * w + w - 1) * 4 + 4]
        if a != b:
            return False
    return pix[:w * 4] == pix[(h - 1) * w * 4:h * w * 4]


def check_determinism(args):
    """Generate twice into two temp dirs, compare sha256 of every file."""
    d1 = tempfile.mkdtemp(prefix="terrain_gen_a_")
    d2 = tempfile.mkdtemp(prefix="terrain_gen_b_")
    try:
        a1 = argparse.Namespace(**vars(args))
        a1.out = d1
        a2 = argparse.Namespace(**vars(args))
        a2.out = d2
        generate(a1, d1)
        generate(a2, d2)
        mismatch = []
        for name in ASSET_FILES:
            if sha256_path(os.path.join(d1, name)) != sha256_path(os.path.join(d2, name)):
                mismatch.append(name)
        return mismatch
    finally:
        shutil.rmtree(d1, ignore_errors=True)
        shutil.rmtree(d2, ignore_errors=True)


def run_checks(args, info):
    failures = []

    def record(name, status, detail=""):
        print("[self-check] %-30s %s %s" % (name, status, detail))
        if status == "FAIL":
            failures.append(name)

    # 1. height.r16 size + sidecar matches CLI args
    res = args.resolution
    r16 = os.path.join(args.out, "height.r16")
    size = os.path.getsize(r16)
    ok = size == 2 * res * res
    record("height.r16 size", "PASS" if ok else "FAIL",
           "%d bytes, expected %d" % (size, 2 * res * res))
    try:
        with open(os.path.join(args.out, "height.json")) as f:
            sc = json.load(f)
        ok = (sc["resolution"] == res
              and sc["worldSizeX"] == args.world_size_x
              and sc["worldSizeZ"] == args.world_size_z
              and sc["heightScale"] == args.height_scale)
        record("height.json sidecar", "PASS" if ok else "FAIL", json.dumps(sc, sort_keys=True))
    except Exception as exc:
        record("height.json sidecar", "FAIL", str(exc))

    # 2. splat.png round-trips through a fresh zlib parse, sums to 255
    try:
        w, h, ctype, decoded = parse_png(os.path.join(args.out, "splat.png"))
        ok = (w == res and h == res and ctype == 6
              and decoded == info["pixels"]["splat.png"])
        record("splat.png zlib roundtrip", "PASS" if ok else "FAIL",
               "%dx%d ctype=%d" % (w, h, ctype))
    except Exception as exc:
        record("splat.png zlib roundtrip", "FAIL", str(exc))
    splat = info["pixels"]["splat.png"]
    total = res * res
    bad = 0
    sampled = 0
    for k in range(300):
        i = (k * 7919) % total
        o = i * 4
        sampled += 1
        if abs(splat[o] + splat[o + 1] + splat[o + 2] + splat[o + 3] - 255) > 1:
            bad += 1
    record("splat channel sums", "PASS" if bad == 0 else "FAIL",
           "%d texels sampled, %d off" % (sampled, bad))

    # 3. probe texels: rule verified numerically
    probes = info["probes"]
    for key, expect in (("steep", "rock"), ("flat", "grass"), ("high", "snow")):
        if key not in probes:
            record("splat probe %s" % key, "SKIP", "no qualifying texel")
            continue
        r, c, hv, slope, wq = probes[key]
        dom = LAYER_NAMES[wq.index(max(wq))]
        detail = ("texel=(%d,%d) h=%.3f slope=%.3f dominant=%s (g,r,m,s)=%s"
                  % (r, c, hv, slope, dom, wq))
        print("[splat-probe] %-9s %s" % (key, detail))
        applies = True
        if key == "steep":
            applies = slope > args.slope_threshold
        elif key == "high":
            applies = hv > args.snow_line + 0.05 and slope < args.slope_threshold - 0.08
        if not applies:
            record("splat probe %s" % key, "SKIP", "rule precondition not met")
        else:
            record("splat probe %s" % key,
                   "PASS" if dom == expect else "FAIL",
                   "expected dominant %s" % expect)

    # 4. tileable textures: exact edge equality
    for name in TILEABLE_FILES:
        pix = info["pixels"][name]
        size = int(math.sqrt(len(pix) // 4))
        ok = edges_equal(pix, size, size)
        record("tileable %s" % name, "PASS" if ok else "FAIL",
               "left==right and top==bottom" if ok else "edge mismatch")

    # 5. determinism: two fresh runs byte-identical
    t0 = time.perf_counter()
    mismatch = check_determinism(args)
    record("determinism (2 temp runs)", "PASS" if not mismatch else "FAIL",
           "%d files sha256-identical in %.1fs" % (len(ASSET_FILES), time.perf_counter() - t0)
           if not mismatch else "mismatch: %s" % ",".join(mismatch))

    return not failures


# ---------------------------------------------------------------------- cli

def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--resolution", type=int, default=513, help="2^k+1")
    ap.add_argument("--world-size-x", type=float, default=51200.0, help="cm")
    ap.add_argument("--world-size-z", type=float, default=51200.0, help="cm")
    ap.add_argument("--height-scale", type=float, default=6000.0, help="cm")
    ap.add_argument("--water-level", type=float, default=0.18,
                    help="fraction of height scale")
    ap.add_argument("--snow-line", type=float, default=0.62, help="fraction")
    ap.add_argument("--slope-threshold", type=float, default=0.55, help="0..1")
    ap.add_argument("--flatten-center-x", type=float, default=0.0, help="cm")
    ap.add_argument("--flatten-center-z", type=float, default=-200.0, help="cm")
    ap.add_argument("--flatten-radius", type=float, default=9000.0, help="cm")
    ap.add_argument("--terrace-levels", type=int, default=6)
    ap.add_argument("--terrace-blend", type=float, default=0.25,
                    help="0 = no terraces, 1 = full stairs")
    ap.add_argument("--out", default="examples/Projects/outdoor/Terrain")
    ap.add_argument("--skip-checks", action="store_true",
                    help="skip the self-verification pass")
    return ap.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if args.resolution < 2:
        print("error: resolution must be >= 2", file=sys.stderr)
        return 2
    t0 = time.perf_counter()
    info = generate(args, args.out)
    print("[gen] wrote %d assets to %s" % (len(ASSET_FILES), args.out))
    for name in ASSET_FILES:
        path = os.path.join(args.out, name)
        print("[gen]   %-20s %9d bytes" % (name, os.path.getsize(path)))
    for stage, dt in sorted(info["times"].items()):
        print("[gen] stage %-10s %6.2fs" % (stage, dt))
    print("[gen] generation total %.2fs" % (time.perf_counter() - t0))
    if args.skip_checks:
        return 0
    ok = run_checks(args, info)
    print("[self-check] overall: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
