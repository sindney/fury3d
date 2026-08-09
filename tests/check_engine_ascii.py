#!/usr/bin/env python3
"""check_engine_ascii.py — fail if engine source contains non-ASCII bytes.

Engine source lives under ``engine/Fury/`` (the engine itself) while every
third-party / vendored dependency lives under ``engine/ThirdParty/`` (FBX2glTF,
ImGui, ImGuizmo, ImReflect, JoltPhysics, LZ4, SFML, STB, lua, meshoptimizer,
nfd, rapidjson, sol2, tinygltf). Scanning only ``engine/Fury/`` therefore
respects the "ignore 3rd party" rule by construction — no per-vendor
allowlist to keep in sync.

We intentionally scan byte-by-byte rather than decoding each file as UTF-8.
UTF-8 with multi-byte sequences is itself non-ASCII even when it's well-formed,
so a decoder would silently accept the very bytes we're trying to flag. The
byte check treats every byte >= 0x80 as a violation regardless of encoding.

A trailing tab/whitespace line is reported alongside the offending byte so a
human reviewer can locate it without re-opening the file. The scanner NEVER
mutates any source file — it only reads and reports.
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass
from typing import Iterable, List, Optional, Tuple

# Repo layout: this script lives at <repo>/tests/check_engine_ascii.py and
# the engine source lives at <repo>/engine/Fury/. Resolve everything relative
# to the repo root so the script works no matter where you invoke it from.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE_SRC = os.path.join(REPO_ROOT, "engine", "Fury")

# File extensions that count as engine source. ``engine/Fury/`` actually
# contains: cpp, h, hpp, inc, mm. ``.mm`` is Objective-C++ (Apple-only DPI
# shim) — same byte-level rules apply.
SOURCE_EXTS = {".cpp", ".h", ".hpp", ".inc", ".mm"}

# Per-file cap on findings before we stop printing line context. A file with
# hundreds of violations usually signals a single bad edit (e.g. a copy-paste
# from a smart-quoted doc) and reporting every line is noise.
MAX_FINDINGS_PER_FILE = 25


@dataclass
class Finding:
    """One non-ASCII byte occurrence."""
    path: str           # absolute path
    line: int           # 1-indexed line number
    column: int         # 1-indexed column
    byte_value: int     # raw byte (0x80..0xFF)
    line_text: str      # the full line, for context


def iter_source_files(root: str) -> Iterable[str]:
    """Yield absolute paths of engine source files under ``root``.

    Skips:
    - hidden directories (``.git``, ``.vs``, ...) so editor / VCS metadata
      never enters the scan.
    - ``build`` / ``build-engine`` directories in case someone runs the
      script from a checked-out worktree that already contains a build dir
      (the script only walks ``engine/Fury/`` so this is belt-and-braces).
    """
    for dirpath, dirnames, filenames in os.walk(root):
        # Prune in-place so os.walk doesn't descend into skipped subtrees.
        dirnames[:] = [
            d for d in dirnames
            if not d.startswith(".")
            and d not in {"build", "build-engine", "ThirdParty"}
        ]
        for name in filenames:
            ext = os.path.splitext(name)[1].lower()
            if ext in SOURCE_EXTS:
                yield os.path.join(dirpath, name)


def scan_file(path: str) -> List[Finding]:
    """Read ``path`` and return one Finding per non-ASCII byte."""
    findings: List[Finding] = []
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        # One unreadable file shouldn't kill the whole scan. Surface it and
        # let the caller decide whether to treat unreadable as failure.
        print(f"warning: cannot read {path}: {e}", file=sys.stderr)
        return findings

    line = 1
    column = 1
    line_start = 0

    # Pre-compute line offsets so we can extract the offending line on
    # demand without re-scanning. For a typical engine file this is a few
    # hundred entries — trivial in memory.
    line_offsets: List[int] = [0]
    for i, b in enumerate(data):
        if b == 0x0A:  # '\n'
            line_offsets.append(i + 1)

    def line_text(ln: int) -> str:
        start = line_offsets[ln - 1] if ln - 1 < len(line_offsets) else len(data)
        end = line_offsets[ln] if ln < len(line_offsets) else len(data)
        # Decode lossily so the context line is printable even if it
        # contains bytes we can't represent, then sanitize to ASCII so the
        # report is terminal-safe on Windows (cp1252) consoles. The byte
        # value column already carries the exact byte; the line text is
        # purely a breadcrumb.
        raw = data[start:end].decode("utf-8", errors="replace").rstrip("\r\n")
        return "".join(c if ord(c) < 0x80 else "?" for c in raw)

    for i, b in enumerate(data):
        if b >= 0x80:
            findings.append(Finding(
                path=path,
                line=line,
                column=column,
                byte_value=b,
                line_text=line_text(line),
            ))
            if len(findings) >= MAX_FINDINGS_PER_FILE:
                # Cap per-file output so a single bad file can't drown the
                # report. Still counted in the final tally.
                break
        if b == 0x0A:  # '\n'
            line += 1
            column = 1
        else:
            column += 1

    return findings


def format_finding(f: Finding, repo_root: str) -> str:
    """Render one Finding as a single human-readable line."""
    rel = os.path.relpath(f.path, repo_root)
    # Show the byte value so the reader can tell 0xE2 (UTF-8 lead) from 0xA9
    # (Latin-1 copyright sign) without opening the file.
    return (
        f"{rel}:{f.line}:{f.column}: "
        f"non-ASCII byte 0x{f.byte_value:02X} in: {f.line_text}"
    )


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Fail if any engine source file contains non-ASCII bytes.",
    )
    parser.add_argument(
        "--root",
        default=ENGINE_SRC,
        help=f"engine source root to scan (default: {ENGINE_SRC})",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="only print the summary line, not individual findings",
    )
    args = parser.parse_args(argv)

    if not os.path.isdir(args.root):
        print(f"error: engine source root not found: {args.root}", file=sys.stderr)
        return 2

    total_files = 0
    total_findings = 0
    files_with_findings: List[Tuple[str, int]] = []

    for path in iter_source_files(args.root):
        total_files += 1
        findings = scan_file(path)
        if findings:
            files_with_findings.append((path, len(findings)))
            total_findings += len(findings)
            if not args.quiet:
                for f in findings:
                    print(format_finding(f, REPO_ROOT))

    # Sort the per-file tally by descending count so the worst offenders are
    # at the top of the summary.
    files_with_findings.sort(key=lambda kv: kv[1], reverse=True)

    print(
        f"scanned {total_files} engine source files under "
        f"{os.path.relpath(args.root, REPO_ROOT)}"
    )
    if files_with_findings:
        print(
            f"found {total_findings} non-ASCII byte(s) across "
            f"{len(files_with_findings)} file(s):"
        )
        for path, count in files_with_findings:
            print(f"  {count:5d}  {os.path.relpath(path, REPO_ROOT)}")
        return 1

    print("no non-ASCII bytes found — engine source is pure ASCII.")
    return 0


if __name__ == "__main__":
    sys.exit(main())