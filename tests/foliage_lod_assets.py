#!/usr/bin/env python3
"""Validate the baked Kraut shading contract and reject broken atlas imports."""
import argparse
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import lz4.block
import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
EXAMPLES = REPO / "examples"


def read_glb(path):
    data = path.read_bytes()
    size = struct.unpack_from("<I", data, 12)[0]
    return json.loads(data[20:20 + size]), data[28 + size:]


def read_scene(path):
    data = path.read_bytes()
    size, compressed = struct.unpack_from(">II", data)
    return json.loads(lz4.block.decompress(data[8:8 + compressed], uncompressed_size=size))


def attribute(model, binary, index):
    accessor = model["accessors"][index]
    view = model["bufferViews"][accessor["bufferView"]]
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    dtype = {5126: "<f4", 5125: "<u4"}[accessor["componentType"]]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    return np.frombuffer(binary, dtype=dtype, count=accessor["count"] * components,
                         offset=offset).reshape(-1, components)


def validate_tree(name):
    folder = EXAMPLES / "Resource" / "Trees" / name
    model, binary = read_glb(folder / (name + ".glb"))
    scene = read_scene(folder / (name + ".bin"))
    contract = model["asset"]["extras"]["kraut"]
    billboard = contract["billboard"]
    assert contract["canopy_normals"] == "crown_dome_v1"
    assert billboard["normal_space"] == "object" and billboard["albedo"] == "unlit"
    thresholds = contract["lod_thresholds"]
    assert thresholds[0] > 1.0, "authored near-distance thresholds must remain unsaturated"
    assert thresholds == sorted(thresholds, reverse=True)

    albedo = np.asarray(Image.open(folder / billboard["texture"]).convert("RGBA"))
    normals = np.asarray(Image.open(folder / billboard["normal_texture"]).convert("RGBA"))
    assert albedo.shape == normals.shape
    assert np.array_equal(albedo[..., 3], normals[..., 3]), "atlas silhouettes differ"
    opaque = normals[..., 3] > 127
    decoded = normals[..., :3][opaque].astype(float) / 127.5 - 1.0
    assert np.max(np.abs(np.linalg.norm(decoded, axis=1) - 1.0)) < 0.015
    assert np.std(decoded[:, 0]) > 0.1 and np.std(decoded[:, 2]) > 0.1, "normal atlas is flat"

    material = next(m for m in scene["materials"] if m["name"] == name + "_billboard")
    uniforms = {u["key"]: u["data"] for u in material["uniforms"]}
    assert uniforms["u_billboard_normal_atlas"] == [1.0]
    assert not material["pre_z"] and not material["wind_enabled"]
    normal = next(t for t in material["textures"] if t["key"] == "normal_texture")
    assert not normal["srgb"] and normal["path"] == billboard["normal_texture"]
    assert any(t["name"] == normal["name"] for t in scene["textures"]), "normal atlas is not registered"
    for texture in scene["textures"]:
        assert not Path(texture["path"]).is_absolute(), "sample contains a machine-local texture path"

    for mesh in model["meshes"]:
        if "_Billboard" in mesh["name"]:
            continue
        imported = next(m for m in scene["meshes"] if m["name"] == mesh["name"])
        authored_vertices = []
        for primitive in mesh["primitives"]:
            attrs = primitive["attributes"]
            points = attribute(model, binary, attrs["POSITION"])
            normal = attribute(model, binary, attrs["NORMAL"])
            authored_vertices.append(np.concatenate([points, normal], axis=1))
            assert np.all(np.isfinite(normal)) and np.min(np.linalg.norm(normal, axis=1)) > 0.99
            material = model["materials"][primitive["material"]]
            if material.get("alphaMode") == "MASK":
                texture_index = material["pbrMetallicRoughness"]["baseColorTexture"]["index"]
                uri = model["images"][model["textures"][texture_index]["source"]]["uri"]
                texture = np.asarray(Image.open(folder / uri).convert("RGBA"))
                assert np.any(texture[..., 3] >= 102), "foliage texture is fully transparent"
                indices = attribute(model, binary, primitive["indices"]).reshape(-1, 3)
                triangles = points[indices]
                areas = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                                triangles[:, 2] - triangles[:, 0]), axis=1)
                assert np.mean(areas > 1e-10) > 0.99, "foliage contains collapsed cards"
        imported_vertices = np.concatenate([np.asarray(imported["positions"]).reshape(-1, 3),
                                             np.asarray(imported["normals"]).reshape(-1, 3)], axis=1)
        expected = np.unique(np.round(np.concatenate(authored_vertices).astype(float), 6), axis=0)
        actual = np.unique(np.round(imported_vertices, 6), axis=0)
        np.testing.assert_allclose(actual, expected, atol=1e-6, rtol=0,
                                   err_msg="import changed authored canopy normals")
    print(name + ": atlas, normals, leaf geometry, thresholds and portable paths OK")
    return folder, model, binary


def validate_rejections(cli, folder, model, binary):
    with tempfile.TemporaryDirectory(prefix="fury-foliage-import-") as directory:
        for broken in ("missing_atlas", "wrong_space", "unknown_model"):
            document = copy.deepcopy(model)
            for image in document["images"]:
                image["uri"] = str(folder / image["uri"])
            billboard = document["asset"]["extras"]["kraut"]["billboard"]
            billboard["normal_texture"] = str(folder / model["asset"]["extras"]["kraut"]["billboard"]["normal_texture"])
            if broken == "missing_atlas":
                document["images"][-1]["uri"] = str(Path(directory) / "missing.png")
                billboard["normal_texture"] = document["images"][-1]["uri"]
            elif broken == "wrong_space":
                billboard["normal_space"] = "tangent"
            else:
                document["asset"]["extras"]["kraut"]["canopy_normals"] = "unknown"
            encoded = json.dumps(document).encode("utf-8")
            encoded += b" " * (-len(encoded) % 4)
            blob = struct.pack("<III", 0x46546c67, 2, 28 + len(encoded) + len(binary))
            blob += struct.pack("<II", len(encoded), 0x4e4f534a) + encoded
            blob += struct.pack("<II", len(binary), 0x004e4942) + binary
            source = Path(directory) / (broken + ".glb")
            source.write_bytes(blob)
            proc = subprocess.run([str(cli), "kraut", "import", str(source),
                                   str(Path(directory) / (broken + ".bin"))],
                                  cwd=EXAMPLES, capture_output=True, text=True, timeout=90)
            expected = "canopy normal model" if broken == "unknown_model" else "billboard normal atlas"
            assert proc.returncode != 0 and expected in proc.stdout + proc.stderr, \
                broken + " was not rejected with an atlas error"
            print(broken + ": correctly rejected")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, default=Path(os.environ.get("FURY_CLI_BIN", EXAMPLES / "furye-cli")))
    args = parser.parse_args()
    palm = validate_tree("PalmTree2")
    validate_tree("Tree1")
    validate_rejections(args.cli.resolve(), *palm)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
