#!/usr/bin/env bash
# tests/pak_pipeline.sh - end-to-end test for the cook/package/pak pipeline.
#
# Builds a tiny synthetic project in /tmp/paktest-<pid>, then exercises:
#   a. scene build (furye-cli exec-script + save) and load-back verify
#   b. cook: textures + per-backend shader artifacts/default reflection;
#      --rhi=metal restricts the shader manifest
#   c. recook: texture and shader DDC cache hits, 0 cooked
#   d. touch one texture: exactly 1 recooked, rest ddc-hit
#   e. package (lz4) + fury exec on the pak: textures and postprocess JSON
#      served headless
#   f. negative: --no-cook with a missing DDC artifact fails, naming it
#   g. sibling auto-mount boundary (see SKIP note inside)
#   h. --compression none: every pak index entry stored uncompressed
#   i. pak round trip: unpack scene.pak + none.pak, byte-compare entries
#      against the scene + DDC sources, repack from extracted bytes with an
#      independent writer, re-mount the repacked pak from an isolated cwd
#      (loose files absent, so disk fallback cannot mask a bad entry), and
#      tripwire payload/index SHA-1 corruption
#
# Usage (from anywhere):
#   tests/pak_pipeline.sh
#
# Requires built binaries at examples/fury and examples/furye-cli (override
# with FURY_BIN/FURY_CLI_BIN for ASAN) and python3. Cleans up its /tmp dir.
# PAK_TEST_RHI=opengl,vulkan,metal forces full backend coverage on any host.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXAMPLES="$REPO_ROOT/examples"
FURY="${FURY_BIN:-$EXAMPLES/fury}"
FURYCLI="${FURY_CLI_BIN:-$EXAMPLES/furye-cli}"
RHI_ARGS=()
if [ -n "${PAK_TEST_RHI:-}" ]; then
    RHI_ARGS=(--rhi "$PAK_TEST_RHI")
fi
LUA_TEST="$REPO_ROOT/tests/lua/pak_scene_load.lua"

[ -x "$FURY" ] || { echo "pak_pipeline FAIL: $FURY not found (build first)" >&2; exit 1; }
[ -x "$FURYCLI" ] || { echo "pak_pipeline FAIL: $FURYCLI not found (build first)" >&2; exit 1; }
command -v python3 >/dev/null || { echo "pak_pipeline FAIL: python3 not found" >&2; exit 1; }

TMP="/tmp/paktest-$$"
PROJ="$TMP/proj"
DDC="$TMP/ddc"
LOGS="$TMP/logs"
mkdir -p "$PROJ" "$DDC" "$LOGS"

cleanup() {
    if [ "${PAK_TEST_KEEP:-0}" = "1" ]; then
        echo "pak_pipeline: kept artifacts in $TMP"
    else
        rm -rf "$TMP"
    fi
}
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
        "${RHI_ARGS[@]}" > "$LOGS/b_cook.log" 2>&1); then
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
shaders = m.get("shaders", [])
assert shaders, "no cooked shader artifacts"
assert {e["backend"] for e in shaders} == {"opengl", "vulkan", "metal"}
for e in shaders:
    assert e["key"] == "DDC/" + e["ddc"]
    path = os.path.join(ddc_root, *e["ddc"].split("/"))
    assert os.path.isfile(path), "missing shader artifact " + path
    if e["ddc"].endswith(".refl"):
        r = json.load(open(path))
        assert r["version"] == 2 and "vertex" in r and "fragment" in r
print("shader manifest OK: %d artifacts across opengl/vulkan/metal" % len(shaders))
found_default = False
for e in shaders:
    if e["backend"] == "metal" and e["shader"].endswith("/GBuffer.glsl") and e["ddc"].endswith(".refl"):
        r = json.load(open(os.path.join(ddc_root, *e["ddc"].split("/"))))
        for member in r["fragment"]["globals"]:
            if member["name"] == "ambient_factor":
                assert struct.unpack("<f", bytes(member["defaults"]))[0] == 1.0
                found_default = True
