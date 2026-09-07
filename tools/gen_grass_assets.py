#!/usr/bin/env python3
"""tools/gen_grass_assets.py - offline grass-clump baker (no Kraut; ground
cover is too light-weight for a full tree generator).

Emits a grass clump as a glb with the kraut contract so the stock import
path (`fury kraut import`) applies all vegetation postprocessing:
  - GrassClump_LOD0: 3 crossed alpha cards (full clump)
  - GrassClump_LOD1: 2 crossed cards (reduced)
  - GrassClump_Billboard: 1 quad, denser baked texture, terminal tier via
    the BILLBOARD shader path (1x1 atlas -- a grass clump reads the same
    from every azimuth)
  - COLOR_0 carries the kraut wind encoding: R = sway (0 root -> 1 tip),
    G = flutter, B = per-card phase, A = 1
  - asset.extras.kraut: lod_thresholds + billboard block (atlas 1x1)

Usage:
  python3 tools/gen_grass_assets.py [--seed N]   (run from repo root)
Writes examples/Resource/Trees/Grass/{GrassClump.glb, GrassClump.bin,
GrassClump_D.png, GrassClump_Billboard.png} and validates the .bin
round-trip via the engine CLI.
"""

import json
import math
import os
import random
import struct
import subprocess
import sys

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "examples", "Resource", "Trees", "Grass")
FURY = os.path.join(REPO, "examples", "fury")

CARD_W = 0.6   # meters
CARD_H = 0.38  # short meadow grass (was 0.6 -- read too tall/reedy)
TEX = 256


# ---------------------------------------------------------------- textures

def draw_blades(path, blade_count, seed, width_scale=1.0):
    """Vertical grass-blade texture: dark root -> light tip, alpha cutout.
    Blades are tapered quadratic curves standing on the bottom edge."""
    rng = random.Random(seed)
    img = Image.new("RGBA", (TEX, TEX), (0, 0, 0, 0))
    drw = ImageDraw.Draw(img)
    for _ in range(blade_count):
        bx = rng.uniform(TEX * 0.06, TEX * 0.94)
        lean = rng.uniform(-TEX * 0.16, TEX * 0.16)
        tip_y = rng.uniform(TEX * 0.02, TEX * 0.25)
        w0 = rng.uniform(5.0, 8.0) * width_scale
        hue = rng.uniform(-8, 10)
        # matched to the island terrain's grass layer: saturated olive,
        # dark enough that full sun doesn't blow out to straw
        root_c = (int(24 + hue * 0.5), int(46 + hue), int(16))
        tip_c = (int(58 + hue), int(104 + hue), int(38))

        # quadratic bezier root -> tip, rasterized as a tapered ribbon
        p0 = (bx, TEX - 1.0)
        p1 = (bx + lean * 0.4, TEX * 0.55)
        p2 = (bx + lean, tip_y)
        segs = 9
        left, right = [], []
        prev = p0
        for i in range(segs + 1):
            t = i / segs
            x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t ** 2 * p2[0]
            y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t ** 2 * p2[1]
            # tangent for the ribbon normal
            dx, dy = x - prev[0], y - prev[1]
            ln = math.hypot(dx, dy) or 1.0
            nx, ny = -dy / ln, dx / ln
            w = (w0 * (1.0 - t) + 0.8 * t) * 0.5
            left.append((x - nx * w, y - ny * w))
            right.append((x + nx * w, y + ny * w))
            prev = (x, y)
            c = tuple(int(root_c[k] + (tip_c[k] - root_c[k]) * t) for k in range(3))
            if i > 0:
                # segment quad (accumulates coverage; alpha 255)
                drw.polygon([left[-2], right[-2], right[-1], left[-1]],
                            fill=c + (255,))
        # sharpen the tip
        drw.polygon([left[-2], right[-2], p2], fill=tip_c + (255,))
    img.save(path)


# ---------------------------------------------------------------- geometry

def card_mesh(cards, w, h, phases):
    """cards = number of vertical quads star-arranged around +Y.
    Returns flat arrays (positions, normals, uvs, colors, indices)."""
    pos, nrm, uv, col, idx = [], [], [], [], []
    for k in range(cards):
        theta = math.pi * k / cards
        c, s = math.cos(theta), math.sin(theta)
        base = len(pos) // 3
        for (x, y) in [(-w / 2, 0.0), (w / 2, 0.0), (w / 2, h), (-w / 2, h)]:
            pos += [x * c, y, -x * s]
            nrm += [0.0, 1.0, 0.0]          # up: grass lights like terrain
            uv += [x / w + 0.5, 1.0 - y / h]
            sway = y / h                     # 0 at root, 1 at tip
            col += [sway, 0.15 * sway, phases[k % len(phases)], 1.0]
        idx += [base + 0, base + 1, base + 2, base + 0, base + 2, base + 3]
    return pos, nrm, uv, col, idx


def billboard_quad(w, h):
    """The BILLBOARD shader convention: local x across [-w/2,w/2],
    y up [0,h], z = 0 (absolute meters; the instance/node scale is 1)."""
    pos = [-w / 2, 0, 0, w / 2, 0, 0, w / 2, h, 0, -w / 2, h, 0]
    nrm = [0, 1, 0] * 4
    uv = [0, 1, 1, 1, 1, 0, 0, 0]
    col = []
    for (x, y) in [(-w / 2, 0), (w / 2, 0), (w / 2, h), (-w / 2, h)]:
        sway = y / h
        col += [sway, 0.15 * sway, 0.0, 1.0]
    return pos, nrm, uv, col, [0, 1, 2, 0, 2, 3]


