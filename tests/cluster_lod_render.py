#!/usr/bin/env python3
"""cluster_lod_render.py - GPU cluster LOD render verification driver.

Drives examples/fury Player.lua Projects/feature/feature.bin and verifies
the cluster LOD renderer three ways (per-frame stats come from the
"Cluster diagnostics" / "Cluster selected level" / "Cluster batch" logs
emitted by --gpu-dump; images from --screenshot):

  1. leaf_parity      FURY_CLUSTER_LOD=1 FURY_CLUSTER_LOD_ERROR=0 at the
                      near pose must select every leaf cluster: exact
                      selected/visible/command counts, exact per-material
                      batch command counts, zero overflows, and the near
                      legacy (FURY_CLUSTER_LOD=0) screenshot must match
                      the near cluster screenshot within pixel tolerance.
  2. adaptive_far     FURY_CLUSTER_LOD=1 with the default screen error:
                      the far pose (3x distance, same direction) must
                      select/draw strictly less than the near pose, and
                      both must stay within the error-0 leaf totals.
  3. budget_fallback  FURY_CLUSTER_LOD_BUDGET=30 must accept exactly the
                      Feu+ground clusters (30 selected) with zero
                      overflow; every tank instance falls back to the
                      legacy path, so the full-scene legacy and cluster
                      screenshots must match within tolerance.
  4. transformed_selection  Exact CPU/GPU selected sets at multiple
                      thresholds, with non-uniform scale and an off-screen
                      tank instance.

Expected leaf counts below were verified on Metal against
Projects/feature/feature.bin (selected=1536, level-0 triangles=134816,
batches Material_Grass27/Material_Wheel24/Material_Body1476/
Material_Glass6/Material_Wood3). Vulkan without `drawIndirectCount` instead
records one expected unsupported_backend skip after checking the legacy image;
it does not run or claim the MDI cases.

Images are compared with tests/tools/imgprobe.py metrics. The reference
legacy-vs-cluster pair in /tmp diffs at mean=0.0106 frac>8=0.000531;
separate runs still differ by particles/temporal state, so the default
tolerances (mean<=0.75, frac<=0.02) are far above the measured floor
without demanding byte identity.

Usage:
  python3 tests/cluster_lod_render.py --backend metal|vulkan
      [--binary examples/fury] [--cwd examples] [--artifacts DIR]
      [--render-thread 0|1] [--case NAME]... [--max-mean F] [--max-frac F]

Exit codes: 0 pass, 1 any case failure. Emits RESULT PASS/FAIL lines and
<artifacts>/summary.json.
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TESTS = Path(__file__).resolve().parent
IMGPROBE = TESTS / "tools" / "imgprobe.py"

SCRIPT = "Player.lua"
SCENE = "Projects/feature/feature.bin"
CAPTURE_FRAME = 30

# Pinned verification poses: px,py,pz,yawDeg,pitchDeg,farCm. The far pose
# is the near pose pushed 3x out along the same view direction.
CAM_NEAR = "15,8,17,41.43,-15.82,5000"
CAM_FAR = "45,24,51,41.43,-15.82,5000"

# Verified Metal error-0 leaf totals for the near pose.
LEAF_SELECTED = 1536
LEAF_TRIANGLES = 134816
LEAF_BATCHES = {"Material_Grass": 27, "Material_Wheel": 24,
                "Material_Body": 1476, "Material_Glass": 6, "Material_Wood": 3}

# FURY_CLUSTER_LOD_BUDGET=30: only the Feu+ground instances fit the budget.
BUDGET_SELECTED = 30

DEFAULT_MAX_MEAN = 0.75
DEFAULT_MAX_FRAC = 0.02

# Interleaved engine output can splice other lines into a record; parse
# each key=value out of the text from the first diagnostics record onward.
DIAG_MARK = "Cluster diagnostics"
KV_RE = r"%s=([0-9]+)"
LEVEL_RE = re.compile(r"Cluster selected level (\d+) clusters=(\d+) triangles=(\d+)")
BATCH_RE = re.compile(r"Cluster batch \d+ material='([^']+)'[^\n]*?commands=(\d+)")


def log(message):
    print(message, flush=True)


def probe(command, *args):
    proc = subprocess.run(
        [sys.executable, str(IMGPROBE), command] + [str(arg) for arg in args],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("imgprobe %s failed: %s" % (command, proc.stderr.strip()))
    return proc.stdout.strip()


def diff(a, b):
    mean, maximum, frac = probe("diff", a, b).split()
    return {"mean": float(mean), "max": int(maximum), "frac": float(frac)}


def run_fury(args, env_extra, mode, out_path, log_path):
    """One fury launch. mode='shot' captures out_path PNG, mode='dump'
    writes the GPU dump into out_path dir. Returns the log text."""
    env = dict(os.environ)
    env.update(env_extra)
    cmd = [args.binary, SCRIPT, SCENE, "--rhi=" + args.backend]
    if args.render_thread is not None:
        cmd.append("--render-thread=%d" % args.render_thread)
    if mode == "shot":
        cmd += ["--screenshot", str(out_path), "--screenshot-frame", str(CAPTURE_FRAME)]
    else:
        cmd += ["--gpu-dump", str(out_path), "--gpu-dump-frame", str(CAPTURE_FRAME)]
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        log_file.write("$ " + " ".join(cmd) + "\n")
        log_file.flush()
        try:
            proc = subprocess.run(
                cmd, cwd=args.cwd, env=env,
                stdout=log_file, stderr=subprocess.STDOUT, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            raise RuntimeError("timed out after %ss (see %s)" % (args.timeout, log_path))
    text = log_path.read_text(encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise RuntimeError("fury exited %d (see %s)" % (proc.returncode, log_path))
    errors = [line for line in text.splitlines() if "[EROR]" in line]
    if errors:
        raise RuntimeError("renderer errors (see %s): %s" % (log_path, errors[0].strip()))
    return text


def parse_stats(text, log_path):
    """Cluster diagnostics for one frame: selected/visible/commands,
    overflows, per-level triangle totals, per-material command totals."""
    diag_at = text.find(DIAG_MARK)
    if diag_at < 0:
        raise RuntimeError("no '%s' line in output (see %s)" % (DIAG_MARK, log_path))
    diag_text = text[diag_at:]

    def number(key):
        match = re.search(KV_RE % key, diag_text)
        if not match:
            raise RuntimeError("missing '%s=' in cluster stats (see %s)" % (key, log_path))
        return int(match.group(1))

    stats = {
        "selected": number("selected"),
        "instances": number("instances"),
        "visibleInstances": number("visibleInstances"),
        "visible": number("visible"),
        "commands": number("commands"),
        "selectionOverflow": number("selectionOverflow"),
        "visibilityOverflow": number("visibilityOverflow"),
        "selectionMismatches": number("mismatches"),
        "roots": number("roots"),
    }
    levels = {int(level): {"clusters": int(clusters), "triangles": int(triangles)}
              for level, clusters, triangles in LEVEL_RE.findall(text)}
    batches = {}
    for material, commands in BATCH_RE.findall(text):
        batches[material] = batches.get(material, 0) + int(commands)
    if not levels:
        raise RuntimeError("no 'Cluster selected level' lines (see %s)" % log_path)
    if not batches:
        raise RuntimeError("no 'Cluster batch' lines (see %s)" % log_path)
    stats["levels"] = levels
    stats["batches"] = batches
    stats["drawn_triangles"] = number("drawnTriangles")
    return stats


def assert_zero_overflow(stats):
    for key in ("selectionOverflow", "visibilityOverflow", "selectionMismatches"):
        if stats[key] != 0:
            raise RuntimeError("%s=%d, want 0" % (key, stats[key]))


def image_pair(args, case_dir, name_a, env_a, name_b, env_b):
    """Screenshot both env sets and return (metrics, png_a, png_b)."""
    png_a = case_dir / (name_a + ".png")
    png_b = case_dir / (name_b + ".png")
    run_fury(args, env_a, "shot", png_a, case_dir / (name_a + ".log"))
    run_fury(args, env_b, "shot", png_b, case_dir / (name_b + ".log"))
    return diff(png_a, png_b), png_a, png_b


def check_image(metrics, max_mean, max_frac):
    if metrics["mean"] >= max_mean:
        raise RuntimeError("image mean %.4f must be < %.4f" % (metrics["mean"], max_mean))
    if metrics["frac"] >= max_frac:
        raise RuntimeError("image frac>8 %.6f must be < %.6f" % (metrics["frac"], max_frac))


def case_leaf_parity(args, case_dir):
    """Near pose, error 0: exact leaf counts + legacy/cluster image parity."""
    cluster_env = {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "1", "FURY_CLUSTER_LOD_ERROR": "0"}
    metrics, legacy_png, cluster_png = image_pair(
        args, case_dir, "legacy", {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "0"},
        "cluster_error0", cluster_env)
    check_image(metrics, args.max_mean, args.max_frac)

    stats = parse_stats(
        run_fury(args, cluster_env, "dump", case_dir / "cluster_error0.dump",
                 case_dir / "cluster_error0.dump.log"),
        case_dir / "cluster_error0.dump.log")
    if stats["selected"] != LEAF_SELECTED or stats["visible"] != LEAF_SELECTED \
            or stats["commands"] != LEAF_SELECTED:
        raise RuntimeError("leaf cut selected=%d visible=%d commands=%d, want %d/%d/%d" % (
            stats["selected"], stats["visible"], stats["commands"],
            LEAF_SELECTED, LEAF_SELECTED, LEAF_SELECTED))
    level0 = stats["levels"].get(0)
    if level0 is None or level0["triangles"] != LEAF_TRIANGLES:
        raise RuntimeError("level 0 triangles %s, want %d" % (
            level0 and level0["triangles"], LEAF_TRIANGLES))
    for material, commands in LEAF_BATCHES.items():
        if stats["batches"].get(material) != commands:
            raise RuntimeError("batch '%s' commands=%s, want %d" % (
                material, stats["batches"].get(material), commands))
    if sum(stats["batches"].values()) != LEAF_SELECTED:
        raise RuntimeError("batch command total %d != selected %d" % (
            sum(stats["batches"].values()), LEAF_SELECTED))
    assert_zero_overflow(stats)
    return {"image": metrics, "stats": stats}, \
        "image mean=%.4f frac=%.6f; selected=%d level0_tri=%d batches=%s" % (
            metrics["mean"], metrics["frac"], stats["selected"],
            level0["triangles"], ",".join("%s%d" % (m, c) for m, c in LEAF_BATCHES.items()))


def case_adaptive_far(args, case_dir):
    """Default screen error: far pose must draw less than near, within leaf."""
    near_stats = parse_stats(
        run_fury(args, {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "1"}, "dump",
                 case_dir / "near.dump", case_dir / "near.dump.log"),
        case_dir / "near.dump.log")
    far_stats = parse_stats(
        run_fury(args, {"FURY_CAM": CAM_FAR, "FURY_CLUSTER_LOD": "1"}, "dump",
                 case_dir / "far.dump", case_dir / "far.dump.log"),
        case_dir / "far.dump.log")
    assert_zero_overflow(near_stats)
    assert_zero_overflow(far_stats)
    for label, stats in (("near", near_stats), ("far", far_stats)):
        if stats["drawn_triangles"] > LEAF_TRIANGLES:
            raise RuntimeError("%s drawn %d > leaf total %d" % (
                label, stats["drawn_triangles"], LEAF_TRIANGLES))
        if stats["selected"] > LEAF_SELECTED:
            raise RuntimeError("%s selected %d > leaf total %d" % (
                label, stats["selected"], LEAF_SELECTED))
    if not far_stats["selected"] < near_stats["selected"]:
        raise RuntimeError("far selected %d must be < near selected %d" % (
            far_stats["selected"], near_stats["selected"]))
    if not far_stats["drawn_triangles"] < near_stats["drawn_triangles"]:
        raise RuntimeError("far drawn %d must be < near drawn %d" % (
            far_stats["drawn_triangles"], near_stats["drawn_triangles"]))
    return {"near": near_stats, "far": far_stats}, \
        "selected %d>%d drawn %d>%d (leaf cap %d/%d)" % (
            near_stats["selected"], far_stats["selected"],
            near_stats["drawn_triangles"], far_stats["drawn_triangles"],
            LEAF_SELECTED, LEAF_TRIANGLES)


def case_budget_fallback(args, case_dir):
    """Budget 30: only Feu+ground accepted, tanks fall back, scene matches."""
    cluster_env = {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "1",
                   "FURY_CLUSTER_LOD_BUDGET": str(BUDGET_SELECTED)}
    metrics, legacy_png, cluster_png = image_pair(
        args, case_dir, "legacy", {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "0"},
        "cluster_budget", cluster_env)
    check_image(metrics, args.max_mean, args.max_frac)

    stats = parse_stats(
        run_fury(args, cluster_env, "dump", case_dir / "cluster_budget.dump",
                 case_dir / "cluster_budget.dump.log"),
        case_dir / "cluster_budget.dump.log")
    assert_zero_overflow(stats)
    if stats["selected"] != BUDGET_SELECTED:
        raise RuntimeError("budget selected=%d, want %d (Feu+ground only)" % (
            stats["selected"], BUDGET_SELECTED))
    return {"image": metrics, "stats": stats}, \
        "image mean=%.4f frac=%.6f; budget selected=%d overflows=0" % (
            metrics["mean"], metrics["frac"], stats["selected"])


def case_transformed_selection(args, case_dir):
    """Exact CPU/GPU sets under non-uniform scale and an off-screen instance."""
    results = {}
    for name, transform in (("baseline", ""), ("nonuniform", "nonuniform"), ("offscreen", "offscreen")):
        env = {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "1",
               "FURY_CLUSTER_LOD_ERROR": "0.25",
               "FURY_CLUSTER_LOD_TEST_TRANSFORM": transform}
        stats = parse_stats(
            run_fury(args, env, "dump", case_dir / (name + ".dump"),
                     case_dir / (name + ".dump.log")), case_dir / (name + ".dump.log"))
        assert_zero_overflow(stats)
        results[name] = stats
    if results["offscreen"]["selected"] >= results["baseline"]["selected"]:
        raise RuntimeError("off-screen instance did not reduce selected work: %d >= %d" % (
            results["offscreen"]["selected"], results["baseline"]["selected"]))
    return results, "exact selected sets; nonuniform=%d offscreen=%d" % (
        results["nonuniform"]["selected"], results["offscreen"]["selected"])


def case_selection_thresholds(args, case_dir):
    """Exact sets from leaves through roots, with non-increasing triangle work."""
    results = {}
    previous = None
    for threshold in (0.0, 0.25, 1.0, 1000000000000.0):
        key = str(threshold)
        env = {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "1",
               "FURY_CLUSTER_LOD_ERROR": key}
        stats = parse_stats(
            run_fury(args, env, "dump", case_dir / (key + ".dump"),
                     case_dir / (key + ".dump.log")), case_dir / (key + ".dump.log"))
        assert_zero_overflow(stats)
        if previous is not None and stats["drawn_triangles"] > previous:
            raise RuntimeError("drawn triangles increased at threshold %s: %d > %d" % (
                key, stats["drawn_triangles"], previous))
        previous = stats["drawn_triangles"]
        results[key] = stats
    if results["1000000000000.0"]["roots"] != results["1000000000000.0"]["selected"]:
        raise RuntimeError("coarse cut contains non-root nodes: roots=%d selected=%d" % (
            results["1000000000000.0"]["roots"], results["1000000000000.0"]["selected"]))
    return results, "exact sets; selected %s" % (
        " > ".join(str(results[key]["selected"]) for key in results))


CASES = {
    "leaf_parity": case_leaf_parity,
    "adaptive_far": case_adaptive_far,
    "budget_fallback": case_budget_fallback,
    "transformed_selection": case_transformed_selection,
    "selection_thresholds": case_selection_thresholds,
}


def run_case(args, name, artifacts):
    case_dir = artifacts / name
    case_dir.mkdir(parents=True, exist_ok=True)
    result = {"id": name, "status": "fail", "metrics": None, "detail": None}
    try:
        metrics, detail = CASES[name](args, case_dir)
        result.update(status="pass", metrics=metrics, detail=detail)
        log("RESULT PASS %s: %s" % (name, detail))
    except (KeyError, RuntimeError, OSError) as exc:
        result["detail"] = str(exc)
        log("RESULT FAIL %s: %s" % (name, exc))
    with open(case_dir / "result.json", "w", encoding="ascii") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    return result


def probe_unsupported_backend(args, artifacts):
    """On Vulkan without GPU-written counts, verify legacy fallback and skip MDI cases."""
    if args.backend != "vulkan":
        return None
    case_dir = artifacts / "unsupported_backend"
    case_dir.mkdir(parents=True, exist_ok=True)
    result = {"id": "unsupported_backend", "status": "fail", "metrics": None, "detail": None}
    try:
        image = case_dir / "legacy.png"
        text = run_fury(args, {"FURY_CAM": CAM_NEAR, "FURY_CLUSTER_LOD": "1"},
                        "shot", image, case_dir / "legacy.log")
        if "count=0" not in text:
            return None
        if DIAG_MARK in text:
            raise RuntimeError("cluster work ran despite unavailable GPU-written indirect counts")
        if not image.is_file() or image.stat().st_size == 0:
            raise RuntimeError("legacy fallback screenshot missing")
        result.update(status="skipped", metrics={"image": str(image)},
                      detail="GPU-written indirect counts unavailable; legacy fallback verified")
        log("RESULT SKIP unsupported_backend: " + result["detail"])
    except (RuntimeError, OSError) as exc:
        result["detail"] = str(exc)
        log("RESULT FAIL unsupported_backend: %s" % exc)
    with open(case_dir / "result.json", "w", encoding="ascii") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    return result


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--backend", required=True, choices=["metal", "vulkan"])
    parser.add_argument("--binary", default=str(REPO / "examples" / "fury"))
    parser.add_argument("--cwd", default=str(REPO / "examples"))
    parser.add_argument("--artifacts", default=None,
                        help="artifact directory (default: "
                             "<repo>/test-artifacts/cluster-lod-<backend>)")
    parser.add_argument("--render-thread", type=int, choices=[0, 1], default=None)
    parser.add_argument("--case", action="append", default=[],
                        choices=sorted(CASES), help="restrict to these cases")
    parser.add_argument("--max-mean", type=float, default=DEFAULT_MAX_MEAN,
                        help="image mean-abs-diff ceiling (default %.2f)" % DEFAULT_MAX_MEAN)
    parser.add_argument("--max-frac", type=float, default=DEFAULT_MAX_FRAC,
                        help="image frac>8 ceiling (default %.3f)" % DEFAULT_MAX_FRAC)
    parser.add_argument("--timeout", type=int, default=240, help="per-launch timeout (s)")
    args = parser.parse_args(argv)

    if not Path(args.binary).is_file():
        log("RESULT FAIL missing binary: " + args.binary)
        return 1
    args.binary = str(Path(args.binary).resolve())
    args.cwd = str(Path(args.cwd).resolve())
    artifacts = (Path(args.artifacts) if args.artifacts else
                 REPO / "test-artifacts" / ("cluster-lod-%s" % args.backend)).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)

    unsupported = probe_unsupported_backend(args, artifacts)
    selected = args.case or sorted(CASES)
    results = [unsupported] if unsupported is not None else [run_case(args, name, artifacts) for name in selected]
    summary = {
        "backend": args.backend,
        "render_thread": args.render_thread,
        "scene": SCENE,
        "capture_frame": CAPTURE_FRAME,
        "counts": {
            "pass": sum(result["status"] == "pass" for result in results),
            "fail": sum(result["status"] == "fail" for result in results),
            "skipped": sum(result["status"] == "skipped" for result in results),
        },
        "results": results,
    }
    with open(artifacts / "summary.json", "w", encoding="ascii") as output:
        json.dump(summary, output, indent=2)
        output.write("\n")
    return 1 if summary["counts"]["fail"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
