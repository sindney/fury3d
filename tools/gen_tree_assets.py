#!/usr/bin/env python3
"""Tree asset baker (add-kraut-vegetation, task 8.2).

Bakes the committed sample trees from the checked-in Kraut descriptors:

  Descriptors/<name>.tree --(fury kraut generate)--> glb + textures + atlas + previews
                        --(fury kraut import)----> <name>.bin fragment
                        --> trees.json index

Output layout (all committed; the samples work without the Kraut submodule):

  examples/Resource/Trees/<name>/<name>.glb         export (re-importable)
  examples/Resource/Trees/<name>/<name>.bin         engine scene fragment
  examples/Resource/Trees/<name>/*.tga|.png         textures (bare siblings of the .bin)
  examples/Resource/Trees/<name>/previews/*.png     per-tier previews
  examples/Resource/Trees/trees.json                index

Deterministic per descriptor + seed (KrautCLI glb is byte-identical).

Usage:
  python3 tools/gen_tree_assets.py            # regenerate all
  python3 tools/gen_tree_assets.py --check    # verify committed assets are current
"""

import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageFile

ImageFile.LOAD_TRUNCATED_IMAGES = True  # some Kraut TGAs trip PIL's strict RLE reader

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXAMPLES = os.path.join(REPO, "examples")
FURY = os.path.join(EXAMPLES, "fury")
TREES_ROOT = os.path.join(EXAMPLES, "Resource", "Trees")
DESCRIPTORS = os.path.join(TREES_ROOT, "Descriptors")

# Foliage tone-map: kraut content textures are bright lime (Palm1 mean rgb
# (156,186,48)); scale toward deep palm green. Applied to the leaf textures
# AND the billboard atlas (it's baked from the same texture) so the terminal
# tier stays color-consistent with the mesh tiers. Override via
# FURY_FOLIAGE_TONE="r,g,b" (e.g. "1,1,1" to disable).
def _parse_tone():
    raw = os.environ.get("FURY_FOLIAGE_TONE")
    if raw:
        parts = [float(x) for x in raw.split(",")]
        if len(parts) == 3:
            return tuple(parts)
    return (0.55, 0.62, 0.85)
FOLIAGE_TONE = _parse_tone()
FOLIAGE_KEYWORDS = ("frond", "leaf", "leaves", "needle", "foliage")

# name -> seed. One palm + one broadleaf (spec kraut-tree-import).
TREES = [
    ("PalmTree2", 7),
    ("Tree1", 42),
]


def run(args, cwd):
    proc = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"gen_tree_assets: command failed ({proc.returncode}): {' '.join(args)}")
    return proc.stdout


def tone_map_foliage(out_dir, name, glb_path):
    """Darken leaf textures + the billboard atlas toward deep green
    (FOLIAGE_TONE), in place. Foliage textures are identified by material
    name in the glb (frond/leaf/needle/...)."""
    with open(glb_path, "rb") as f:
        data = f.read()
    jlen = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20:20 + jlen])

    targets = set()
    images = gltf.get("images", [])
    for mat in gltf.get("materials", []):
        mname = mat.get("name", "").lower()
        if not any(k in mname for k in FOLIAGE_KEYWORDS):
            continue
        tex_idx = mat.get("pbrMetallicRoughness", {}).get(
            "baseColorTexture", {}).get("index")
        if tex_idx is None:
            continue
        src = gltf["textures"][tex_idx].get("source")
        if src is not None and "uri" in images[src]:
            targets.add(images[src]["uri"])
    targets.add(f"{name}_BillboardAtlas.png")

    for uri in targets:
        path = os.path.join(out_dir, uri)
        if not os.path.isfile(path):
            continue
        im = Image.open(path).convert("RGBA")
        lut = []
        for c in range(3):
            lut += [min(255, int(v * FOLIAGE_TONE[c])) for v in range(256)]
        lut += list(range(256))  # alpha untouched
        im = im.point(lut)
        im.save(path)
    print(f"gen_tree_assets: tone-mapped {len(targets)} foliage textures for {name}")


