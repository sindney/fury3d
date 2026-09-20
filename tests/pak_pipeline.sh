#!/usr/bin/env bash
# tests/pak_pipeline.sh - end-to-end test for the cook/package/pak pipeline.
#
# Builds a tiny synthetic project in /tmp/paktest-<pid>, then exercises:
#   a. scene build (furye-cli exec-script + save) and load-back verify
#   b. cook: manifest, DDC tiered layout, BCn format, full mip chain
#   c. recook: DDC cache hits, 0 cooked
#   d. touch one texture: exactly 1 recooked, rest ddc-hit
#   e. package (lz4) + fury exec on the pak: textures served headless
#   f. negative: --no-cook with a missing DDC artifact fails, naming it
#   g. sibling auto-mount boundary (see SKIP note inside)
#   h. --compression none: every pak index entry stored uncompressed
#
# Usage (from anywhere):
#   tests/pak_pipeline.sh
#
# Requires the built binaries at examples/fury and examples/furye-cli and
# python3. Cleans up its /tmp directory on exit.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXAMPLES="$REPO_ROOT/examples"
FURY="$EXAMPLES/fury"
FURYCLI="$EXAMPLES/furye-cli"
LUA_TEST="$REPO_ROOT/tests/lua/pak_scene_load.lua"

[ -x "$FURY" ] || { echo "pak_pipeline FAIL: $FURY not found (build first)" >&2; exit 1; }
[ -x "$FURYCLI" ] || { echo "pak_pipeline FAIL: $FURYCLI not found (build first)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "pak_pipeline FAIL: python3 not found" >&2; exit 1; }

TMP="/tmp/paktest-$$"
PROJ="$TMP/proj"
DDC="$TMP/ddc"
LOGS="$TMP/logs"
mkdir -p "$PROJ" "$DDC" "$LOGS"

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

die() { echo "pak_pipeline FAIL: $1" >&2; exit 1; }
stage() { echo "pak_pipeline: [$1] $2: PASS"; }

# ---------------------------------------------------------------------------
# a. synthetic scene: three small textures + a cube node per texture
# ---------------------------------------------------------------------------
# Sky/ ships exactly two small PNGs; the third comes from the ocean project's
# terrain set. Textures bind via Material.SetTexture (slot key = file name so
# the EM entry's GetName == GetFilePath, the invariant pak_scene_load.lua
# looks up).
cp "$EXAMPLES/Resource/Texture/Sky/cloud_noise.png" "$PROJ/sky_cloud.png"
cp "$EXAMPLES/Resource/Texture/Sky/moon.png" "$PROJ/sky_moon.png"
cp "$EXAMPLES/Projects/ocean/rock.png" "$PROJ/terrain_rock.png"

cat > "$TMP/build_scene.lua" <<'LUA'
-- build_scene.lua: tiny scene, one cube node per texture, save to arg[1].
local out = arg and arg[1]
if not out or out == "" then
    io.stderr:write("build_scene FAIL: expected output path as arg[1]\n")
    os.exit(1)
end

local scene = Scene.Create("paktest", "")
Scene.SetActive(scene)
local root = scene:GetRootNode()

local cube = MeshUtil.CreateCube()
scene:AddMesh(cube)

for _, name in ipairs({ "sky_cloud.png", "sky_moon.png", "terrain_rock.png" }) do
    local mat = Material.Create("mat_" .. name)
    mat:SetTexture(name, name) -- slot key = file name -> EM name == path
    scene:AddMaterial(mat)

    local node = SceneNode.Create("node_" .. name)
    node:SetLocalPosition(0.0, 0.0, 0.0)
    node:AddComponent(MeshRender.Create(mat, cube))
    root:AddChild(node)
end

if not FileUtil.SaveFile(scene, out) then
    io.stderr:write("build_scene FAIL: SaveFile failed\n")
    os.exit(1)
end
print("build_scene: saved " .. out)
os.exit(0)
LUA

if ! (cd "$PROJ" && "$FURYCLI" exec-script "$TMP/build_scene.lua" "$PROJ/scene.json" \
        > "$LOGS/a_build.log" 2>&1); then
    cat "$LOGS/a_build.log" >&2; die "stage a: scene build failed"
fi
[ -f "$PROJ/scene.json" ] || die "stage a: scene.json not written"
# load it back: 3 textures, 4 nodes (root + 3), known texture resolvable
if ! (cd "$PROJ" && "$FURY" exec scene.json "$LUA_TEST" 3 4 sky_moon.png \
        > "$LOGS/a_verify.log" 2>&1); then
    cat "$LOGS/a_verify.log" >&2; die "stage a: load-back verify failed"
fi
grep -q "pak_scene_load: PASS" "$LOGS/a_verify.log" \
    || die "stage a: verify PASS line missing"
stage "a" "build synthetic scene (3 textures, 3 cube nodes) and load back"

# ---------------------------------------------------------------------------
# b. cook: exit 0, manifest shape, DDC layout, BCn, mip chain
# ---------------------------------------------------------------------------
if ! (cd "$PROJ" && "$FURYCLI" cook scene.json --texture-target legacy --ddc "$DDC" \
        > "$LOGS/b_cook.log" 2>&1); then
    cat "$LOGS/b_cook.log" >&2; die "stage b: cook failed"
fi
MANIFEST="$PROJ/scene.cookmanifest.json"
[ -f "$MANIFEST" ] || die "stage b: manifest not written"
python3 - "$MANIFEST" "$DDC" <<'PY'
import json, math, os, struct, sys
manifest, ddc_root = sys.argv[1], sys.argv[2]
m = json.load(open(manifest))
tex = m.get("textures", [])
assert len(tex) == 3, "expected 3 textures, got %d" % len(tex)
for e in tex:
    for k in ("key", "ddc", "format", "width", "height", "mips"):
        assert k in e, "entry for %s lacks %s" % (e.get("key", "?"), k)
    assert e["format"].startswith("bc"), "format not BCn: " + e["format"]
    assert e["width"] > 0 and e["height"] > 0, "non-positive dims for " + e["key"]
    expect = int(math.floor(math.log2(max(e["width"], e["height"])))) + 1
    assert e["mips"] == expect, "%s: mips %d != full chain %d" % (e["key"], e["mips"], expect)
    parts = e["ddc"].split("/")
    assert len(parts) == 3 and len(parts[0]) == 2 and len(parts[1]) == 2 \
        and parts[2].endswith(".ktx2"), "ddc not tiered ab/cd/: " + e["ddc"]
    path = os.path.join(ddc_root, *parts)
    assert os.path.isfile(path), "missing ddc artifact " + path
    hdr = open(path, "rb").read(48)
    ktx2_id = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])
    assert hdr[:12] == ktx2_id, "bad KTX2 identifier in " + path
    vk_format = struct.unpack_from("<I", hdr, 12)[0]
    w = struct.unpack_from("<I", hdr, 20)[0]
    h = struct.unpack_from("<I", hdr, 24)[0]
    levels = struct.unpack_from("<I", hdr, 40)[0]
    assert vk_format != 0, "vkFormat 0 in " + path
    assert (w, h) == (e["width"], e["height"]), \
        "%s: ktx2 %dx%d != manifest %dx%d" % (path, w, h, e["width"], e["height"])
    assert levels == e["mips"], "%s: ktx2 levels %d != manifest mips %d" % (path, levels, e["mips"])
