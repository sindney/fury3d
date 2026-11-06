#!/usr/bin/env python3
"""render_baseline.py - capture and compare a deterministic render scenario.

Part of the rendering automation lane (change:
refactor-automation-and-sample-scenes, tasks 8.2-8.4). Drives
tests/lua/render_scenario.lua through the fury launcher, verifies the
semantic checks and capture metadata, then compares the capture against
the backend-specific approved baseline:

  tests/baselines/<backend>/<scenario>.png        approved capture
  tests/baselines/<backend>/<scenario>.json       capture metadata

Baselines are ONLY written by the explicit update operation
(--update or FURY_UPDATE_BASELINES=1, which tests/run.py sets from its
--update-baselines flag). A missing baseline is a failure that still
writes the actual capture into the artifact directory for review; it is
never silently approved.

Usage:
  python3 tests/render_baseline.py --scenario NAME --backend BACKEND
      [--binary examples/fury] [--cwd examples]
      [--scenarios tests/render_scenarios.json]
      [--baselines tests/baselines] [--artifacts DIR]
      [--update] [--no-temporal]

Exit codes: 0 pass, 1 scenario failure (log line: RESULT PASS/FAIL ...).
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
IMGPROBE = Path(__file__).resolve().parent / "tools" / "imgprobe.py"


def log(msg):
    print(msg, flush=True)


def resolve(path, ctx):
    for key, rep in ctx.items():
        path = path.replace("{" + key + "}", str(rep))
    return path


def load_config(path):
    with open(path, "r", encoding="ascii") as fh:
        return json.load(fh)


def merge_tolerances(defaults, scenario, backend=None):
    tol = dict(defaults.get("tolerances", {}))
    tol.update(scenario.get("tolerances", {}))
    per_backend = scenario.get("backend_tolerances", {})
    if backend in per_backend:
        tol.update(per_backend[backend])
    return tol


def run_capture(args, scenario, cfg, backend, temporal, out_png, log_path):
    """Run one fury capture. Returns (ok, log_text)."""
    env = dict(os.environ)
    env["FURY_SCENARIO"] = args.scenario
    env["FURY_RESOLUTION"] = "%dx%d" % (cfg["width"], cfg["height"])
    if temporal:
        env["FURY_SCENARIO_TEMPORAL"] = "1"
    for key, value in scenario.get("env", {}).items():
        env[key] = value

    cmd = [
        args.binary, str(Path(__file__).resolve().parent / "lua" / "render_scenario.lua"),
        args.scenario,
        "--rhi=" + backend,
    ]
    if temporal:
        tcfg = scenario.get("temporal", {})
        cmd += ["--screenshot-series",
                "%s,%d,%d" % (out_png, tcfg.get("frames", 4),
                              tcfg.get("interval", 4)),
                "--screenshot-frame", str(scenario.get(
                    "capture_frame", cfg.get("capture_frame", 40)))]
    else:
        cmd += ["--screenshot", str(out_png),
                "--screenshot-frame", str(scenario.get(
                    "capture_frame", cfg.get("capture_frame", 40)))]

    with open(log_path, "w", encoding="utf-8", errors="replace") as lf:
        lf.write("$ " + " ".join(cmd) + "\n")
        lf.flush()
        try:
            proc = subprocess.run(
                cmd, cwd=args.cwd, env=env,
                stdout=lf, stderr=subprocess.STDOUT,
                timeout=scenario.get("timeout", cfg.get("timeout", 600)),
            )
            rc = proc.returncode
        except subprocess.TimeoutExpired:
            return False, "timeout after %ss (see %s)" % (
                scenario.get("timeout", cfg.get("timeout", 600)), log_path)
    if rc != 0:
        return False, "fury exit %s (see %s)" % (rc, log_path)
    return True, Path(log_path).read_text(encoding="utf-8", errors="replace")


def check_meta(text, args, cfg, backend):
    """Verify SCENARIO_SEMANTIC_OK and SCENARIO_META match the request."""
    if "SCENARIO_SEMANTIC_OK " + args.scenario not in text:
        return None, "semantic checks did not pass (no SCENARIO_SEMANTIC_OK line)"
    # Engine log lines can interleave into the same line, so match the
    # marker wherever it appears and read to end of line.
    m = re.search(r"SCENARIO_META (name=.*)$", text, re.MULTILINE)
    if not m:
        return None, "no SCENARIO_META line in output"
    meta = dict(kv.split("=", 1) for kv in m.group(1).split())
    want_size = "%dx%d" % (cfg["width"], cfg["height"])
    if meta.get("size") != want_size:
        return meta, "capture size %s != requested %s" % (meta.get("size"), want_size)
    if meta.get("backend") != backend:
        return meta, "requested backend %s, engine ran %s" % (
            backend, meta.get("backend"))
    expected_clustered = os.environ.get("FURY_EXPECT_CLUSTERED")
    if expected_clustered is not None and meta.get("clustered_effective") != expected_clustered:
        return meta, "expected clustered_effective=%s, got %s" % (
            expected_clustered, meta.get("clustered_effective"))
    if backend == "opengl" and expected_clustered == "0" and "cluster_assign_shader" in text:
        return meta, "OpenGL fallback performed clustered assignment work"
    return meta, None


def img_diff(a, b, masks, include=None):
    cmd = [sys.executable, str(IMGPROBE)]
    if include:
        cmd += ["diff", a, b, include]
    elif masks:
        cmd += ["diffmask", a, b] + masks
    else:
        cmd += ["diff", a, b]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("imgprobe failed: " + out.stderr.strip())
    parts = out.stdout.split()
    metrics = {
        "mean_abs_diff": float(parts[0]),
        "max_abs_diff": float(parts[1]),
        "frac_gt_8": float(parts[2]),
    }
    if masks:
        metrics["eval_frac"] = float(parts[3])
    return metrics


def within_tolerance(metrics, tol):
    for key, limit in tol.items():
        if key in metrics and metrics[key] > limit:
            return key, metrics[key], limit
    return None


def write_diff_image(actual, expected, masks, out_path):
    """Amplified per-pixel abs-diff image; masked regions black."""
    from PIL import Image
    a = Image.open(actual).convert("RGB")
    b = Image.open(expected).convert("RGB")
    if a.size != b.size:
        a = a.resize(b.size)
    px_a, px_b = a.getdata(), b.getdata()
    excl = []
    for spec in masks:
        excl.append([int(v) for v in spec.split(",")])
    out = []
    for i, (ca, cb) in enumerate(zip(px_a, px_b)):
        x, y = i % a.width, i // a.width
        skip = any(x0 <= x < x1 and y0 <= y < y1
                   for x0, y0, x1, y1 in excl)
        if skip:
            out.append((0, 0, 0))
        else:
            out.append(tuple(min(255, 4 * abs(ca[k] - cb[k])) for k in range(3)))
    img = Image.new("RGB", a.size)
    img.putdata(out)
    img.save(out_path)


def slice_cells(atlas_png, count, cell_w, cell_h):
    """Split a screenshot-series atlas into per-cell PNGs."""
    from PIL import Image
    atlas = Image.open(atlas_png)
    cols = 1
    while cols * cols < count:
        cols += 1
    cells = []
    for f in range(count):
        cx, cy = (f % cols) * cell_w, (f // cols) * cell_h
        cell = atlas.crop((cx, cy, cx + cell_w, cy + cell_h))
        path = atlas_png.parent / ("%s_cell%d.png" % (atlas_png.stem, f))
        cell.save(path)
        cells.append(path)
    return cells


def compare(args, scenario, cfg, backend, meta, artifacts):
    """Compare the actual capture against the approved baseline."""
    actual = artifacts / ("%s.actual.png" % args.scenario)
    baseline_dir = Path(args.baselines) / backend
    baseline = baseline_dir / (args.scenario + ".png")
    baseline_meta_path = baseline_dir / (args.scenario + ".json")
    masks = scenario.get("masks", cfg.get("masks", []))
    tol = merge_tolerances(cfg, scenario, backend)

    if not baseline.exists():
        if args.update:
            baseline_dir.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(actual, baseline)
            with open(baseline_meta_path, "w", encoding="ascii") as fh:
                json.dump({
                    "scenario": args.scenario,
                    "backend": backend,
                    "approved_by": "render_baseline.py --update",
                    "meta": meta,
                    "tolerances": tol,
                    "masks": masks,
                }, fh, indent=2)
                fh.write("\n")
            return True, "baseline written: %s" % baseline
        return False, ("missing baseline for backend '%s' (run with "
                       "--update-baselines to approve): %s" % (backend, baseline))

    if args.update:
        log("RESULT NOTE updating existing baseline %s" % baseline)
        baseline_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(actual, baseline)
        with open(baseline_meta_path, "w", encoding="ascii") as fh:
            json.dump({
                "scenario": args.scenario,
                "backend": backend,
                "approved_by": "render_baseline.py --update",
                "meta": meta,
                "tolerances": tol,
                "masks": masks,
            }, fh, indent=2)
            fh.write("\n")
        return True, "baseline updated: %s" % baseline

    metrics = img_diff(str(actual), str(baseline), masks)
    breach = within_tolerance(metrics, tol)
    if breach:
        expected_copy = artifacts / ("%s.expected.png" % args.scenario)
        shutil.copyfile(baseline, expected_copy)
        diff_png = artifacts / ("%s.diff.png" % args.scenario)
        write_diff_image(str(actual), str(baseline), masks, str(diff_png))
        with open(artifacts / ("%s.metrics.json" % args.scenario), "w",
                  encoding="ascii") as fh:
            json.dump({"scenario": args.scenario, "backend": backend,
                       "metrics": metrics, "tolerances": tol,
                       "breach": breach}, fh, indent=2)
            fh.write("\n")
        return False, ("image diff %s=%.4f exceeds tolerance %.4f "
                       "(actual=%s expected=%s diff=%s)" % (
                           breach[0], breach[1], breach[2],
                           actual, expected_copy, diff_png))
    detail = "mean=%.4f max=%d frac>8=%.6f (tol mean<=%.3f frac<=%.4f)" % (
        metrics["mean_abs_diff"], metrics["max_abs_diff"], metrics["frac_gt_8"],
        tol.get("mean_abs_diff", -1), tol.get("frac_gt_8", -1))
    return True, "baseline match: " + detail


def temporal_check(args, scenario, cfg, backend, artifacts):
    """Series capture: animated regions must move, static regions must not."""
    tcfg = scenario.get("temporal")
    if not tcfg:
        return True, "no temporal checks"
    frames = tcfg.get("frames", 4)
    interval = tcfg.get("interval", 4)
    atlas = artifacts / ("%s.series.png" % args.scenario)
    log_path = artifacts / ("%s.series.log" % args.scenario)
    ok, text = run_capture(args, scenario, cfg, backend, True, atlas, log_path)
    if not ok:
        return False, "temporal capture failed: " + text
    meta, err = check_meta(text, args, cfg, backend)
    if err:
        return False, "temporal capture: " + err
    if not atlas.exists():
        return False, "temporal capture produced no atlas (see %s)" % log_path

    cells = slice_cells(atlas, frames, cfg["width"], cfg["height"])
    checks = []
    for region_name, region in tcfg.get("move_regions", {}).items():
        m = img_diff(str(cells[0]), str(cells[1]), [], include=region)
        checks.append((region_name, "move", m["mean_abs_diff"],
                       tcfg.get("min_move_mean", 0.5)))
    for region_name, region in tcfg.get("static_regions", {}).items():
        m = img_diff(str(cells[0]), str(cells[1]), [], include=region)
        checks.append((region_name, "static", m["mean_abs_diff"],
                       tcfg.get("max_static_mean", 0.5)))

    fails = []
    for region_name, kind, value, bound in checks:
        if kind == "move" and value <= bound:
            fails.append("%s should move (mean=%.4f > %.3f)" % (
                region_name, value, bound))
        if kind == "static" and value > bound:
            fails.append("%s should be static (mean=%.4f <= %.3f)" % (
                region_name, value, bound))
    detail = "; ".join("%s %s=%.4f" % (n, k, v) for n, k, v, _ in checks)
    if fails:
        return False, "temporal check failed (%s) [%s]" % ("; ".join(fails), detail)
    return True, "temporal ok (%s)" % detail


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--backend", required=True)
    parser.add_argument("--binary", default="examples/fury")
    parser.add_argument("--cwd", default=str(REPO / "examples"))
    parser.add_argument("--scenarios", default=str(
        Path(__file__).resolve().parent / "render_scenarios.json"))
    parser.add_argument("--baselines", default=str(
        Path(__file__).resolve().parent / "baselines"))
    parser.add_argument("--artifacts", default=None,
                        help="artifact directory (default: <repo>/test-artifacts/render-<scenario>-<backend>)")
    parser.add_argument("--update", action="store_true",
                        help="approve the capture as the new baseline")
    parser.add_argument("--no-temporal", action="store_true")
    args = parser.parse_args(argv)

    if os.environ.get("FURY_UPDATE_BASELINES") == "1":
        args.update = True

    cfg = load_config(args.scenarios)
    scenarios = cfg.get("scenarios", {})
    if args.scenario not in scenarios:
        log("RESULT FAIL unknown scenario '%s' (have: %s)" % (
            args.scenario, ", ".join(sorted(scenarios))))
        return 1
    scenario = scenarios[args.scenario]

    ctx = {"repo": str(REPO), "examples": str(REPO / "examples"),
           "tests": str(Path(__file__).resolve().parent)}
    for req in scenario.get("requires", []):
        if not Path(resolve(req, ctx)).exists():
            log("RESULT FAIL missing precondition: " + resolve(req, ctx))
            return 1
    if not Path(args.binary).exists():
        log("RESULT FAIL missing binary: " + args.binary)
        return 1

    artifacts = Path(args.artifacts) if args.artifacts else \
        REPO / "test-artifacts" / ("render-%s-%s" % (args.scenario, args.backend))
    artifacts.mkdir(parents=True, exist_ok=True)

    defaults = cfg.get("defaults", {})
    for key in ("width", "height"):
        cfg[key] = scenario.get(key, defaults.get(key, 1280 if key == "width" else 720))

    actual = artifacts / ("%s.actual.png" % args.scenario)
    log_path = artifacts / ("%s.capture.log" % args.scenario)
    ok, text = run_capture(args, scenario, cfg, args.backend, False,
                           actual, log_path)
    if not ok:
        log("RESULT FAIL %s/%s: %s" % (args.scenario, args.backend, text))
        return 1
    if not actual.exists():
        log("RESULT FAIL %s/%s: no screenshot written (see %s)" % (
            args.scenario, args.backend, log_path))
        return 1
    meta, err = check_meta(text, args, cfg, args.backend)
    if err:
        log("RESULT FAIL %s/%s: %s" % (args.scenario, args.backend, err))
        return 1

    ok, detail = compare(args, scenario, cfg, args.backend, meta, artifacts)
    if ok:
        log("RESULT PASS %s/%s capture ok; %s" % (args.scenario, args.backend, detail))
    else:
        log("RESULT FAIL %s/%s %s" % (args.scenario, args.backend, detail))

    temporal_ok, temporal_detail = (True, "skipped") if args.no_temporal else \
        temporal_check(args, scenario, cfg, args.backend, artifacts)
    if temporal_ok:
        log("RESULT PASS %s/%s temporal: %s" % (args.scenario, args.backend, temporal_detail))
    else:
        log("RESULT FAIL %s/%s temporal: %s" % (args.scenario, args.backend, temporal_detail))

    return 0 if ok and temporal_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