assert found_default, "GBuffer ambient_factor default missing from reflection"
PY
stage "b" "cook: manifest + DDC layout + BCn + mips"

if ! (cd "$PROJ" && "$FURYCLI" cook scene.json --texture-target legacy --ddc "$DDC" \
        --rhi=metal --manifest "$TMP/metal-only.manifest.json" > "$LOGS/b_metal_only.log" 2>&1); then
    cat "$LOGS/b_metal_only.log" >&2; die "stage b: Metal-only cook failed"
fi
python3 - "$TMP/metal-only.manifest.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
shaders = m.get("shaders", [])
assert shaders and {e["backend"] for e in shaders} == {"metal"}
assert all(e["ddc"].endswith((".metal", ".refl")) for e in shaders)
print("Metal-only manifest OK: %d artifacts" % len(shaders))
PY
stage "b2" "--rhi=metal restricts cooked shader targets"

# ---------------------------------------------------------------------------
# c. recook: everything served from the DDC
# ---------------------------------------------------------------------------
if ! (cd "$PROJ" && "$FURYCLI" cook scene.json --texture-target legacy --ddc "$DDC" \
        "${RHI_ARGS[@]}" > "$LOGS/c_recook.log" 2>&1); then
    cat "$LOGS/c_recook.log" >&2; die "stage c: recook failed"
fi
grep -q "0 textures cooked, 3 ddc-hits; 0 shader targets cooked" "$LOGS/c_recook.log" \
    || die "stage c: expected texture and shader DDC hits, got: $(grep 'done:' "$LOGS/c_recook.log" || true)"
stage "c" "recook: all texture and shader artifacts served from DDC"

# ---------------------------------------------------------------------------
# d. touch one texture: exactly one recook
# ---------------------------------------------------------------------------
printf 'X' >> "$PROJ/sky_moon.png" # content hash changes; ktx still ingests it
if ! (cd "$PROJ" && "$FURYCLI" cook scene.json --texture-target legacy --ddc "$DDC" \
        "${RHI_ARGS[@]}" > "$LOGS/d_touch.log" 2>&1); then
    cat "$LOGS/d_touch.log" >&2; die "stage d: recook after touch failed"
fi
grep -q "1 textures cooked, 2 ddc-hits; 0 shader targets cooked" "$LOGS/d_touch.log" \
    || die "stage d: expected one texture recook and shader DDC hits, got: $(grep 'done:' "$LOGS/d_touch.log" || true)"
grep -q "cooked sky_moon.png" "$LOGS/d_touch.log" \
    || die "stage d: recooked texture was not sky_moon.png"
stage "d" "touch sky_moon.png: exactly 1 recooked, 2 ddc-hits"

# ---------------------------------------------------------------------------
# e. package (lz4 default) + headless exec on the pak
# ---------------------------------------------------------------------------
if ! (cd "$PROJ" && "$FURYCLI" package scene.json --texture-target legacy --ddc "$DDC" \
        "${RHI_ARGS[@]}" > "$LOGS/e_package.log" 2>&1); then
    cat "$LOGS/e_package.log" >&2; die "stage e: package failed"
fi
[ -f "$PROJ/scene.pak" ] || die "stage e: scene.pak not written"
if ! (cd "$PROJ" && "$FURY" exec scene.pak "$LUA_TEST" 3 4 sky_moon.png check_postprocess \
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
        "${RHI_ARGS[@]}" --compression none --output "$TMP/none.pak") > "$LOGS/h_package.log" 2>&1; then
    cat "$LOGS/h_package.log" >&2; die "stage h: package --compression none failed"
