#!/usr/bin/env python3
"""Run named cloud, PreZ, and backend-parity visual regression cases.

Usage:
  python3 tests/render_regression.py [--case NAME]... [--backend BACKEND]
      [--binary examples/fury] [--cwd examples] [--artifacts DIR]

Case definitions live in tests/render_regressions.json. Captures, logs, and
metrics are written below the artifact directory. Exit 0 means every selected
case passed; unknown cases, missing inputs, capture failures, and metric
failures return 1.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TESTS = Path(__file__).resolve().parent
IMGPROBE = TESTS / "tools" / "imgprobe.py"


def log(message):
    print(message, flush=True)


def resolve(value, ctx):
    if isinstance(value, str):
        for key, replacement in ctx.items():
            value = value.replace("{" + key + "}", str(replacement))
        return value
    if isinstance(value, list):
        return [resolve(item, ctx) for item in value]
    if isinstance(value, dict):
        return {key: resolve(item, ctx) for key, item in value.items()}
    return value


def probe(command, *args):
    proc = subprocess.run(
        [sys.executable, str(IMGPROBE), command] + [str(arg) for arg in args],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError("imgprobe %s failed: %s" % (command, proc.stderr.strip()))
    return proc.stdout.strip()


def diff(a, b, region=None):
    args = [a, b] + ([region] if region else [])
    mean, maximum, frac = probe("diff", *args).split()
    return {"mean": float(mean), "max": int(maximum), "frac": float(frac)}


def run_capture(args, defaults, case, capture, out_dir, ctx):
    name = capture["name"]
    output = out_dir / (name + ".png")
    log_path = out_dir / (name + ".log")
    env = dict(os.environ)
    for values in (defaults.get("env", {}), case.get("env", {}), capture.get("env", {})):
        env.update(resolve(values, ctx))
    if case.get("camera"):
        env["FURY_CAM"] = case["camera"]

    backend = capture.get("backend", args.backend or defaults.get("backend", "opengl"))
    frame = capture.get("capture_frame", case.get(
        "capture_frame", defaults.get("capture_frame", 40)))
    screenshot = capture.get("screenshot", True)
    script = capture.get("script", case.get("script", defaults["script"]))
    scenario = capture.get("scenario", case.get("scenario"))
    cmd = [args.binary, resolve(script, ctx)]
    if scenario:
        cmd.append(scenario)
    cmd += ["--rhi=" + backend]
    if screenshot:
        cmd += ["--screenshot", str(output), "--screenshot-frame", str(frame)]
    timeout = capture.get("timeout", case.get("timeout", defaults.get("timeout", 240)))
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        log_file.write("$ " + " ".join(cmd) + "\n")
        log_file.flush()
        try:
            proc = subprocess.run(
                cmd, cwd=args.cwd, env=env,
                stdout=log_file, stderr=subprocess.STDOUT, timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            raise RuntimeError("%s timed out after %ss" % (name, timeout))
    text = log_path.read_text(encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise RuntimeError("%s exited %d (see %s)" % (name, proc.returncode, log_path))
    if not screenshot and "cloud_visual_check OK" not in text:
        raise RuntimeError("%s did not report completion (see %s)" % (name, log_path))
    if screenshot and not output.is_file():
        raise RuntimeError("%s produced no screenshot (see %s)" % (name, log_path))
    expected_clustered = capture.get("expect_clustered")
    if expected_clustered is not None and "clustered_effective=%d" % expected_clustered not in text:
        raise RuntimeError("%s did not reach clustered_effective=%s (see %s)" % (
            name, expected_clustered, log_path))
    return output if screenshot else None


def evaluate(case, images):
    kind = case["kind"]
    names = [capture["name"] for capture in case["captures"]]
    if kind == "self_skip":
        return {"clean_exit": True}, "clean exit without PreZ vegetation"
    if kind == "coverage_sweep":
        values = [float(probe("frac_nonblue", images[name], case["region"]))
                  for name in names]
        if not values[0] < values[1] < values[2]:
            raise RuntimeError("coverage is not monotonic: %s" % values)
        return {"frac_nonblue": values}, "frac_nonblue %.6f < %.6f < %.6f" % tuple(values)
    if kind == "tod_tint":
        cell = probe("pick_cloudy", images[names[0]], case["region"], case["grid"])
        noon = float(probe("rb_ratio", images[names[0]], cell))
        sunset = float(probe("rb_ratio", images[names[1]], cell))
        required = noon * case.get("min_ratio", 1.1)
        if sunset <= required:
            raise RuntimeError("R/B sunset %.6f must exceed %.6f" % (sunset, required))
        return {"cell": cell, "noon_rb": noon, "sunset_rb": sunset}, \
            "cloud cell %s R/B noon=%.6f sunset=%.6f" % (cell, noon, sunset)
    if kind == "occlusion":
        metrics = {}
        details = []
        for region in case["regions"]:
            value = diff(images[names[0]], images[names[1]], region["rect"])
            metrics[region["name"]] = value
            details.append("%s mean=%.4f" % (region["name"], value["mean"]))
            if "max_mean" in region and value["mean"] >= region["max_mean"]:
                raise RuntimeError("%s mean %.4f must be < %.4f" % (
                    region["name"], value["mean"], region["max_mean"]))
            if "min_mean" in region and value["mean"] <= region["min_mean"]:
                raise RuntimeError("%s mean %.4f must be > %.4f" % (
                    region["name"], value["mean"], region["min_mean"]))
        return metrics, "; ".join(details)
    if kind == "image_ab":
        value = diff(images[names[0]], images[names[1]])
        if value["mean"] >= case.get("max_mean", 0.5):
            raise RuntimeError("mean %.4f must be < %.4f" % (value["mean"], case["max_mean"]))
        if value["frac"] >= case.get("max_frac", 1.0):
            raise RuntimeError("frac %.6f must be < %.6f" % (value["frac"], case["max_frac"]))
        return value, "mean=%.4f max=%d frac>8=%.6f" % (
            value["mean"], value["max"], value["frac"])
    if kind == "backend_parity":
        floor = diff(images[names[0]], images[names[1]])
        cross = diff(images[names[0]], images[names[2]])
        if floor["mean"] != 0.0:
            raise RuntimeError("same-backend noise floor is %.4f, want 0" % floor["mean"])
        if cross["mean"] >= case["max_mean"] or cross["frac"] >= case["max_frac"]:
            raise RuntimeError("backend diff mean=%.4f frac=%.6f exceeds %.4f/%.6f" % (
                cross["mean"], cross["frac"], case["max_mean"], case["max_frac"]))
        return {"noise_floor": floor, "backend_diff": cross}, \
            "mean=%.4f max=%d frac>8=%.6f (floor 0)" % (
                cross["mean"], cross["max"], cross["frac"])
    raise RuntimeError("unknown case kind: " + kind)


def run_case(args, defaults, name, case, artifacts, ctx):
    out_dir = artifacts / name
    out_dir.mkdir(parents=True, exist_ok=True)
    result = {"id": name, "status": "fail", "metrics": None, "detail": None}
    try:
        images = {}
        for capture in case["captures"]:
            image = run_capture(args, defaults, case, capture, out_dir, ctx)
            if image is not None:
                images[capture["name"]] = image
        metrics, detail = evaluate(case, images)
        result.update(status="pass", metrics=metrics, detail=detail)
        log("RESULT PASS %s: %s" % (name, detail))
    except (KeyError, RuntimeError, OSError) as exc:
        result["detail"] = str(exc)
        log("RESULT FAIL %s: %s" % (name, exc))
    with open(out_dir / "result.json", "w", encoding="ascii") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    return result


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument("--backend")
    parser.add_argument("--binary", default=str(REPO / "examples" / "fury"))
    parser.add_argument("--cwd", default=str(REPO / "examples"))
    parser.add_argument("--config", default=str(TESTS / "render_regressions.json"))
    parser.add_argument("--artifacts", default=str(REPO / "test-artifacts" / "render-regressions"))
    args = parser.parse_args(argv)

    with open(args.config, "r", encoding="ascii") as config_file:
        config = json.load(config_file)
    cases = config["cases"]
    selected = args.case or list(cases)
    unknown = [name for name in selected if name not in cases]
    if unknown:
        log("RESULT FAIL unknown case(s): " + ", ".join(unknown))
        return 1
    if not Path(args.binary).is_file():
        log("RESULT FAIL missing binary: " + args.binary)
        return 1

    artifacts = Path(args.artifacts).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    ctx = {"repo": str(REPO), "examples": str(REPO / "examples"), "tests": str(TESTS)}
    results = [run_case(args, config["defaults"], name, cases[name], artifacts, ctx)
               for name in selected]
    summary = {
        "counts": {
            "pass": sum(result["status"] == "pass" for result in results),
            "fail": sum(result["status"] == "fail" for result in results),
        },
        "results": results,
    }
    with open(artifacts / "summary.json", "w", encoding="ascii") as output:
        json.dump(summary, output, indent=2)
        output.write("\n")
    return 1 if summary["counts"]["fail"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