# ---------------------------------------------------------------- glb I/O

def pack_glb(path, meshes, images, extras_kraut):
    """meshes: list of (name, pos, nrm, uv, col, idx, texture_index)."""
    blob = bytearray()
    accessors, views, gltf_meshes = [], [], []

    def push(data, target, ctype, count, vmin=None, vmax=None):
        off = len(blob)
        blob.extend(data)
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data)})
        acc = {"bufferView": len(views) - 1, "componentType": ctype,
               "count": count, "type": target}
        if vmin is not None:
            acc["min"], acc["max"] = vmin, vmax
        accessors.append(acc)
        return len(accessors) - 1

    def push_floats(vals, target, vmin=None, vmax=None):
        return push(struct.pack("<%df" % len(vals), *vals), target, 5126,
                    len(vals) // {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[target],
                    vmin, vmax)

    for name, pos, nrm, uv, col, idx, tex in meshes:
        xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
        a_pos = push_floats(pos, "VEC3", [min(xs), min(ys), min(zs)],
                            [max(xs), max(ys), max(zs)])
        a_nrm = push_floats(nrm, "VEC3")
        a_uv = push_floats(uv, "VEC2")
        a_col = push_floats(col, "VEC4")
        a_idx = push(struct.pack("<%dI" % len(idx), *idx), "SCALAR", 5125, len(idx))
        gltf_meshes.append({
            "name": name,
            "primitives": [{
                "attributes": {"POSITION": a_pos, "NORMAL": a_nrm,
                               "TEXCOORD_0": a_uv, "COLOR_0": a_col},
                "indices": a_idx,
                "material": tex,
            }],
        })

    materials = [{
        "name": "GrassClump",
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                 "metallicFactor": 0.0, "roughnessFactor": 0.9},
        "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True,
    }, {
        "name": "GrassClump_Billboard",
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 1},
                                 "metallicFactor": 0.0, "roughnessFactor": 0.9},
        "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True,
    }]

    doc = {
        "asset": {"version": "2.0", "generator": "gen_grass_assets.py",
                  "extras": {"kraut": extras_kraut}},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(meshes)))}],
        "nodes": [{"name": m[0], "mesh": i} for i, m in enumerate(meshes)],
        "meshes": gltf_meshes,
        "materials": materials,
        "textures": [{"source": i} for i in range(len(images))],
        "images": [{"uri": u} for u in images],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    js = json.dumps(doc, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack("<II", len(blob), 0x004E4942))
        f.write(blob)


# ---------------------------------------------------------------- main

def main():
    seed = 42
    if "--seed" in sys.argv:
        seed = int(sys.argv[sys.argv.index("--seed") + 1])
    os.makedirs(OUT_DIR, exist_ok=True)

    tex_d = os.path.join(OUT_DIR, "GrassClump_D.png")
    tex_bb = os.path.join(OUT_DIR, "GrassClump_Billboard.png")
    draw_blades(tex_d, 40, seed)
    draw_blades(tex_bb, 64, seed + 1, width_scale=1.35)

    lod0 = card_mesh(3, CARD_W, CARD_H, phases=[0.0, 0.37, 0.71])
    lod1 = card_mesh(2, CARD_W, CARD_H, phases=[0.0, 0.55])
    bb = billboard_quad(CARD_W, CARD_H)

    # kraut-convention thresholds: coverage = radius / dist / tan(0.7854/2)
    # grass tiers switch NEAR: the clump is cheap, and the billboard tier
    # past 12 m only has to read until the 60 m cull distance cuts in
    radius = 0.5 * math.sqrt(CARD_W * CARD_W + CARD_H * CARD_H + CARD_W * CARD_W)
    t1 = radius / 5.0 / 0.41421356          # LOD1 from 5 m
    t2 = radius / 12.0 / 0.41421356         # billboard from 12 m
    extras = {
        "seed": seed, "descriptor": "gen_grass_assets.py",
        "reference_fov": 0.7854,
        "lod_thresholds": [round(t1, 6), round(t2, 6)],
        "billboard": {"atlas_cols": 1, "atlas_rows": 1, "mode": "cylindrical",
                      "texture": "GrassClump_Billboard.png",
                      "quad_width": CARD_W, "quad_height": CARD_H,
                      # grass cards all have +Y normals; the billboard tier
                      # must match or it darkens at distance (trees: 0.28)
                      "up_bias": 1.0},
        "wind": {"encoding": "r=sway,g=flutter,b=phase,a=variation"},
    }

    glb = os.path.join(OUT_DIR, "GrassClump.glb")
    pack_glb(glb, [
        ("GrassClump_LOD0", *lod0, 0),
        ("GrassClump_LOD1", *lod1, 0),
        ("GrassClump_Billboard", *bb, 1),
    ], ["GrassClump_D.png", "GrassClump_Billboard.png"], extras)
    print("gen_grass_assets: wrote", glb)

    # import through the stock kraut path (applies thresholds, billboard
    # flag, foliage material flags, m -> cm scaling)
    bin_path = os.path.join(OUT_DIR, "GrassClump.bin")
    r = subprocess.run([FURY, "kraut", "import", glb, bin_path],
                       cwd=os.path.join(REPO, "examples"),
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print(r.stderr, file=sys.stderr)
        sys.exit("gen_grass_assets: fury kraut import failed")
    print("gen_grass_assets: baked", bin_path)


if __name__ == "__main__":
    main()