fi
[ -f "$TMP/none.pak" ] || die "stage h: none.pak not written"
python3 - "$TMP/none.pak" "$MANIFEST" <<'PY'
import hashlib, json, struct, sys
path, manifest_path = sys.argv[1:3]
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
manifest = json.load(open(manifest_path))
expect = {"scene.json", "sky_cloud.png", "sky_moon.png", "terrain_rock.png"}
expect.update(e["key"] for e in manifest["shaders"])
expect.update(e["key"] for e in manifest["passthrough"])
assert set(entries) == expect, "entry keys %s != %s" % (sorted(entries), sorted(expect))
assert boot == "scene.json", "boot key is %r" % boot
for key, (codec, nblocks) in entries.items():
    assert codec == 0 and nblocks == 0, "%s not stored uncompressed" % key
print("none.pak OK: %d entries, boot scene.json, all codec=0 (stored)" % len(entries))
PY
stage "h" "--compression none: footer/index parse, all entries stored"

# ---------------------------------------------------------------------------
# i. pak round trip: unpack, byte-compare against sources, repack, re-mount
# ---------------------------------------------------------------------------
# scene.pak (lz4) and none.pak (stored) must hold identical entry bytes, and
# those bytes must equal the scene file plus the DDC artifacts the manifest
# names. Entries are then repacked with an independent writer and mounted by
# the engine from a cwd with no loose files, so a missing or mis-keyed entry
# cannot be masked by disk fallback. Two corruption copies pin the SHA-1
# tripwires: a flipped payload byte must fail ReadEntry, a flipped index byte
# must fail Mount.
if ! python3 - "$PROJ" "$DDC" "$MANIFEST" "$PROJ/scene.pak" "$TMP/none.pak" \
        "$TMP/roundtrip.pak" "$TMP/rt_corrupt_payload.pak" "$TMP/rt_corrupt_index.pak" \
        > "$LOGS/i_roundtrip.log" 2>&1 <<'PY'
import hashlib, json, os, struct, sys

proj, ddc, manifest_path, lz4_pak, raw_pak, out_pak, out_bad_payload, out_bad_index = sys.argv[1:9]

def parse_pak(path):
    data = open(path, "rb").read()
    assert len(data) >= 52, path + ": too small for a footer"
    magic, version, idx_off, idx_size, idx_sha, _ = \
        struct.unpack_from("<8sIQQ20sI", data, len(data) - 52)
    assert magic == b"FURYPAK1" and version == 1, path + ": bad footer"
    idx = data[idx_off:idx_off + idx_size]
    assert hashlib.sha1(idx).digest() == idx_sha, path + ": index sha1 mismatch"
    (count,) = struct.unpack_from("<I", idx, 0)
    pos, entries = 4, []
    for _ in range(count):
        (klen,) = struct.unpack_from("<I", idx, pos); pos += 4
        key = idx[pos:pos + klen].decode("utf-8"); pos += klen
        off, usize = struct.unpack_from("<QQ", idx, pos); pos += 16
        codec, bsize, nblocks = struct.unpack_from("<III", idx, pos); pos += 12
        blocks = list(struct.unpack_from("<%dI" % nblocks, idx, pos)); pos += 4 * nblocks
        sha = idx[pos:pos + 20]; pos += 20
        entries.append((key, off, usize, codec, bsize, blocks, sha))
    (blen,) = struct.unpack_from("<I", idx, pos); pos += 4
    boot = idx[pos:pos + blen].decode("utf-8"); pos += blen
    assert pos == idx_size, path + ": trailing bytes in index"
    return data, entries, boot, idx_off

def lz4_block(src, expected, key):
    # Minimal LZ4 block decoder (sequence = token, literals, offset, match).
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]; i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1
                lit += b
                if b != 255: break
        out += src[i:i + lit]; i += lit
        if i >= n: break # final sequence carries literals only
        off = src[i] | (src[i + 1] << 8); i += 2
        mlen = token & 15
        if mlen == 15:
            while True:
                b = src[i]; i += 1
                mlen += b
                if b != 255: break
        assert 0 < off <= len(out), key + ": bad match offset"
        for _ in range(mlen + 4):
            out.append(out[-off])
    assert len(out) == expected, "%s: lz4 decoded %d != %d" % (key, len(out), expected)
    return bytes(out)