def bake(name, seed, trees_root):
    out_dir = os.path.join(trees_root, name)
    descriptor = os.path.join(DESCRIPTORS, name + ".tree")
    if not os.path.isfile(descriptor):
        raise SystemExit(f"gen_tree_assets: descriptor missing: {descriptor}")

    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    # 1. generate: glb + textures + billboard atlas + previews
    run([FURY, "kraut", "generate", descriptor, "--seed", str(seed), "--out", out_dir],
        cwd=EXAMPLES)

    glb = os.path.join(out_dir, name + ".glb")
    if not os.path.isfile(glb):
        raise SystemExit(f"gen_tree_assets: no glb produced for {name}")

    # 1.5 tone-map foliage textures + atlas toward deep green
    tone_map_foliage(out_dir, name, glb)

    # previews -> previews/ subdir
    previews = os.path.join(out_dir, "previews")
    os.makedirs(previews)
    for f in os.listdir(out_dir):
        if "_tier" in f and f.endswith(".png"):
            shutil.move(os.path.join(out_dir, f), os.path.join(previews, f))

    # 2. import: glb -> engine scene fragment (textures stay bare siblings of
    # the .bin -- the engine's scene-save relocation convention)
    bin_path = os.path.join(out_dir, name + ".bin")
    run([FURY, "kraut", "import", glb, bin_path], cwd=EXAMPLES)

    # 3. validate: fury info must read the fragment and see the mesh chain
    info = run([FURY, "info", bin_path], cwd=EXAMPLES)
    if "meshes:" not in info:
        raise SystemExit(f"gen_tree_assets: info failed for {bin_path}")

    size_kb = os.path.getsize(bin_path) // 1024
    return {
        "name": name,
        "seed": seed,
        "fragment": f"Engine/Trees/{name}/{name}.bin",
        "glb": f"Engine/Trees/{name}/{name}.glb",
        "atlas": f"Engine/Trees/{name}/{name}_BillboardAtlas.png",
        "sizeKB": size_kb,
    }


def _norm_scene_json(path):
    """Parse a .bin (LZ4-JSON) and normalize volatile bits (uuids, and the
    uuid-keyed entity array order -- registration order is per-run) so two
    bakes of the same descriptor compare equal."""
    import lz4.block  # noqa: F401  (resolved below if missing)
    import struct as _struct
    raw = open(path, "rb").read()
    src = _struct.unpack(">I", raw[:4])[0]
    comp = _struct.unpack(">I", raw[4:8])[0]
    doc = json.loads(data := lz4.block.decompress(raw[8:8 + comp], uncompressed_size=src))

    def strip(v):
        if isinstance(v, dict):
            return {k: strip(x) for k, x in v.items() if k != "uuid"}
        if isinstance(v, list):
            return [strip(x) for x in v]
        return v

    doc = strip(doc)
    # entity arrays are iterated in uuid-hash order at save -> sort by name
    for key in ("textures", "materials", "meshes", "animations", "particleSystems", "heightmaps"):
        if isinstance(doc.get(key), list):
            doc[key] = sorted(doc[key], key=lambda e: (e.get("name", ""), json.dumps(e, sort_keys=True)))
    return doc


def main():
    check_only = "--check" in sys.argv

    if not os.path.isfile(FURY):
        raise SystemExit(f"gen_tree_assets: {FURY} not found -- build the engine first "
                         "(the kraut_tools target builds the tool binaries)")

    if check_only:
        try:
            import lz4.block  # noqa: F401
        except ImportError:
            raise SystemExit("gen_tree_assets: --check needs the lz4 python package "
                             "(pip install lz4) to parse .bin fragments")
        with tempfile.TemporaryDirectory() as tmp:
            entries = [bake(name, seed, tmp) for name, seed in TREES]
            for name, _ in TREES:
                a = os.path.join(TREES_ROOT, name, name + ".bin")
                b = os.path.join(tmp, name, name + ".bin")
                if not os.path.isfile(a) or _norm_scene_json(a) != _norm_scene_json(b):
                    raise SystemExit(f"gen_tree_assets: {name}.bin is stale -- rerun tools/gen_tree_assets.py")
        print("gen_tree_assets: committed assets are current")
        return 0

    entries = [bake(name, seed, TREES_ROOT) for name, seed in TREES]
    index = os.path.join(TREES_ROOT, "trees.json")
    with open(index, "w") as f:
        json.dump({"trees": entries}, f, indent=1)
        f.write("\n")
    print(f"gen_tree_assets: baked {len(entries)} trees -> {TREES_ROOT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
