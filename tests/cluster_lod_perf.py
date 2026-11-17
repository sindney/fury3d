#!/usr/bin/env python3
"""cluster_lod_perf.py - task 5.6 legacy vs cluster-LOD perf comparison.

Measures the runtime bench line (FURY_CLUSTER_BENCH=1, emitted every 120
frames by DeferredPipeline; frames 1..120 averaged, uncapped via
FURY_MAX_FPS=0, screenshot at frame 120 ends the run) plus on-demand
cluster statistics from --gpu-dump at frame 30, for the feature tank
scene and Sponza, in legacy (FURY_CLUSTER_LOD=0) and cluster
(FURY_CLUSTER_LOD=1) modes.

GPU time is NOT available on the Metal backend (GpuZoneScope only wires
Vulkan/GL Tracy contexts; Metal GPU timestamps are unavailable on this
host). Historical Vulkan captures used MoltenVK emulation; current Vulkan
cluster rendering requires drawIndirectCount and uses legacy fallback on
MoltenVK. Native Vulkan MDI performance is unmeasured.

Sponza asset-eligibility note: its single mesh carries 103 submeshes,
above ClusterRenderer MaxSubmeshes (8), so every instance uses
the legacy fallback; cluster-mode numbers are expected to match legacy
and no 'Cluster diagnostics' line is emitted.

Usage:
  python3 -I tests/cluster_lod_perf.py --backend metal|vulkan|all
      [--binary examples/fury] [--cwd examples] [--artifacts DIR]
      [--tracy-capture build/tracy-capture/tracy-capture]
      [--csvexport build/tracy-csvexport/tracy-csvexport]
      [--tracy-seconds 15] [--skip-tracy] [--timeout 300]

Exit codes: 0 always (measurement failures are recorded per run, not
asserted); emits <artifacts>/results.json and <artifacts>/RESULTS.md.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

SCENES = {
    "feature": {
        "scene": "Projects/feature/feature.bin",
        "cam": "15,8,17,41.43,-15.82,5000",
        "note": "deterministic near tank pose (matches cluster_lod_render.py CAM_NEAR)",
    },
    "sponza": {
        "scene": "Projects/samples/sponza/Sponza.bin",
        "cam": "-48,3000,300,0,-75",
        "note": "bird's-eye over the courtyard; scene has one dim sun (intensity 0.3), "
                "mesh Mesh_0 has 103 submeshes > MaxSubmeshes(8): full legacy fallback",
    },
}

MODES = {"legacy": "0", "cluster": "1"}

BENCH_FRAME = 120
DUMP_FRAME = 30
BENCH_RE = re.compile(
    r"Cluster bench frame_cpu_ms=([0-9.eE+-]+) gbuffer_cpu_ms=([0-9.eE+-]+)"
    r" last_frame_cpu_ms=([0-9.eE+-]+) assign_ms=([0-9.eE+-]+)"
    r" draw_calls=(\d+) local_lights=(\d+) max_count=(\d+) overflow=(\d+)")
DIAG_RE = re.compile(
    r"Cluster diagnostics frame=(\d+) instances=(\d+) candidateInstances=(\d+)"
    r" fallbackInstances=(\d+) batches=(\d+) visibleInstances=(\d+) selected=(\d+)"
    r" visible=(\d+) commands=(\d+) drawnTriangles=(\d+)"
    r" selectionOverflow=(\d+) visibilityOverflow=(\d+)")

BACKEND_NOTES = {
    "metal": "native Metal (Apple M4 Pro)",
    "vulkan": "MoltenVK on Apple Silicon macOS - indirect count unsupported; "
              "GPU cluster rendering is unavailable and uses legacy fallback",
}

GPU_UNAVAILABLE = {
    "metal": "Metal backend has no Tracy GPU zone wiring (GpuZoneScope covers "
             "Vulkan/GL only) and Metal GPU timestamps are unavailable on this host",
    "vulkan": None,
}


def log(message):
    print(message, flush=True)


def run_fury(args, env_extra, extra_cmd, log_path):
    env = dict(os.environ)
    env.update(env_extra)
    cmd = [args.binary, "Player.lua", extra_cmd.pop("scene"), "--rhi=" + args.backend]
    cmd += extra_cmd["flags"]
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        log_file.write("$ " + " ".join(cmd) + "\n")
        log_file.flush()
        try:
            proc = subprocess.run(
                cmd, cwd=args.cwd, env=env,
                stdout=log_file, stderr=subprocess.STDOUT, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            return None, log_path.read_text(encoding="utf-8", errors="replace")
    return proc.returncode, log_path.read_text(encoding="utf-8", errors="replace")


def parse_bench(text):
    matches = BENCH_RE.findall(text)
    if not matches:
        return None
    m = matches[-1]
    keys = ("frame_cpu_ms", "gbuffer_cpu_ms", "last_frame_cpu_ms", "assign_ms",
            "draw_calls", "local_lights", "max_count", "overflow")
    out = {k: (float(v) if k.endswith("_ms") else int(v)) for k, v in zip(keys, m)}
    out["frames_averaged"] = BENCH_FRAME
    return out


def parse_diagnostics(text):
    m = DIAG_RE.search(text)
    if not m:
        return None
    keys = ("frame", "instances", "candidateInstances", "fallbackInstances",
            "batches", "visibleInstances", "selected", "visible", "commands",
            "drawnTriangles", "selectionOverflow", "visibilityOverflow")
    return {k: int(v) for k, v in zip(keys, m.groups())}


def bench_run(args, out_dir, scene_name, mode):
    env = {"FURY_CLUSTER_BENCH": "1", "FURY_MAX_FPS": "0",
           "FURY_CLUSTER_LOD": MODES[mode], "FURY_CAM": SCENES[scene_name]["cam"]}
    png = out_dir / ("frame%d_%s_%s.png" % (BENCH_FRAME, scene_name, mode))
    rc, text = run_fury(args, env,
                        {"scene": SCENES[scene_name]["scene"],
                         "flags": ["--screenshot", str(png),
                                   "--screenshot-frame", str(BENCH_FRAME)]},
                        out_dir / ("bench_%s_%s.log" % (scene_name, mode)))
    result = {"mode": mode, "env": env, "returncode": rc,
              "screenshot": str(png) if png.is_file() else None}
    if rc != 0:
        result["error"] = "fury exited %d" % rc
        return result
    bench = parse_bench(text)
    if bench is None:
        result["error"] = "no Cluster bench line (see log)"
        return result
    result["bench"] = bench
    return result


def dump_run(args, out_dir, scene_name, mode):
    env = {"FURY_MAX_FPS": "0", "FURY_CLUSTER_LOD": MODES[mode],
           "FURY_CAM": SCENES[scene_name]["cam"]}
    dump_dir = out_dir / ("dump_%s_%s" % (scene_name, mode))
    rc, text = run_fury(args, env,
                        {"scene": SCENES[scene_name]["scene"],
                         "flags": ["--gpu-dump", str(dump_dir),
                                   "--gpu-dump-frame", str(DUMP_FRAME)]},
                        out_dir / ("dump_%s_%s.log" % (scene_name, mode)))
    result = {"mode": mode, "env": env, "returncode": rc}
    if rc != 0:
        result["error"] = "fury exited %d" % rc
        return result
    diag = parse_diagnostics(text)
    result["cluster_diagnostics"] = diag
    if diag is None:
        if mode == "legacy":
            result["cluster_note"] = "cluster path inactive in legacy mode (no diagnostics expected)"
        else:
            result["cluster_note"] = (
                "no accepted cluster instances: Sponza Mesh_0 has 103 submeshes > "
                "ClusterRenderer MaxSubmeshes(8), whole mesh uses legacy fallback" if
                scene_name == "sponza" else
                "no cluster diagnostics despite cluster mode (see log)")
    return result


def tracy_run(args, out_dir, scene_name, mode):
    env = {"FURY_MAX_FPS": "0", "FURY_CLUSTER_LOD": MODES[mode],
           "FURY_CAM": SCENES[scene_name]["cam"]}
    trace = out_dir / ("tracy_%s_%s.tracy" % (scene_name, mode))
    log_path = out_dir / ("tracy_%s_%s.log" % (scene_name, mode))
    env_full = dict(os.environ)
    env_full.update(env)
    cmd = [args.binary, "Player.lua", SCENES[scene_name]["scene"],
           "--rhi=" + args.backend]
    result = {"mode": mode, "env": env, "trace": str(trace)}
    with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
        log_file.write("$ " + " ".join(cmd) + " & tracy-capture\n")
        log_file.flush()
        proc = subprocess.Popen(cmd, cwd=args.cwd, env=env_full,
                                stdout=log_file, stderr=subprocess.STDOUT)
        try:
            time.sleep(3)
            cap = subprocess.run(
                [args.tracy_capture, "-o", str(trace), "-f", "-s", str(args.tracy_seconds)],
                capture_output=True, text=True, timeout=args.tracy_seconds + 120)
            log_file.write(cap.stdout + cap.stderr)
        except subprocess.TimeoutExpired:
            result["error"] = "tracy-capture timed out"
        finally:
            proc.kill()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                pass
    if not trace.is_file():
        if "error" not in result:
            result["error"] = "no trace written (see %s)" % log_path
        return result
    try:
        exp = subprocess.run(
            [args.csvexport, "-g", str(trace)],
            capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        result["error"] = "csvexport timed out"
        return result
    if exp.returncode != 0:
        result["error"] = "csvexport failed: " + exp.stderr.strip()
        return result
    zones = {}
    for line in exp.stdout.splitlines()[1:]:
        parts = line.split(",")
        if len(parts) < 4:
            continue
        try:
            zones.setdefault(parts[0], []).append(int(parts[3]) / 1e6)
        except ValueError:
            continue
    warmup_drop = 30
    gbuffer = zones.get("pass_gbuffer", [])
    steady = gbuffer[warmup_drop:]
    if not steady:
        result["error"] = "no pass_gbuffer GPU zones in trace"
        result["gpu_zone_names"] = sorted(zones)
        return result
    result["gpu_time_ms"] = {
        "zone": "pass_gbuffer",
        "label": "MoltenVK-emulated Vulkan GPU timestamp (not native GPU perf)",
        "frames_averaged": len(steady),
        "mean_ms": statistics.fmean(steady),
        "median_ms": statistics.median(steady),
        "all_zone_means_ms": {k: statistics.fmean(v[warmup_drop:] or v)
                              for k, v in sorted(zones.items())},
    }
    return result


def measure_backend(args, backend):
    out_dir = Path(args.artifacts) / backend
    out_dir.mkdir(parents=True, exist_ok=True)
    entry = {"backend": backend, "backend_note": BACKEND_NOTES[backend],
             "scenes": {}}
    for scene_name, scene_info in SCENES.items():
        scene_entry = {"scene": scene_info["scene"], "camera": scene_info["cam"],
                       "camera_note": scene_info["note"], "modes": {}}
        for mode in MODES:
            log("[%s/%s/%s] bench run" % (backend, scene_name, mode))
            bench = bench_run(args, out_dir, scene_name, mode)
            log("[%s/%s/%s] gpu-dump run" % (backend, scene_name, mode))
            dump = dump_run(args, out_dir, scene_name, mode)
            mode_entry = {"bench_run": bench, "dump_run": dump}
            gpu_reason = GPU_UNAVAILABLE[backend]
            if gpu_reason is None and not args.skip_tracy:
                log("[%s/%s/%s] tracy capture (%ss)" % (backend, scene_name, mode, args.tracy_seconds))
                mode_entry["tracy_run"] = tracy_run(args, out_dir, scene_name, mode)
            elif gpu_reason is None:
                mode_entry["gpu_time_note"] = "skipped via --skip-tracy"
            else:
                mode_entry["gpu_time_note"] = gpu_reason
            scene_entry["modes"][mode] = mode_entry
        scene_entry["scene_command"] = (
            "%s Player.lua %s --rhi=%s --screenshot frame%d.png --screenshot-frame %d  "
            "(env: FURY_MAX_FPS=0 FURY_CLUSTER_BENCH=1 FURY_CLUSTER_LOD=<0|1> FURY_CAM=%s)" % (
                args.binary, scene_info["scene"], backend, BENCH_FRAME, BENCH_FRAME,
                scene_info["cam"]))
        scene_entry["dump_command"] = (
            "%s Player.lua %s --rhi=%s --gpu-dump <dir> --gpu-dump-frame %d  "
            "(env: FURY_MAX_FPS=0 FURY_CLUSTER_LOD=<0|1> FURY_CAM=%s)" % (
                args.binary, scene_info["scene"], backend, DUMP_FRAME, scene_info["cam"]))
        entry["scenes"][scene_name] = scene_entry
    return entry


def fmt_ms(v):
    return "%.3f" % v if isinstance(v, float) else str(v)


def write_markdown(path, results, args):
    lines = ["# Cluster LOD perf comparison (task 5.6)", "",
             "Host: macOS Apple M4 Pro. All runs uncapped (`FURY_MAX_FPS=0`), bench line",
             "averages frames 1..%d (`FURY_CLUSTER_BENCH=1`); screenshot at frame %d ends" % (BENCH_FRAME, BENCH_FRAME),
             "each bench run. Cluster stats from `--gpu-dump` at frame %d." % DUMP_FRAME,
             "",
             "Cross-run CPU variance on this host is high (single 1..120 average per",
             "launch; e.g. Vulkan Sponza legacy 5.0 ms vs cluster 2.4 ms with byte-identical",
             "work - both modes full legacy fallback). Treat frame_cpu_ms below as",
             "+/-50% unless replicated; the stable signals are draw_calls, cluster",
             "diagnostics counts, and the Tracy GPU-zone means (1400+ frames each).",
             "",
             "**GPU time policy**: Metal backend - GPU time unmeasured (no Tracy GPU",
             "zone wiring for Metal; Metal GPU timestamps unavailable on this host).",
             "Vulkan/MoltenVK cluster rendering is unsupported without indirect count.",
             "Prior Tracy zones are historical emulation only, not native Vulkan",
             "performance or evidence of a supported cluster path.",
             ""]
    for entry in results:
        backend = entry["backend"]
        lines += ["## Backend: %s" % backend, "", entry["backend_note"], ""]
        for scene_name, scene in entry["scenes"].items():
            lines += ["### %s - `%s`" % (scene_name, scene["scene"]), "",
                      "Camera `%s` (%s)" % (scene["camera"], scene["camera_note"]), "",
                      "```", scene["scene_command"], "```", ""]
            header = "| mode | frame_cpu_ms (1..%d avg) | gbuffer_cpu_ms | draw_calls |" % BENCH_FRAME
            lines += [header, "|---|---|---|---|"]
            for mode in MODES:
                m = scene["modes"][mode]
                bench = m["bench_run"].get("bench")
                if bench:
                    lines.append("| %s | %s | %s | %d |" % (
                        mode, fmt_ms(bench["frame_cpu_ms"]),
                        fmt_ms(bench["gbuffer_cpu_ms"]), bench["draw_calls"]))
                else:
                    lines.append("| %s | ERROR: %s | | |" % (mode, m["bench_run"].get("error")))
            lines += ["", "Cluster stats (`--gpu-dump` frame %d):" % DUMP_FRAME, "",
                      "| mode | diagnostics | batches | indirect commands | selected | drawn triangles | fallback instances |",
                      "|---|---|---|---|---|---|---|"]
            for mode in MODES:
                m = scene["modes"][mode]
                diag = m["dump_run"].get("cluster_diagnostics")
                if diag:
                    lines.append("| %s | yes | %d | %d | %d | %d | %d |" % (
                        mode, diag["batches"], diag["commands"], diag["selected"],
                        diag["drawnTriangles"], diag["fallbackInstances"]))
                else:
                    lines.append("| %s | none (%s) | - | - | - | - | - |" % (
                        mode, m["dump_run"].get("cluster_note", "n/a")))
            lines += ["", "GPU time:", ""]
            for mode in MODES:
                m = scene["modes"][mode]
                tracy = m.get("tracy_run") or {}
                gpu = tracy.get("gpu_time_ms")
                if gpu:
                    lines.append("- %s: pass_gbuffer GPU mean=%.3f ms, median=%.3f ms "
                                 "(%s, %d frames, warmup drop %d) [%s]" % (
                                     mode, gpu["mean_ms"], gpu["median_ms"],
                                     gpu["label"], gpu["frames_averaged"], 30,
                                     tracy.get("trace")))
                else:
                    note = tracy.get("error") or m.get("gpu_time_note", "unmeasured")
                    lines.append("- %s: unmeasured - %s" % (mode, note))
            lines.append("")
    lines += ["## Exact bench command template", "", "```bash",
              "# from %s, one run per (scene, mode); FURY_CLUSTER_LOD=0 legacy, =1 cluster" % args.cwd,
              "FURY_CLUSTER_BENCH=1 FURY_MAX_FPS=0 FURY_CLUSTER_LOD=<0|1> FURY_CAM=<pose> \\",
              "  %s Player.lua <scene.bin> --rhi=<backend> --screenshot frame%d.png --screenshot-frame %d" % (args.binary, BENCH_FRAME, BENCH_FRAME),
              "```", ""]
    path.write_text("\n".join(lines), encoding="ascii", errors="replace")


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--backend", required=True, choices=["metal", "vulkan", "all"])
    parser.add_argument("--binary", default=str(REPO / "examples" / "fury"))
    parser.add_argument("--cwd", default=str(REPO / "examples"))
    parser.add_argument("--artifacts", default=str(
        REPO / "test-artifacts" / "cluster-lod-perf"))
    parser.add_argument("--tracy-capture", default=str(
        REPO / "build" / "tracy-capture" / "tracy-capture"))
    parser.add_argument("--csvexport", default=str(
        REPO / "build" / "tracy-csvexport" / "tracy-csvexport"))
    parser.add_argument("--tracy-seconds", type=int, default=15)
    parser.add_argument("--skip-tracy", action="store_true")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args(argv)

    if not Path(args.binary).is_file():
        log("missing binary: " + args.binary)
        return 1
    backends = ["metal", "vulkan"] if args.backend == "all" else [args.backend]
    need_tracy = (not args.skip_tracy) and any(
        GPU_UNAVAILABLE[b] is None for b in backends)
    if need_tracy:
        for tool in (args.tracy_capture, args.csvexport):
            if not Path(tool).is_file():
                log("missing tracy tool: " + tool)
                return 1

    artifacts = Path(args.artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)
    results = [measure_backend(args, b) for b in backends]
    # Merge with prior single-backend runs so results.json covers all
    # measured backends (each --backend invocation only re-measures its own).
    prior = {}
    results_path = artifacts / "results.json"
    if results_path.is_file():
        try:
            prior = {e["backend"]: e for e in
                     json.loads(results_path.read_text(encoding="ascii")).get("backends", [])}
        except (ValueError, KeyError):
            prior = {}
    prior.update({e["backend"]: e for e in results})
    merged = [prior[b] for b in ("metal", "vulkan") if b in prior] or results
    summary = {
        "task": "add-gpu-cluster-lod 5.6",
        "host": "macOS Apple M4 Pro (Metal native; Vulkan/MoltenVK cluster LOD unsupported)",
        "bench_frame": BENCH_FRAME,
        "dump_frame": DUMP_FRAME,
        "gpu_time_policy": "Metal: unmeasured (no GPU zone wiring, Metal timestamps "
                           "unavailable). Vulkan: Tracy pass_gbuffer, MoltenVK-emulated, "
                           "not native GPU perf. No GPU time inferred from CPU timing.",
        "backends": merged,
    }
    with open(results_path, "w", encoding="ascii") as output:
        json.dump(summary, output, indent=2)
        output.write("\n")
    write_markdown(artifacts / "RESULTS.md", merged, args)
    log("wrote %s and %s" % (results_path, artifacts / "RESULTS.md"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