def extract(data, e):
    key, off, usize, codec, bsize, blocks, sha = e
    if codec == 0:
        out = data[off:off + usize]
        assert len(out) == usize, key + ": stored entry truncated"
    else:
        # Each 64 KB block is an independent LZ4 stream (offsets never cross
        # block boundaries), so decode block by block like PakFile::ReadEntry.
        out = bytearray()
        pos = off
        for bs in blocks:
            expected = min(bsize, usize - len(out))
            out += lz4_block(data[pos:pos + bs], expected, key)
            pos += bs
        out = bytes(out)
        assert len(out) == usize, key + ": lz4 blocks under-produced"
    assert hashlib.sha1(out).digest() == sha, key + ": extracted sha1 != index sha1"
    return out

manifest = json.load(open(manifest_path))
sources = {"scene.json": open(os.path.join(proj, "scene.json"), "rb").read()}
for e in manifest["textures"]:
    sources[e["key"]] = open(os.path.join(ddc, *e["ddc"].split("/")), "rb").read()
for e in manifest["shaders"]:
    sources[e["key"]] = open(os.path.join(ddc, *e["ddc"].split("/")), "rb").read()
for e in manifest["passthrough"]:
    sources[e["key"]] = open(e["source"], "rb").read()

lz4_data, lz4_entries, lz4_boot, _ = parse_pak(lz4_pak)
raw_data, raw_entries, raw_boot, _ = parse_pak(raw_pak)
lz4_keys = [e[0] for e in lz4_entries]
assert lz4_keys == [e[0] for e in raw_entries], "key order differs between paks"
assert lz4_boot == raw_boot == "scene.json", "boot key mismatch"
assert set(lz4_keys) == set(sources), "pak keys %s != source set" % sorted(lz4_keys)

lz4_blobs, raw_blobs, n_lz4 = {}, {}, 0
for e in lz4_entries:
    lz4_blobs[e[0]] = extract(lz4_data, e)
    if e[3] == 1:
        n_lz4 += 1
for e in raw_entries:
    raw_blobs[e[0]] = extract(raw_data, e)
assert n_lz4 >= 1, "lz4 pak has no compressed entry: round trip covers nothing"
for key in lz4_keys:
    assert lz4_blobs[key] == raw_blobs[key], key + ": lz4 vs stored bytes differ"
    assert lz4_blobs[key] == sources[key], key + ": pak bytes != source bytes"
print("unpack OK: %d entries, %d lz4-decoded, all byte-identical to sources"
    % (len(lz4_keys), n_lz4))

def write_pak(path, blobs, boot):
    items = sorted(blobs.items(), key=lambda kv: kv[0].encode("utf-8"))
    data = bytearray()
    index = bytearray(struct.pack("<I", len(items)))
    for key, blob in items:
        kb = key.encode("utf-8")
        index += struct.pack("<I", len(kb)) + kb
        index += struct.pack("<QQ", len(data), len(blob))
        index += struct.pack("<III", 0, 0, 0) # stored: codec, block size, no blocks
        index += hashlib.sha1(blob).digest()
        data += blob
    bb = boot.encode("utf-8")
    index += struct.pack("<I", len(bb)) + bb
    footer = b"FURYPAK1" + struct.pack("<IQQ20sI", 1, len(data), len(index),
        hashlib.sha1(index).digest(), 0)
    open(path, "wb").write(bytes(data) + bytes(index) + footer)

