#!/usr/bin/env python3
"""FFT ocean wave bake tool (stdlib only, deterministic).

Bakes tileable, seamlessly looping ocean animation from a Phillips
directional spectrum (Tessendorf-style choppy waves). Per band outputs:

  <band>_disp.f16   frames x N x N x RGBA16 half LE: dx, dy, dz, foam
  <band>_nrm.u8     frames x N x N x RGBA8: normal xyz *0.5+0.5, alpha=foam
  ocean.json        params, per-band info, amplitude range, sha256

Field layout: frame-major, inside a frame rows=z cols=x (row-major),
matching the Heightmap [z*N+x] convention. Loop is exact: every angular
frequency is quantized to an integer multiple of 2*pi/loop_seconds.

numpy is used when importable (~50x faster); the pure-python fallback is
the reference path and produces the same values within float rounding.
"""

import argparse
import cmath
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

try:
    import numpy as _np
except ImportError:
    _np = None

GRAVITY = 981.0          # cm/s^2 (engine units are cm)
FOAM_BIAS = 1.0          # foam where jacobian J < FOAM_BIAS
FOAM_GAIN = 2.0          # foam = clamp01((FOAM_BIAS - J) * FOAM_GAIN)
FETCH_DAMP_FRAC = 0.016  # small-k damping length = fetch_cm * this


def clamp01(x):
    return 0.0 if x < 0.0 else (1.0 if x > 1.0 else x)


def clamp8(x):
    if x < 0.0:
        return 0
    if x > 255.0:
        return 255
    return int(x + 0.5)


# ------------------------------------------------------------------ spectrum

def freq_index(i, n):
    return i if i < n // 2 else i - n


def phillips(kx, kz, wind_speed, dir_x, dir_z, damp_len):
    k = math.sqrt(kx * kx + kz * kz)
    if k < 1e-9:
        return 0.0
    l_cap = wind_speed * wind_speed / GRAVITY
    spec = math.exp(-1.0 / (k * l_cap) ** 2) / (k ** 4)
    spec *= (kx / k * dir_x + kz / k * dir_z) ** 2
    spec *= math.exp(-(k * damp_len) ** 2)
    return spec


def build_h0(n, tile_cm, wind_speed, dir_deg, damp_len, rng, omega_max):
    """h0 spectrum in array order + per-texel quantized omega index m.
    Modes above omega_max (the frame-sampling Nyquist) are rolled off with
    exp(-(w/wmax)^4): a mode faster than the baked frame rate can't lerp
    between two of its phases - the in-shader frame lerp just morphs the
    pattern, which reads as flicker/morphing rather than wave motion."""
    dir_x = math.cos(math.radians(dir_deg))
    dir_z = math.sin(math.radians(dir_deg))
    h0 = [[0j] * n for _ in range(n)]
    omega = [[0.0] * n for _ in range(n)]
    dk = 2.0 * math.pi / tile_cm
    inv_sqrt2 = 1.0 / math.sqrt(2.0)
    for i in range(n):
        kz = freq_index(i, n) * dk
        for j in range(n):
            kx = freq_index(j, n) * dk
            p = phillips(kx, kz, wind_speed, dir_x, dir_z, damp_len)
            if p <= 0.0:
                continue
            w = math.sqrt(GRAVITY * math.hypot(kx, kz))
            amp = inv_sqrt2 * math.sqrt(p)
            amp *= math.exp(-((w / omega_max) ** 4))
            h0[i][j] = complex(rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0)) * amp
            omega[i][j] = w
    return h0, omega


# ------------------------------------------------------------------ pure fft

