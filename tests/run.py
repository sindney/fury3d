#!/usr/bin/env python3
"""tests/run.py - aggregate test runner for the fury3d suite.

Runs the three automation lanes declared in tests/manifest.json:

  headless  `fury exec <scene> <script.lua> [args...]` from examples/ (no GPU)
  ui        editor UI automation via `furye-ui-tests`
  render    windowed `fury <script.lua>` captures, requires display + GPU

Also supports shell/python entries through the explicit `command` form.

Usage:
  python3 tests/run.py list [--manifest PATH] [--lane L] [--tag T]...
                            [--filter SUBSTR]...
  python3 tests/run.py run  [--manifest PATH] [--lane L] [--tag T]...
                            [--filter SUBSTR]... [--artifacts DIR]
                            [--fury PATH] [--furye PATH] [--furye-cli PATH]
                            [--timeout-multiplier F] [--summary PATH]
                            [--update-baselines]

Selection is AND across categories (lane/tag/filter) and OR within one
category. Filters match id, script, or command substrings.

Exit codes: 0 when nothing failed (pass and skipped both ok), 1 otherwise.

The runner only invokes existing binaries; it never builds and never writes
to examples/fury*. Test output lands in --artifacts (default
<repo>/test-artifacts/<run-id>): one log per test plus a summary.json.

Render-lane baseline scenarios (tests/render_baseline.py entries) compare
against approved per-backend baselines under tests/baselines/. A missing
baseline fails the entry without approving anything; --update-baselines
sets FURY_UPDATE_BASELINES=1 so those entries (re)approve baselines from
the current capture. Baseline approval is a review step: do not pass
--update-baselines in routine regression runs.
"""

import argparse
import json
import os
import platform
import shlex
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = REPO / "tests" / "manifest.json"
STATUS_PASS = "pass"
STATUS_FAIL = "fail"
STATUS_TIMEOUT = "timeout"
STATUS_SKIP = "skipped"


def log(msg):
    print(msg, flush=True)


def display_available():
    if platform.system() == "Darwin":
        try:
            out = subprocess.run(
                ["launchctl", "managername"],
                capture_output=True, text=True, timeout=5,
            )
            return "Aqua" in out.stdout
        except (OSError, subprocess.SubprocessError):
            return False
    return bool(os.environ.get("DISPLAY"))


def load_manifest(path):
    with open(path, "r", encoding="ascii") as fh:
        return json.load(fh)


def resolve_placeholders(value, ctx):
    if isinstance(value, str):
        for key, rep in ctx.items():
            value = value.replace("{" + key + "}", str(rep))
        return value
    if isinstance(value, list):
        return [resolve_placeholders(v, ctx) for v in value]
    if isinstance(value, dict):
        return {k: resolve_placeholders(v, ctx) for k, v in value.items()}
    return value


def selected(tests, lane, tags, filters):
    out = []
    for t in tests:
        if lane and t.get("lane") != lane:
            continue
        if tags and not (set(tags) & set(t.get("tags", []))):
            continue
        if filters:
            hay = " ".join([t.get("id", ""), t.get("script", ""),
                            " ".join(t.get("command", []))])
            if not any(f in hay for f in filters):
                continue
        out.append(t)
    return out


def check_preconditions(entry, binaries, ctx):
    """Return None if runnable, else a human-readable missing precondition."""
    if entry.get("display") and not display_available():
        return "display/GPU session unavailable (Aqua/DISPLAY)"
    for req in entry.get("requires", []):
        path = Path(resolve_placeholders(req, ctx))
        if not path.exists():
            return "missing precondition: " + str(path)
    if "command" not in entry:
        name = entry.get("binary", "fury")
        if not Path(binaries[name]).exists():
            return "missing binary: " + binaries[name]
    return None


def build_command(entry, binaries, ctx):
    args = resolve_placeholders(entry.get("args", []), ctx)
    if "command" in entry:
        cmd = resolve_placeholders(entry.get("command", []), ctx)
        return list(cmd) + args
    script = resolve_placeholders(entry["script"], ctx)
    lane = entry["lane"]
    if lane == "headless":
        scene = resolve_placeholders(entry["scene"], ctx)
        return [binaries["fury"], "exec", scene, script] + args
    binary = binaries[entry.get("binary", "furye" if lane == "ui" else "fury")]
    return [binary, script] + args