write_pak(out_pak, lz4_blobs, lz4_boot)
# corrupt copies: one payload byte inside sky_moon.png, one index byte
rt = bytearray(open(out_pak, "rb").read())
_, entries, _, idx_off = parse_pak(out_pak)
moon = [e for e in entries if e[0] == "sky_moon.png"][0]
bad = bytearray(rt)
bad[moon[1] + moon[2] // 2] ^= 0x01
open(out_bad_payload, "wb").write(bytes(bad))
bad = bytearray(rt)
bad[idx_off + 9] ^= 0x01 # inside the first index key
open(out_bad_index, "wb").write(bytes(bad))

# re-parse the repacked pak with the independent reader before handing it to
# the engine: structure, index sha1, per-entry sha1, source byte equality
rp_data, rp_entries, rp_boot, _ = parse_pak(out_pak)
assert [e[0] for e in rp_entries] == lz4_keys and rp_boot == lz4_boot
for e in rp_entries:
    assert extract(rp_data, e) == sources[e[0]], e[0] + ": repack bytes != source"
print("repack OK: independent writer + re-reader agree with sources")
PY
then
    cat "$LOGS/i_roundtrip.log" >&2; die "stage i: unpack/repack verification failed"
fi
cat "$LOGS/i_roundtrip.log"
grep -q "unpack OK" "$LOGS/i_roundtrip.log" || die "stage i: unpack verification missing"
grep -q "repack OK" "$LOGS/i_roundtrip.log" || die "stage i: repack verification missing"

# re-mount the repacked pak from an isolated cwd: no loose files, so every
# asset byte must come from the pak itself
mkdir -p "$TMP/iso"
cp "$TMP/roundtrip.pak" "$TMP/iso/roundtrip.pak"
if ! (cd "$TMP/iso" && "$FURY" exec roundtrip.pak "$LUA_TEST" 3 4 sky_moon.png check_postprocess \
        > "$LOGS/i_remount.log" 2>&1); then
    cat "$LOGS/i_remount.log" >&2; die "stage i: exec on repacked pak failed"
fi
grep -q "pak_scene_load: PASS" "$LOGS/i_remount.log" \
    || die "stage i: repacked pak PASS line missing"
if grep -q "Failed to load image\|EROR" "$LOGS/i_remount.log"; then
    grep "Failed to load image\|EROR" "$LOGS/i_remount.log" >&2
    die "stage i: repacked pak served a bad asset"
fi
stage "i1" "round trip: unpack both paks, byte-compare sources, repack, re-mount"

# corrupted payload: ReadEntry must trip the per-entry SHA-1
cp "$TMP/rt_corrupt_payload.pak" "$TMP/iso/roundtrip.pak"
set +e
(cd "$TMP/iso" && "$FURY" exec roundtrip.pak "$LUA_TEST" 3 4 sky_moon.png \
    > "$LOGS/i_bad_payload.log" 2>&1)
rc=$?
set -e
grep -q "payload SHA-1 mismatch" "$LOGS/i_bad_payload.log" \
    || { cat "$LOGS/i_bad_payload.log" >&2; \
         die "stage i: corrupted payload not caught (rc=$rc)"; }
stage "i2" "corrupt payload byte: SHA-1 mismatch caught (rc=$rc)"

# corrupted index: Mount must trip the index SHA-1
cp "$TMP/rt_corrupt_index.pak" "$TMP/iso/roundtrip.pak"
set +e
(cd "$TMP/iso" && "$FURY" exec roundtrip.pak "$LUA_TEST" 3 4 sky_moon.png \
    > "$LOGS/i_bad_index.log" 2>&1)
rc=$?
set -e
[ "$rc" -ne 0 ] || die "stage i: corrupted index mounted cleanly"
grep -q "index SHA-1 mismatch" "$LOGS/i_bad_index.log" \
    || { cat "$LOGS/i_bad_index.log" >&2; die "stage i: wrong mount failure reason"; }
stage "i3" "corrupt index byte: mount rejected (rc=$rc)"

echo "pak_pipeline: PASS"