print("manifest OK: 3 textures, ddc tiered, BCn, full mip chain, ktx2 dims match")
PY
stage "b" "cook: manifest + DDC layout + BCn + mips"

# ---------------------------------------------------------------------------
# c. recook: everything served from the DDC
# ---------------------------------------------------------------------------
if ! (cd "$PROJ" && "$FURYCLI" cook scene.json --texture-target legacy --ddc "$DDC" \
        > "$LOGS/c_recook.log" 2>&1); then
    cat "$LOGS/c_recook.log" >&2; die "stage c: recook failed"
fi
grep -q "0 cooked, 3 ddc-hits" "$LOGS/c_recook.log" \
    || die "stage c: expected '0 cooked, 3 ddc-hits', got: $(grep 'done:' "$LOGS/c_recook.log" || true)"
stage "c" "recook: 0 cooked, 3 ddc-hits"

# ---------------------------------------------------------------------------
# d. touch one texture: exactly one recook
# ---------------------------------------------------------------------------
printf 'X' >> "$PROJ/sky_moon.png" # content hash changes; ktx still ingests it
if ! (cd "$PROJ" && "$FURYCLI" cook scene.json --texture-target legacy --ddc "$DDC" \
        > "$LOGS/d_touch.log" 2>&1); then
    cat "$LOGS/d_touch.log" >&2; die "stage d: recook after touch failed"
fi
grep -q "1 cooked, 2 ddc-hits" "$LOGS/d_touch.log" \
    || die "stage d: expected '1 cooked, 2 ddc-hits', got: $(grep 'done:' "$LOGS/d_touch.log" || true)"
grep -q "cooked sky_moon.png" "$LOGS/d_touch.log" \
    || die "stage d: recooked texture was not sky_moon.png"
stage "d" "touch sky_moon.png: exactly 1 recooked, 2 ddc-hits"

# ---------------------------------------------------------------------------
# e. package (lz4 default) + headless exec on the pak
# ---------------------------------------------------------------------------
if ! (cd "$PROJ" && "$FURYCLI" package scene.json --texture-target legacy --ddc "$DDC" \
        > "$LOGS/e_package.log" 2>&1); then
    cat "$LOGS/e_package.log" >&2; die "stage e: package failed"
fi
[ -f "$PROJ/scene.pak" ] || die "stage e: scene.pak not written"
if ! (cd "$PROJ" && "$FURY" exec scene.pak "$LUA_TEST" 3 4 sky_moon.png \
        > "$LOGS/e_exec.log" 2>&1); then
    cat "$LOGS/e_exec.log" >&2; die "stage e: exec on pak failed"
fi
grep -q "pak_scene_load: PASS" "$LOGS/e_exec.log" \
    || die "stage e: pak PASS line missing"