def run_one(entry, binaries, ctx, log_dir, timeout_multiplier):
    result = {
        "id": entry.get("id", "?"),
        "lane": entry.get("lane", "?"),
        "tags": entry.get("tags", []),
        "status": None,
        "reason": None,
        "returncode": None,
        "duration_s": None,
        "log": None,
    }
    missing = check_preconditions(entry, binaries, ctx)
    if missing:
        result["status"] = STATUS_SKIP
        result["reason"] = missing
        log("[SKIP]  %s: %s" % (result["id"], missing))
        return result

    cmd = build_command(entry, binaries, ctx)
    cwd = Path(resolve_placeholders(entry.get("cwd", "{examples}"), ctx))
    env = dict(os.environ)
    env.update(resolve_placeholders(entry.get("env", {}), ctx))
    env.setdefault("FURY_BIN", binaries["fury"])
    env.setdefault("FURY_CLI_BIN", binaries["furye-cli"])
    timeout = entry.get("timeout", ctx["default_timeout"])
    timeout = int(timeout * timeout_multiplier)
    expected = entry.get("expected_exit", 0)

    log_path = log_dir / (result["id"] + ".log")
    result["log"] = str(log_path)
    start = time.monotonic()
    with open(log_path, "w", encoding="utf-8", errors="replace") as lf:
        lf.write("$ " + " ".join(shlex.quote(c) for c in cmd) + "\n")
        lf.write("(cwd: %s)\n" % cwd)
        lf.flush()
        try:
            proc = subprocess.Popen(
                cmd, cwd=str(cwd), env=env,
                stdout=lf, stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        except OSError as exc:
            result["status"] = STATUS_FAIL
            result["reason"] = "failed to start: %s" % exc
            log("[FAIL]  %s: %s" % (result["id"], result["reason"]))
            return result
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError:
                proc.kill()
            proc.wait()
            rc = None
    result["duration_s"] = round(time.monotonic() - start, 3)
    result["returncode"] = rc
    if rc is None:
        result["status"] = STATUS_TIMEOUT
        result["reason"] = "timeout after %ss" % timeout
    elif rc != expected:
        result["status"] = STATUS_FAIL
        result["reason"] = "exit %s, expected %s" % (rc, expected)
    else:
        result["status"] = STATUS_PASS
        result["reason"] = "exit %s" % rc
    mark = {"pass": "[PASS] ", "fail": "[FAIL] ", "timeout": "[TMO ] "}.get(
        result["status"], "[????] ")
    log("%s%s (%s, %.1fs) %s" % (mark, result["id"], result["status"],
                                 result["duration_s"] or 0.0,
                                 result["reason"] or ""))
    return result


def cmd_list(args):
    manifest = load_manifest(args.manifest)
    tests = selected(manifest.get("tests", []), args.lane, args.tag,
                     args.filter)
    log("%d/%d tests selected" % (len(tests), len(manifest.get("tests", []))))
    for t in tests:
        target = t.get("script") or " ".join(t.get("command", []))
        scene = t.get("scene", "")
        log("  %-32s %-8s %ds  %s %s" % (
            t.get("id", "?"), t.get("lane", "?"),
            t.get("timeout", manifest.get("defaults", {}).get("timeout", 120)),
            scene, target))
    return 0


def cmd_run(args):
    manifest = load_manifest(args.manifest)
    defaults = manifest.get("defaults", {})
    binaries = dict(manifest.get("binaries", {}))
    binaries["fury"] = args.fury or binaries.get("fury", "examples/fury")
    binaries["furye"] = args.furye or binaries.get("furye", "examples/furye")
    binaries["furye-cli"] = (args.furye_cli or binaries.get("furye-cli")
                             or "examples/furye-cli")
    for name, path in binaries.items():
        p = Path(path)
        binaries[name] = str(p if p.is_absolute() else REPO / p)

    run_id = time.strftime("%Y%m%d-%H%M%S")
    artifacts = Path(args.artifacts).resolve() if args.artifacts else \
        REPO / "test-artifacts" / run_id
    artifacts.mkdir(parents=True, exist_ok=True)
    log_dir = artifacts / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)

    ctx = {
        "repo": str(REPO),
        "examples": str(REPO / "examples"),
        "tests": str(REPO / "tests"),
        "artifacts": str(artifacts),
        "default_timeout": defaults.get("timeout", 120),
    }

    tests = selected(manifest.get("tests", []), args.lane, args.tag,
                     args.filter)
    log("running %d test(s), artifacts in %s" % (len(tests), artifacts))
    results = [run_one(t, binaries, ctx, log_dir, args.timeout_multiplier)
               for t in tests]

    counts = {}
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    summary = {
        "runner": "tests/run.py",
        "manifest": str(Path(args.manifest).resolve()),
        "run_id": run_id,
        "artifacts": str(artifacts),
        "binaries": binaries,
        "selection": {"lane": args.lane, "tags": args.tag,
                      "filters": args.filter},
        "counts": counts,
        "results": results,
    }
    summary_path = artifacts / "summary.json"
    with open(summary_path, "w", encoding="ascii") as fh:
        json.dump(summary, fh, indent=2)
        fh.write("\n")
    if args.summary:
        Path(args.summary).write_text(
            json.dumps(summary, indent=2) + "\n", encoding="ascii")
    log("summary: %s" % summary_path)
    log("counts: %s" % json.dumps(counts, sort_keys=True))
    return 1 if counts.get(STATUS_FAIL) or counts.get(STATUS_TIMEOUT) else 0


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST),
                        help="manifest JSON path (default tests/manifest.json)")
    sub = parser.add_subparsers(dest="sub", required=True)

    def add_selectors(p):
        p.add_argument("--lane", choices=["headless", "ui", "render"])
        p.add_argument("--tag", action="append", default=[])
        p.add_argument("--filter", action="append", default=[])

    add_selectors(sub.add_parser("list", help="list selected tests"))
    run_p = sub.add_parser("run", help="run selected tests")
    add_selectors(run_p)
    run_p.add_argument("--artifacts", help="artifact directory")
    run_p.add_argument("--fury", help="path to fury binary")
    run_p.add_argument("--furye", help="path to furye binary")
    run_p.add_argument("--furye-cli", dest="furye_cli",
                       help="path to furye-cli binary")
    run_p.add_argument("--timeout-multiplier", type=float, default=1.0)
    run_p.add_argument("--summary", help="extra copy of the summary JSON")
    run_p.add_argument("--update-baselines", action="store_true",
                       help="let render baseline entries approve new baselines "
                            "(sets FURY_UPDATE_BASELINES=1 for child processes)")

    args = parser.parse_args(argv)
    if getattr(args, "update_baselines", False):
        os.environ["FURY_UPDATE_BASELINES"] = "1"
    if args.sub == "list":
        return cmd_list(args)
    return cmd_run(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