def _twiddles(n, sign):
    return [cmath.exp(sign * 2j * math.pi * k / n) for k in range(n // 2)]


def _fft1d(a, tw):
    n = len(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        half = size >> 1
        step = n // size
        for i in range(0, n, size):
            k = 0
            for p in range(i, i + half):
                q = p + half
                t = tw[k] * a[q]
                a[q] = a[p] - t
                a[p] += t
                k += step
        size <<= 1


def _ifft2(m, tw, inv_n):
    for row in m:
        _fft1d(row, tw)
    m[:] = [list(r) for r in zip(*m)]
    for row in m:
        _fft1d(row, tw)
    m[:] = [list(r) for r in zip(*m)]
    scale = inv_n * inv_n
    for row in m:
        for j in range(len(row)):
            row[j] *= scale


# ------------------------------------------------------------------ band bake

def bake_band(n, tile_cm, frames, loop_s, wind_speed, dir_deg, damp_len,
              chop, rng):
    """Returns (disp, nrm): per-frame flat lists of (dx,dy,dz,foam) floats
    and (nx,ny,nz,foam) 0..255 ints, plus max|dy| pre-normalization."""
    # frame-sampling Nyquist with margin (3.8+ samples/cycle): a faster mode
    # aliases through the in-shader frame lerp (pattern morphs instead of
    # flowing = flicker)
    omega_max = math.pi * frames / loop_s * 0.7
    h0, omega = build_h0(n, tile_cm, wind_speed, dir_deg, damp_len, rng,
                         omega_max)
    dk = 2.0 * math.pi / tile_cm
    # quantize omega to integer multiples of 2*pi/loop for an exact loop
    m_idx = [[int(round(omega[i][j] * loop_s / (2.0 * math.pi)))
              for j in range(n)] for i in range(n)]
    h0neg = [[h0[(-i) % n][(-j) % n].conjugate()
              for j in range(n)] for i in range(n)]
    kx_over_k = [[0.0] * n for _ in range(n)]
    kz_over_k = [[0.0] * n for _ in range(n)]
    for i in range(n):
        kz = freq_index(i, n) * dk
        for j in range(n):
            kx = freq_index(j, n) * dk
            k = math.hypot(kx, kz)
            if k > 1e-9:
                kx_over_k[i][j] = kx / k
                kz_over_k[i][j] = kz / k
    # phase lookup: t_f = f*loop/frames -> exp(-i*w*t) = exp(-2pi i f m/F)
    trig_m = [[cmath.exp(-2j * math.pi * f * mm / frames)
               for mm in range(frames)] for f in range(frames)]

    raw = []  # per frame flat lists of (dx, dy, dz)
    if _np is not None:
        raw = _bake_frames_numpy(h0, h0neg, m_idx, kx_over_k, kz_over_k,
                                 chop, n, frames)
    else:
        raw = _bake_frames_pure(h0, h0neg, m_idx, kx_over_k, kz_over_k,
                                chop, n, frames, trig_m)
    return raw


def _bake_frames_pure(h0, h0neg, m_idx, kx_over_k, kz_over_k, chop, n,
                      frames, trig_m):
    tw = _twiddles(n, +1)  # inverse sign
    inv_n = 1.0 / n
    out = []
    for f in range(frames):
        trig = trig_m[f]
        c1 = [[0j] * n for _ in range(n)]
        c2 = [[0j] * n for _ in range(n)]
        for i in range(n):
            h0r, h0nr, mr = h0[i], h0neg[i], m_idx[i]
            kxr, kzr = kx_over_k[i], kz_over_k[i]
            c1r, c2r = c1[i], c2[i]
            for j in range(n):
                a = h0r[j]
                if a == 0j:
                    continue
                e = trig[mr[j] % frames]
                h = a * e + h0nr[j] * e.conjugate()
                dxs = -1j * kxr[j] * chop * h
                dzs = -1j * kzr[j] * chop * h
                c1r[j] = h + 1j * dxs
                c2r[j] = dzs
        _ifft2(c1, tw, inv_n)
        _ifft2(c2, tw, inv_n)
        fr = []
        for i in range(n):
            for j in range(n):
                v = c1[i][j]
                fr.append((v.imag, v.real, c2[i][j].real))
        out.append(fr)
    return out


def _bake_frames_numpy(h0, h0neg, m_idx, kx_over_k, kz_over_k, chop, n,
                       frames):
    h0a = _np.array(h0, dtype=_np.complex128)
    h0na = _np.array(h0neg, dtype=_np.complex128)
    ma = _np.array(m_idx, dtype=_np.int64)
    kxa = _np.array(kx_over_k)
    kza = _np.array(kz_over_k)
    out = []
    for f in range(frames):
        e = _np.exp(-2j * _np.pi * f * ma / frames)
        h = h0a * e + h0na * _np.conj(e)
        dxs = -1j * kxa * chop * h
        dzs = -1j * kza * chop * h
        c1 = _np.fft.ifft2(h + 1j * dxs)
        c2 = _np.fft.ifft2(dzs)
        fr = []
        for i in range(n):
            for j in range(n):
                fr.append((c1[i, j].imag, c1[i, j].real, c2[i, j].real))
        out.append(fr)
    return out


def normalize(raw, target_amp):
    peak = 0.0
    for fr in raw:
        for (_, dy, _) in fr:
            if abs(dy) > peak:
                peak = abs(dy)
    if peak <= 0.0:
        raise RuntimeError("degenerate spectrum: zero amplitude")
    s = target_amp / peak
    return [[(dx * s, dy * s, dz * s) for (dx, dy, dz) in fr] for fr in raw], peak


def foam_and_normals(raw, n, tile_cm):
    """Central differences (wrap) -> jacobian foam + normals, per frame."""
    ds = tile_cm / n
    inv_2ds = 1.0 / (2.0 * ds)
    disp_out = []
    nrm_out = []
    max_foam = 0.0
    for fr in raw:
        disp = [0.0] * (n * n * 4)
        nrm = [0] * (n * n * 4)
        for i in range(n):
            ip = (i + 1) % n
            im = (i - 1) % n
            for j in range(n):
                jp = (j + 1) % n
                jm = (j - 1) % n
                o = i * n + j
                dx = fr[o][0]
                dy = fr[o][1]
                dz = fr[o][2]
                dxx = (fr[i * n + jp][0] - fr[i * n + jm][0]) * inv_2ds
                dxz = (fr[ip * n + j][0] - fr[im * n + j][0]) * inv_2ds
                dzx = (fr[i * n + jp][2] - fr[i * n + jm][2]) * inv_2ds
                dzz = (fr[ip * n + j][2] - fr[im * n + j][2]) * inv_2ds
                jac = (1.0 + dxx) * (1.0 + dzz) - dxz * dzx
                foam = clamp01((FOAM_BIAS - jac) * FOAM_GAIN)
                if foam > max_foam:
                    max_foam = foam
                ddyx = (fr[i * n + jp][1] - fr[i * n + jm][1]) * inv_2ds
                ddyz = (fr[ip * n + j][1] - fr[im * n + j][1]) * inv_2ds
                nx, ny, nz = -ddyx, 1.0, -ddyz
                nl = math.sqrt(nx * nx + ny * ny + nz * nz)
                nx, ny, nz = nx / nl, ny / nl, nz / nl
                disp[o * 4:o * 4 + 4] = [dx, dy, dz, foam]
                f8 = clamp8(foam * 255.0)
                nrm[o * 4:o * 4 + 4] = [clamp8((nx * 0.5 + 0.5) * 255.0),
                                        clamp8((ny * 0.5 + 0.5) * 255.0),
                                        clamp8((nz * 0.5 + 0.5) * 255.0), f8]
        disp_out.append(disp)
        nrm_out.append(nrm)
    return disp_out, nrm_out, max_foam


# ------------------------------------------------------------------ writers

def write_f16(path, frames):
    buf = bytearray()
    for fr in frames:
        for k in range(0, len(fr), 512):
            chunk = fr[k:k + 512]
            buf += struct.pack("<%de" % len(chunk),
                               *[max(-60000.0, min(60000.0, v))
                                 for v in chunk])
    with open(path, "wb") as fh:
        fh.write(buf)


def write_u8(path, frames):
    buf = bytearray()
    for fr in frames:
        buf += bytes(fr)
    with open(path, "wb") as fh:
        fh.write(buf)


# ------------------------------------------------------------------ previews

def _png_chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag
            + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))


def write_png_rgb(path, w, h, rgb):
    """Minimal stdlib 8-bit RGB PNG writer (filter 0 rows, zlib IDAT)."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit truecolor
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(_png_chunk(b"IHDR", ihdr))
        fh.write(_png_chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        fh.write(_png_chunk(b"IEND", b""))


def write_previews(out_dir, band, res, disp, nrm):
    """Human-viewable previews next to the binary payloads (the .f16/.u8
    stay authoritative for the engine): the wave height field at frame 0
    (dy normalized to the band's observed peak) and a max-projection of
    the foam channel over all frames (normalized grayscale)."""
    texel_count = res * res
    peak = 1e-6
    for k in range(texel_count):
        v = abs(disp[0][k * 4 + 1])
        if v > peak:
            peak = v
    gray = bytearray()
    for k in range(texel_count):
        v = int(max(0.0, min(255.0, (disp[0][k * 4 + 1] / peak * 0.5 + 0.5) * 255.0)))
        gray += bytes((v, v, v))
    height_png = "%s_height_preview.png" % band
    write_png_rgb(os.path.join(out_dir, height_png), res, res, gray)
    foam_max = bytearray(texel_count)
    peak_foam = 1e-6
    for fr in nrm:
        for k in range(texel_count):
            v = fr[k * 4 + 3]
            if v > foam_max[k]:
                foam_max[k] = v
            if v > peak_foam:
                peak_foam = v
    gray = bytearray()
    for v in foam_max:
        g = int(max(0.0, min(255.0, v / peak_foam * 255.0)))
        gray += bytes((g, g, g))
    foam_png = "%s_foam_preview.png" % band
    write_png_rgb(os.path.join(out_dir, foam_png), res, res, gray)
    return height_png, foam_png


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ------------------------------------------------------------------ checks

def seam_error(frames, n):
    """Wrap quality on the dy channel: spatial wrap-edge steps and the
    frame-(F-1)->0 loop step, each against the field's own MAX adjacency
    (the wrap is exact by construction, so a seam can never legitimately
    exceed the biggest interior step; the old 3x-mean heuristic false-
    positives once the spectrum carries its full high-frequency range -
    max/mean for 16k Gaussian-ish samples sits at ~4)."""
    def dy(fr, i, j):
        return frames[fr][(i * n + j) * 4 + 1]
    seam = 0.0
    f0 = 0
    max_adj = 0.0
    for i in range(0, n, 4):
        for j in range(0, n, 4):
            ax = abs(dy(f0, i, j) - dy(f0, i, (j + 1) % n))
            az = abs(dy(f0, i, j) - dy(f0, (i + 1) % n, j))
            max_adj = max(max_adj, ax, az)
            seam = max(seam, abs(dy(f0, i, n - 1) - dy(f0, i, 0)))
            seam = max(seam, abs(dy(f0, n - 1, j) - dy(f0, 0, j)))
    spatial_limit = max(1e-4, max_adj)
    nf = len(frames)
    max_t = 0.0
    loop_seam = 0.0
    for fr in range(nf):
        nxt = (fr + 1) % nf
        for i in range(0, n, 4):
            for j in range(0, n, 4):
                d = abs(dy(fr, i, j) - dy(nxt, i, j))
                if fr < nf - 1:
                    max_t = max(max_t, d)
                else:
                    loop_seam = max(loop_seam, d)
    loop_limit = max(1e-4, max_t)
    return seam, spatial_limit, loop_seam, loop_limit


def run_checks(args, out_dir, band_info):
    ok = True
    for name, info in band_info.items():
        n = info["resolution"]
        path = os.path.join(out_dir, info["dispFile"])
        vals = []
        with open(path, "rb") as fh:
            data = fh.read()
        nvals = len(data) // 2
        vals = list(struct.unpack("<%de" % nvals, data[:nvals * 2]))
        frames = [vals[f * n * n * 4:(f + 1) * n * n * 4]
                  for f in range(args.frames)]
        seam, spatial_limit, loop_seam, loop_limit = seam_error(frames, n)
        print("check %s: seam=%.4f/%.4f loop=%.4f/%.4f"
              % (name, seam, spatial_limit, loop_seam, loop_limit))
        if seam > spatial_limit or loop_seam > loop_limit:
            print("FAIL: %s wrap discontinuity" % name)
            ok = False
        if args.choppiness > 0.0:
            max_dx = max(abs(frames[0][k * 4]) for k in range(0, n * n, 7))
            max_dy = max(abs(frames[0][k * 4 + 1]) for k in range(0, n * n, 7))
            if max_dx <= 0.01 * max_dy:
                print("FAIL: %s horizontal displacement ~zero" % name)
                ok = False
        max_foam = max(frames[f][k * 4 + 3]
                       for f in range(0, args.frames, 4)
                       for k in range(0, n * n, 7))
        if max_foam <= 0.0:
            print("FAIL: %s foam all zero" % name)
            ok = False
    with open(os.path.join(out_dir, "ocean.json")) as fh:
        side = json.load(fh)
    for field in ("seed", "frames", "loopSeconds", "windSpeedCmPerSec",
                  "windDirectionDeg", "fetchCm", "choppiness"):
        want = getattr(args, {"windSpeedCmPerSec": "wind_speed",
                              "windDirectionDeg": "wind_direction",
                              "fetchCm": "fetch_cm",
                              "loopSeconds": "loop_seconds"}.get(field, field))
        if abs(float(side[field]) - float(want)) > 1e-6:
            print("FAIL: sidecar %s mismatch" % field)
            ok = False
    return ok


# ------------------------------------------------------------------ main

def bake_all(args, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(args.seed)
    band_info = {}
    for band, res, tile, amp in (
            ("swell", args.swell_resolution, args.swell_tile_cm,
             args.swell_amplitude_cm),
            ("ripple", args.ripple_resolution, args.ripple_tile_cm,
             args.ripple_amplitude_cm),
            ("chop", args.chop_resolution, args.chop_tile_cm,
             args.chop_amplitude_cm)):
        if res & (res - 1):
            raise RuntimeError("%s resolution must be a power of 2" % band)
        t0 = time.time()
        # high-frequency cutoff is TILE-relative: a fetch-relative length
        # (16 m at the default fetch) sat inside the small tiles' passband
        # and zeroed every mode - the 8 m "ripple" band was a single-mode
        # plane wave (the visible dash repetition), and a 3 m chop tile
        # degenerated entirely. 2 texels keeps the band's full range and
        # cuts only at its Nyquist edge.
        damp_len = tile * 2.0 / res
        raw = bake_band(res, tile, args.frames, args.loop_seconds,
                        args.wind_speed, args.wind_direction, damp_len,
                        args.choppiness, rng)
        raw, peak = normalize(raw, amp)
        disp, nrm, max_foam = foam_and_normals(raw, res, tile)
        disp_file = "%s_disp.f16" % band
        nrm_file = "%s_nrm.u8" % band
        write_f16(os.path.join(out_dir, disp_file), disp)
        write_u8(os.path.join(out_dir, nrm_file), nrm)
        nrm_png, foam_png = write_previews(out_dir, band, res, disp, nrm)
        band_info[band] = {
            "tileCm": tile, "resolution": res,
            "dispFile": disp_file, "nrmFile": nrm_file,
            "previewHeight": nrm_png, "previewFoam": foam_png,
            "amplitudeCm": amp, "preNormPeakDy": round(peak, 4),
            "maxFoam": round(max_foam, 4),
        }
        print("band %s: %dx%d x%d frames in %.1fs (peak dy %.1f cm)"
              % (band, res, res, args.frames, time.time() - t0, amp))
    side = {
        "format": 1,
        "seed": args.seed,
        "frames": args.frames,
        "loopSeconds": args.loop_seconds,
        "windSpeedCmPerSec": args.wind_speed,
        "windDirectionDeg": args.wind_direction,
        "fetchCm": args.fetch_cm,
        "choppiness": args.choppiness,
        "foamBias": FOAM_BIAS,
        "foamGain": FOAM_GAIN,
        "bands": band_info,
        "layout": {"disp": "RGBA16 half LE, frame-major, rows=z cols=x",
                   "nrm": "RGBA8, frame-major, rows=z cols=x"},
    }
    with open(os.path.join(out_dir, "ocean.json"), "w") as fh:
        json.dump(side, fh, indent=2, sort_keys=True)
    hashes = {}
    for name in sorted(os.listdir(out_dir)):
        if name in ("ocean.json",):
            continue
        hashes[name] = sha256_file(os.path.join(out_dir, name))
    side["sha256"] = hashes
    with open(os.path.join(out_dir, "ocean.json"), "w") as fh:
        json.dump(side, fh, indent=2, sort_keys=True)
    return band_info


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--swell-resolution", type=int, default=128)
    ap.add_argument("--ripple-resolution", type=int, default=64)
    ap.add_argument("--frames", type=int, default=32)
    ap.add_argument("--loop-seconds", type=float, default=12.0)
    ap.add_argument("--swell-tile-cm", type=float, default=10000.0)
    ap.add_argument("--ripple-tile-cm", type=float, default=800.0)
    ap.add_argument("--swell-amplitude-cm", type=float, default=80.0)
    ap.add_argument("--ripple-amplitude-cm", type=float, default=8.0)
    # third cascade (the "chop" band): fills the 0.5-4 m gap between the
    # ripple and swell tiles - carries normals + foam only in the shader
    # (no vertex displacement), so buoyancy is unaffected
    ap.add_argument("--chop-resolution", type=int, default=64)
    ap.add_argument("--chop-tile-cm", type=float, default=300.0)
    ap.add_argument("--chop-amplitude-cm", type=float, default=2.0)
    ap.add_argument("--wind-speed", type=float, default=800.0,
                    help="cm/s (800 = 8 m/s)")
    ap.add_argument("--wind-direction", type=float, default=35.0,
                    help="degrees")
    ap.add_argument("--fetch-cm", type=float, default=100000.0)
    ap.add_argument("--choppiness", type=float, default=1.0)
    ap.add_argument("--out", default="examples/Projects/ocean/Ocean")
    ap.add_argument("--skip-checks", action="store_true")
    args = ap.parse_args()
    if _np is None:
        print("note: numpy not found, using pure-python FFT (slower)")
    if _np is None and args.swell_resolution >= 256:
        print("warning: pure-python bake at resolution >= 256 is slow")
    t0 = time.time()
    band_info = bake_all(args, args.out)
    ok = True
    if not args.skip_checks:
        ok = run_checks(args, args.out, band_info)
        if ok:
            tmp = tempfile.mkdtemp(prefix="ocean_bake_check")
            try:
                bake_all(args, tmp)
                for name in sorted(os.listdir(args.out)):
                    a = sha256_file(os.path.join(args.out, name))
                    b = sha256_file(os.path.join(tmp, name))
                    if a != b:
                        print("FAIL: determinism mismatch on %s" % name)
                        ok = False
            finally:
                shutil.rmtree(tmp, ignore_errors=True)
    print("bake %s in %.1fs -> %s"
          % ("OK" if ok else "FAILED", time.time() - t0, args.out))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