if grep -q "Failed to load image\|EROR" "$LOGS/e_exec.log"; then
    grep "Failed to load image\|EROR" "$LOGS/e_exec.log" >&2
    die "stage e: pak served textures failed to decode"
fi
stage "e" "package + exec on pak: 3 textures decoded from KTX2, 4 nodes"

# ---------------------------------------------------------------------------
# f. negative: --no-cook with a deleted DDC artifact
# ---------------------------------------------------------------------------
REMOVED_DDC="$(python3 -c "import json;print(json.load(open('$MANIFEST'))['textures'][0]['ddc'])")"
cp "$DDC/$REMOVED_DDC" "$TMP/removed.ktx2"
rm "$DDC/$REMOVED_DDC"
set +e
(cd "$PROJ" && "$FURYCLI" package scene.json --no-cook --ddc "$DDC" \
        --output "$TMP/neg.pak") > "$LOGS/f_negative.log" 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] || die "stage f: package --no-cook succeeded with a missing DDC artifact"
grep -q "artifact missing" "$LOGS/f_negative.log" \
    || { cat "$LOGS/f_negative.log" >&2; die "stage f: stderr does not name the missing artifact"; }
grep -q "$REMOVED_DDC" "$LOGS/f_negative.log" \
    || die "stage f: stderr does not name the removed ddc path"
mv "$TMP/removed.ktx2" "$DDC/$REMOVED_DDC" # restore for stage h
stage "f" "--no-cook with missing DDC artifact: exit $rc, artifact named"

# ---------------------------------------------------------------------------
# g. sibling auto-mount
# ---------------------------------------------------------------------------
# Sibling auto-mount is a fury-launcher behavior (examples/main.cpp prints
# "fury: mounted sibling pak ..." only on the windowed path); `fury exec`
# mounts a pak given directly and must NOT mount siblings. The launcher path
# itself was verified manually: loose scene.bin + sibling .pak (no loose
# textures) renders a correct screenshot, with Player.lua read from the pak.
# Here we only pin the exec boundary: no mount line, scene loads from disk.
if ! (cd "$PROJ" && "$FURY" exec scene.json "$LUA_TEST" 3 4 sky_moon.png \
        > "$LOGS/g_exec.log" 2>&1); then
    cat "$LOGS/g_exec.log" >&2; die "stage g: loose exec failed"
fi
if grep -q "mounted sibling pak" "$LOGS/g_exec.log"; then
    die "stage g: exec unexpectedly mounted a sibling pak"
fi
echo "pak_pipeline: [g] sibling auto-mount: SKIP (launcher-only; manually verified)"
stage "g" "exec boundary: no sibling mount, loose scene loads"

# ---------------------------------------------------------------------------
# h. --compression none: every entry stored uncompressed
# ---------------------------------------------------------------------------
if ! (cd "$PROJ" && "$FURYCLI" package scene.json --texture-target legacy --ddc "$DDC" \
        --compression none --output "$TMP/none.pak") > "$LOGS/h_package.log" 2>&1; then
    cat "$LOGS/h_package.log" >&2; die "stage h: package --compression none failed"
fi
[ -f "$TMP/none.pak" ] || die "stage h: none.pak not written"
python3 - "$TMP/none.pak" <<'PY'
import hashlib, struct, sys
path = sys.argv[1]
data = open(path, "rb").read()
assert len(data) >= 52, "pak too small for a footer"
magic, version, idx_off, idx_size, idx_sha, _ = struct.unpack_from("<8sIQQ20sI", data, len(data) - 52)
assert magic == b"FURYPAK1", "bad magic %r" % magic
assert version == 1, "bad version %d" % version
idx = data[idx_off:idx_off + idx_size]
assert hashlib.sha1(idx).digest() == idx_sha, "index sha1 mismatch"
(count,) = struct.unpack_from("<I", idx, 0)
pos = 4
entries = {}
for _ in range(count):
    (klen,) = struct.unpack_from("<I", idx, pos); pos += 4
    key = idx[pos:pos + klen].decode("utf-8"); pos += klen
    _off, _usize = struct.unpack_from("<QQ", idx, pos); pos += 16
    codec, _bsize, nblocks = struct.unpack_from("<III", idx, pos); pos += 12
    pos += 4 * nblocks + 20
    entries[key] = (codec, nblocks)
(blen,) = struct.unpack_from("<I", idx, pos); pos += 4
boot = idx[pos:pos + blen].decode("utf-8"); pos += blen
assert pos == idx_size, "trailing bytes in index"
expect = {"scene.json", "sky_cloud.png", "sky_moon.png", "terrain_rock.png"}
assert set(entries) == expect, "entry keys %s != %s" % (sorted(entries), sorted(expect))
assert boot == "scene.json", "boot key is %r" % boot
for key, (codec, nblocks) in entries.items():
    assert codec == 0 and nblocks == 0, "%s not stored uncompressed" % key
print("none.pak OK: 4 entries, boot scene.json, all codec=0 (stored)")
PY
stage "h" "--compression none: footer/index parse, all entries stored"

echo "pak_pipeline: PASS"
