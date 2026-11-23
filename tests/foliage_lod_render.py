#!/usr/bin/env python3
"""Check foliage lighting, pre-Z/cache parity, instancing and LOD boundaries."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess

import numpy as np
from PIL import Image

from foliage_lod_assets import EXAMPLES, REPO, read_scene


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=["opengl", "metal", "vulkan"], required=True)
    parser.add_argument("--binary", type=Path, default=Path(os.environ.get("FURY_BIN", EXAMPLES / "fury")))
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    out = (args.artifacts or REPO / "test-artifacts" / ("foliage-lod-" + args.backend)).resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = {}
    pipeline = json.loads((EXAMPLES / "Resource/Pipeline/DeferredLightingPBR.json").read_text())
    removed = {shader["name"] for shader in pipeline["shaders"]
               if shader["name"].startswith("prez_depth_alphatest_wind")}
    full_pipeline = None
    if args.backend == "opengl":
        # Foliage uses the light-volume path, including on GL 4.1 without clustered lighting.
        pipeline["shaders"] = [shader for shader in pipeline["shaders"]
                               if shader["name"] != "clustered_lighting_shader"]
        full_pipeline = out / "foliage_pipeline.json"
        full_pipeline.write_text(json.dumps(pipeline))

    def remove_depth_variants(value):
        if isinstance(value, list):
            return [remove_depth_variants(item) for item in value
                    if not (isinstance(item, str) and item in removed)
                    and not (isinstance(item, dict) and item.get("name") in removed)]
        if isinstance(value, dict):
            return {key: remove_depth_variants(item) for key, item in value.items()}
        return value

    partial_pipeline = out / "missing_depth_variant.json"
    partial_pipeline.write_text(json.dumps(remove_depth_variants(pipeline)))

    def capture(name, require_lighting=True, **values):
        env = dict(os.environ)
        for key in list(env):
            if key.startswith("FURY_FOLIAGE_"):
                del env[key]
        env.update(FURY_WIND_TIME="0", FURY_LOD_JITTER="0", FURY_RENDER_THREAD="0",
                   FURY_VEG_PREZ="0", FURY_DRAWCMD_CACHE="1")
        if full_pipeline:
            env["FURY_FOLIAGE_PIPELINE"] = str(full_pipeline)
        env.update({key: str(value) for key, value in values.items()})
        path = out / (name + ".png")
        with (out / (name + ".log")).open("w") as log:
            proc = subprocess.run([str(args.binary.resolve()), str(REPO / "tests/lua/scenes/foliage_lod_check.lua"),
                                   "--rhi=" + args.backend, "--screenshot", str(path),
                                   "--screenshot-frame", "8"], cwd=EXAMPLES, env=env,
                                  stdout=log, stderr=log, timeout=90)
        text = (out / (name + ".log")).read_text(errors="replace")
        assert proc.returncode == 0 and path.is_file(), name + ": capture failed"
        assert "[EROR]" not in text and "Lua callback error" not in text, name + ": renderer error"
        image = np.asarray(Image.open(path).convert("RGB")).astype(float)
        if require_lighting:
            assert np.count_nonzero(image.max(axis=2) > 8) > 100, name + ": empty render"
        else:
            assert image.max() > 0, name + ": empty render"
        return image, text

    def foliage(image):
        red, green, blue = np.moveaxis(image, 2, 0)
        mask = (green > red * 1.12) & (green > blue * 1.12) & (green > 8)
        assert mask.sum() > 100, "too few foliage pixels"
        luma = red * 0.2126 + green * 0.7152 + blue * 0.0722
        return float(luma[mask].mean())

    def parity(name, left, right):
        delta = np.abs(left - right)
        metrics = {"mean": float(delta.mean()), "fraction_over_8": float((delta.max(axis=2) > 8).mean())}
        assert metrics["mean"] < 0.25 and metrics["fraction_over_8"] < 0.005, name + ": " + str(metrics)
        results[name] = metrics
        print(name + ": OK " + str(metrics), flush=True)

    try:
        for tree, billboard in [("PalmTree2", 4), ("Tree1", 3)]:
            for index, (sun_yaw, sun_pitch, rotation, distance) in enumerate([
                    (-1.0, 0.5, 0.0, 2400), (-1.0, -1.0, 0.0, 2400),
                    (-1.0, 1.5, 1.1, 2400), (0.7, 0.35, 0.0, 2400),
                    (-1.0, 0.5, 0.0, 12000)]):
                common = dict(FURY_FOLIAGE_TREE=tree, FURY_FOLIAGE_SUN_YAW=sun_yaw,
                              FURY_FOLIAGE_SUN_PITCH=sun_pitch, FURY_FOLIAGE_YAW=rotation,
                              FURY_FOLIAGE_DISTANCE=distance)
                name = tree + "_lighting_" + str(index)
                mesh, _ = capture(name + "_mesh", FURY_FOLIAGE_TIER=0, **common)
                atlas, _ = capture(name + "_billboard", FURY_FOLIAGE_TIER=billboard, **common)
                mesh_luma, atlas_luma = foliage(mesh), foliage(atlas)
                ratio = atlas_luma / mesh_luma
                results[name] = {"mesh_luma": mesh_luma, "billboard_luma": atlas_luma, "ratio": ratio}
                assert abs(ratio - 1.0) < 0.18, name + ": lighting mismatch " + str(ratio)
                print(name + ": OK ratio=%.4f" % ratio, flush=True)

        for mode in ["plain", "instanced", "mixed"]:
            common = {"FURY_FOLIAGE_MODE": mode}
            if mode != "mixed":
                common["FURY_FOLIAGE_TIER"] = 0
            off, _ = capture(mode + "_prez_off", FURY_VEG_PREZ=0, **common)
            on, _ = capture(mode + "_prez_on", FURY_VEG_PREZ=1, **common)
            uncached, _ = capture(mode + "_uncached", FURY_VEG_PREZ=1, FURY_DRAWCMD_CACHE=0, **common)
            parity(mode + "_prez", off, on)
            parity(mode + "_cache", on, uncached)
            partial, _ = capture(mode + "_partial_depth", FURY_VEG_PREZ=1,
                                 FURY_FOLIAGE_PIPELINE=partial_pipeline, **common)
            parity(mode + "_partial_depth", off, partial)
            if mode == "mixed":
                assert np.count_nonzero(off[:, 800:].max(axis=2) > 8) > 50, "mixed scene has no distant billboard"
                threaded, _ = capture("mixed_threaded", FURY_VEG_PREZ=1, FURY_RENDER_THREAD=1, **common)
                parity("mixed_threaded", on, threaded)

        plain, _ = capture("billboard_plain", FURY_FOLIAGE_TIER=4, FURY_FOLIAGE_YAW=1.1)
        instanced, _ = capture("billboard_instanced", FURY_FOLIAGE_TIER=4,
                               FURY_FOLIAGE_YAW=1.1, FURY_FOLIAGE_MODE="instanced")
        parity("billboard_instancing", plain, instanced)
        divisor, _ = capture("billboard_divisor", FURY_FOLIAGE_TIER=4, FURY_INSTANCE_SSBO=0,
                             FURY_FOLIAGE_YAW=1.1, FURY_FOLIAGE_MODE="instanced")
        parity("billboard_instance_stream", instanced, divisor)
        ldr_mesh, _ = capture("ldr_mesh", FURY_FOLIAGE_TIER=0, FURY_FOLIAGE_HDR=0)
        ldr_atlas, _ = capture("ldr_billboard", FURY_FOLIAGE_TIER=4, FURY_FOLIAGE_HDR=0)
        assert abs(foliage(ldr_atlas) / foliage(ldr_mesh) - 1.0) < 0.18, "LDR billboard lighting mismatch"

        scene = read_scene(EXAMPLES / "Resource/Trees/PalmTree2/PalmTree2.bin")
        mesh = next(m for m in scene["meshes"] if m["name"] == "PalmTree2_LOD0")
        points = np.asarray(mesh["positions"]).reshape(-1, 3)
        radius = np.linalg.norm(points.max(axis=0) - points.min(axis=0)) * 50.0
        thresholds = mesh["lod_thresholds"]
        coverages = [thresholds[0] * 1.2] + [(a + b) * 0.5 for a, b in zip(thresholds, thresholds[1:])] + [thresholds[-1] * 0.75]
        for expected, coverage in enumerate(coverages):
            distance = radius / math.tan(0.7854 * 0.5) / coverage
            _, log = capture("natural_lod_" + str(expected), require_lighting=False,
                             FURY_FOLIAGE_DISTANCE=distance)
            match = re.search(r"FOLIAGE tier=(\d+)", log)
            assert match and int(match[1]) == expected, "natural LOD selection mismatch at " + str(distance)
        results["natural_lod"] = "all five tiers reachable"
    finally:
        (out / "metrics.json").write_text(json.dumps(results, indent=2) + "\n")
    print("foliage_lod_render: OK (" + args.backend + ")", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
