#!/usr/bin/env python3
"""tools/gen_island_grass.py - compute splat-aware grass placement for
ocean_island (change: add-kraut-vegetation, task 10.6).

Reads the island's heightmap + splatmap (TerrainIsland/height.r16,
splat.png -- both 513x513 over a 102400cm square, heightScale 4000) and
writes examples/Projects/ocean/grass_points.lua: a flat {x,y,z,yaw,scale}
tuple list consumed by tests/lua/populate_island_grass.lua.

Accept rule: grass is the dominant splat channel (R of R=grass,G=rock,
B=mud,A=snow), slope < ~31 deg, height > 30cm (above the waterline).
Seeded and deterministic.
"""

import math
import os
import random
import struct
import sys

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TERRAIN_DIR = os.path.join(REPO, "examples", "Projects", "ocean", "TerrainIsland")
OUT_LUA = os.path.join(REPO, "examples", "Projects", "ocean", "grass_points.lua")

RES = 513
WORLD = 102400.0      # cm
HEIGHT_SCALE = 4000.0
TARGET = 15000
MAX_SLOPE = 0.6       # rise/run
MIN_HEIGHT = 30.0     # cm above y=0 (water)


def load_heights():
    with open(os.path.join(TERRAIN_DIR, "height.r16"), "rb") as f:
        raw = f.read()
    assert len(raw) == RES * RES * 2, "unexpected heightmap size"
    vals = struct.unpack("<%dH" % (RES * RES), raw)
    return [v * (HEIGHT_SCALE / 65535.0) for v in vals]


def main():
    seed = 7
    if "--seed" in sys.argv:
        seed = int(sys.argv[sys.argv.index("--seed") + 1])
    target = TARGET
    if "--count" in sys.argv:
        target = int(sys.argv[sys.argv.index("--count") + 1])

    # grass textures as scene siblings (the island's texture convention:
    # material blocks embed bare filenames resolved against the scene's
    # working dir)
    import shutil
    ocean_dir = os.path.dirname(OUT_LUA)
    for tex in ("GrassClump_D.png", "GrassClump_Billboard.png"):
        shutil.copyfile(os.path.join(REPO, "examples", "Resource", "Trees", "Grass", tex),
                        os.path.join(ocean_dir, tex))

    heights = load_heights()
    splat = Image.open(os.path.join(TERRAIN_DIR, "splat.png")).convert("RGBA")
    assert splat.size == (RES, RES)
    sp = splat.load()

    def height_at(ix, iz):
        return heights[min(max(iz, 0), RES - 1) * RES + min(max(ix, 0), RES - 1)]

    def world_to_grid(x, z):
        return (x + WORLD * 0.5) / WORLD * (RES - 1), (z + WORLD * 0.5) / WORLD * (RES - 1)

    def bilerp_height(gx, gz):
        x0, z0 = int(gx), int(gz)
        tx, tz = gx - x0, gz - z0
        h00 = height_at(x0, z0)
        h10 = height_at(x0 + 1, z0)
        h01 = height_at(x0, z0 + 1)
        h11 = height_at(x0 + 1, z0 + 1)
        return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz

    rng = random.Random(seed)
    texel_cm = WORLD / (RES - 1)

    def accepts(ix, iz):
        r, g, b, a = sp[min(max(ix, 0), RES - 1), min(max(iz, 0), RES - 1)]
        return r > g and r > b and r > a and r > 90

    def accept_height_slope(gx, gz, ix, iz):
        h = bilerp_height(gx, gz)
        if h < MIN_HEIGHT:
            return None
        dhdx = (height_at(ix + 1, iz) - height_at(ix - 1, iz)) / (2 * texel_cm)
        dhdz = (height_at(ix, iz + 1) - height_at(ix, iz - 1)) / (2 * texel_cm)
        if math.hypot(dhdx, dhdz) > MAX_SLOPE:
            return None
        return h

    # Meadow-patch layout: grass clumps read sparse at island scale (the
    # grass splat covers ~1/4 of 1 km^2; even 45k clumps is < 0.2/m^2).
    # Real ground cover grows in patches -- so: find habitat seeds with a
    # coarse rejection pass, then fill dense gaussian blobs around them.
    habitat = []
    attempts = 0
    while len(habitat) < 600 and attempts < 600000:
        attempts += 1
        x = rng.uniform(-WORLD * 0.5, WORLD * 0.5)
        z = rng.uniform(-WORLD * 0.5, WORLD * 0.5)
        gx, gz = world_to_grid(x, z)
        ix, iz = int(round(gx)), int(round(gz))
        if not accepts(ix, iz):
            continue
        if accept_height_slope(gx, gz, ix, iz) is None:
            continue
        habitat.append((x, z))

    n_clusters = max(24, min(64, len(habitat) // 10))
    centers = rng.sample(habitat, n_clusters)

    # 60% dense blobs around the centers, 40% uniform lawn baseline so
    # every grass-splat area keeps ground cover (clusters alone left the
    # grove's grass strips bare)
    cluster_target = target * 3 // 5
    per_cluster = cluster_target // n_clusters

    points = []
    for (cx, cz) in centers:
        placed_here = 0
        tries = 0
        while placed_here < per_cluster and tries < per_cluster * 30:
            tries += 1
            # gaussian blob, sigma ~10 m
            x = rng.gauss(cx, 1000.0)
            z = rng.gauss(cz, 1000.0)
            gx, gz = world_to_grid(x, z)
            ix, iz = int(round(gx)), int(round(gz))
            if not accepts(ix, iz):
                continue
            h = accept_height_slope(gx, gz, ix, iz)
            if h is None:
                continue
            points.append((x, h - 2.0, z, rng.uniform(0, math.pi * 2),
                           rng.uniform(90.0, 150.0)))
            placed_here += 1

    lawn_target = target - len(points)
    placed = 0
    attempts = 0
    while placed < lawn_target and attempts < lawn_target * 100:
        attempts += 1
        x = rng.uniform(-WORLD * 0.5, WORLD * 0.5)
        z = rng.uniform(-WORLD * 0.5, WORLD * 0.5)
        gx, gz = world_to_grid(x, z)
        ix, iz = int(round(gx)), int(round(gz))
        if not accepts(ix, iz):
            continue
        h = accept_height_slope(gx, gz, ix, iz)
        if h is None:
            continue
        points.append((x, h - 2.0, z, rng.uniform(0, math.pi * 2),
                       rng.uniform(90.0, 150.0)))
        placed += 1

    with open(OUT_LUA, "w") as f:
        f.write("-- generated by tools/gen_island_grass.py (seed %d) -- do not edit\n" % seed)
        f.write("return {\n")
        for p in points:
            f.write("  %.1f, %.1f, %.1f, %.4f, %.1f,\n" % p)
        f.write("}\n")
    print("gen_island_grass: %d points (%d attempts) -> %s" % (len(points), attempts, OUT_LUA))


if __name__ == "__main__":
    main()
