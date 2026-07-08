#!/usr/bin/env python3
"""lua_api_docgen.py — emit docs/LUA_API.md from engine/Fury/LuaBindings.cpp.

The scanner is a hand-rolled regex over LuaBindings.cpp. The file has a
regular structure (one `lua.new_usertype<T>(...)` / `lua.create_named_table(...)`
call per binding, with member names registered as `"name", value` pairs in the
usertype body), so libclang's AST walk would be overkill. Output is deterministic
(bindings in registration order, members in source order) so re-running on the
same input produces byte-identical docs/LUA_API.md.

If a binding can't be fully introspected (e.g., a complex lambda-bound member
that we can't classify as data vs function), the scanner emits a
`<!-- docgen: unable to introspect, see LuaBindings.cpp:LINE -->` marker at
that binding's section so the gap is visible and reviewable. The scanner
NEVER aborts on parse errors — it always emits a complete document.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


@dataclass
class Member:
    """One member within a usertype or namespace table."""
    name: str
    line: int
    # A best-effort signature hint: "function", "property", "overload", "lambda",
    # "value", "unparseable". Helps humans reading the doc but never crashes
    # the generator if the hint is wrong.
    kind: str = "unparseable"
    # Free-form hint from the scanner (e.g. "lambda" or "overload").
    detail: str = ""


@dataclass
class Binding:
    """One usertype or namespace table registered on the lua state."""
    name: str
    line: int
    # "usertype" or "namespace".
    kind: str
    members: List[Member] = field(default_factory=list)
    # Set when the scanner can't find the closing paren — `members` will be
    # empty and the doc will include an "unable to introspect" marker.
    truncated: bool = False


# ----- scanner --------------------------------------------------------------

# Match a usertype registration. We don't try to recover the C++ template
# type — sol2 lets the Lua-side name diverge ("Vector4" vs "Vec4"), so the
# string literal in the second argument is the source of truth.
#
# Captures: (line, name)
USERTYPE_RE = re.compile(
    r'lua\.new_usertype<[^>]+>\(\s*"([^"]+)"',
    re.MULTILINE,
)

NAMESPACE_TABLE_RE = re.compile(
    r'(?:sol::table\s+\w+\s*=\s*)?lua\.create_named_table\(\s*"([^"]+)"\s*\)',
    re.MULTILINE,
)

# Match `"member", value` pairs inside a usertype body. The value is allowed
# to be:
#   - &T::method  (member pointer)
#   - sol::property(...)  (property getter/setter)
#   - sol::overload(...)  (overloaded function set)
#   - sol::call_constructor / sol::no_constructor / sol::base_classes,
#     sol::meta_function::*, sol::constructors<...>  (control keys)
#   - sol::meta_function::* (operator overloads)
#   - "[name]", value  (assignment in a namespace table)
#   - Anything else  (lambda, computed, etc.)
MEMBER_PAIR_RE = re.compile(
    r'"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([^,()]+(?:\([^)]*\))?|\([^)]*\))',
    re.MULTILINE,
)


def find_binding_body(src: str, open_paren_idx: int) -> Tuple[int, int, bool]:
    """Find the matching close paren for the binding call at `open_paren_idx`.

    Returns (body_start, body_end, ok). `body_start` is the index right after
    the opening paren; `body_end` is the index of the matching close paren.
    `ok` is False if the parens don't balance — caller should mark the
    binding truncated.
    """
    depth = 1
    i = open_paren_idx + 1
    while i < len(src):
        c = src[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return open_paren_idx + 1, i, True
        elif c == '"':
            # skip string literals (no embedded " in our file)
            j = src.find('"', i + 1)
            if j == -1:
                return open_paren_idx + 1, len(src), False
            i = j
        i += 1
    return open_paren_idx + 1, len(src), False


def classify_member(value: str) -> Tuple[str, str]:
    """Return (kind, detail) for the rhs of a `"name", value` pair.

    Used to label the member in the output table. Pure classification —
    never raises.
    """
    v = value.strip()
    if v.startswith('&'):
        # Member pointer like `&Mesh::GetName`. Treat as function.
        # Strip the leading `&` and the leading class qualifier (`Class::`)
        # to leave just the method name as the detail.
        m = re.match(r'&[A-Za-z_][A-Za-z0-9_]*::([A-Za-z_][A-Za-z0-9_]*)', v)
        if m:
            return "function", m.group(1)
        return "function", v[1:]
    if v.startswith('sol::property'):
        return "property", "getter/setter"
    if v.startswith('sol::overload'):
        return "function", "overloaded"
    if v.startswith('sol::call_constructor'):
        return "control", "call_constructor"
    if v.startswith('sol::no_constructor'):
        return "control", "no_constructor"
    if v.startswith('sol::base_classes'):
        return "control", "base_classes"
    if v.startswith('sol::constructors'):
        return "control", "constructors"
    if v.startswith('sol::meta_function'):
        return "operator", v[len('sol::meta_function::'):].strip()
    if v.startswith('sol::'):
        return "unparseable", v.split('(')[0]
    if v.startswith('[') or v.startswith('lua::'):
        return "lambda", "lua-side"
    if v.startswith('static_cast<'):
        return "function", "static_cast"
    if v.startswith('"'):
        return "value", "string"
    # Bare literal number or identifier — likely a value assignment.
    if re.match(r'^-?\d+(?:\.\d+)?$', v):
        return "value", "numeric"
    # Otherwise we don't know what this is; mark unparseable but don't fail.
    return "unparseable", v[:40]


def scan(src_path: str) -> List[Binding]:
    src = open(src_path).read()
    bindings: List[Binding] = []
    seen_lines: set = set()

    def line_of(idx: int) -> int:
        return src.count('\n', 0, idx) + 1

    def find_value_end(body: str, start: int) -> int:
        """Given an index just past the comma after a `"name",` pair,
        return the index just after the corresponding value (a single
        expression — identifier, parenthesized call, lambda, etc.)."""
        depth_paren = 0
        depth_angle = 0
        i = start
        while i < len(body):
            c = body[i]
            if c == '(':
                depth_paren += 1
            elif c == ')':
                if depth_paren == 0:
                    return i
                depth_paren -= 1
            elif c == '<':
                depth_angle += 1
            elif c == '>':
                if depth_angle > 0:
                    depth_angle -= 1
            elif c == '"':
                j = body.find('"', i + 1)
                if j == -1:
                    return len(body)
                i = j
                continue
            elif c == '[' and depth_paren == 0:
                # Lambda capture list: `[&]` or `[&lua]` — treat as opaque
                # until matching `]` so we don't stop at the first comma
                # inside the capture spec.
                depth_sq = 1
                i += 1
                while i < len(body) and depth_sq > 0:
                    if body[i] == '[':
                        depth_sq += 1
                    elif body[i] == ']':
                        depth_sq -= 1
                    i += 1
                continue
            elif c == ',' and depth_paren == 0 and depth_angle == 0:
                return i
            i += 1
        return len(body)

    def extract_pairs(body: str, body_offset: int):
        """Yield (name, value, line) for each `"name", value` pair inside
        the usertype body."""
        i = 0
        while i < len(body):
            # Skip whitespace and comments so we don't grab `"comment"`.
            while i < len(body) and body[i] in ' \t\r\n':
                i += 1
            if i >= len(body):
                break
            if body[i] == '/' and i + 1 < len(body) and body[i + 1] == '/':
                # line comment
                nl = body.find('\n', i)
                i = nl + 1 if nl != -1 else len(body)
                continue
            if body[i] == '/' and i + 1 < len(body) and body[i + 1] == '*':
                # block comment
                end = body.find('*/', i + 2)
                i = end + 2 if end != -1 else len(body)
                continue
            if body[i] != '"':
                # Skip stray characters (operators, identifiers) until the
                # next string literal. This keeps us in step with the actual
                # registration arguments.
                i += 1
                continue
            # Read the string literal.
            j = body.find('"', i + 1)
            if j == -1:
                return
            member_name = body[i + 1:j]
            i = j + 1
            # Skip whitespace.
            while i < len(body) and body[i] in ' \t\r\n':
                i += 1
            if i >= len(body) or body[i] != ',':
                # Not a `name, value` pair; skip past this token.
                continue
            i += 1
            # Skip whitespace.
            while i < len(body) and body[i] in ' \t\r\n':
                i += 1
            value_start = i
            value_end = find_value_end(body, i)
            value = body[value_start:value_end].strip()
            yield (member_name, value, line_of(body_offset + i))
            i = value_end
            # Skip trailing comma.
            if i < len(body) and body[i] == ',':
                i += 1

    # ---- usertypes -------------------------------------------------------
    for m in USERTYPE_RE.finditer(src):
        name = m.group(1)
        line = line_of(m.start())
        # The opening paren of `new_usertype<T>("Name", ...)` sits between
        # the closing `>` of the template and the opening `"` of the name
        # literal. m.start(1) is the first char of the captured name, so
        # walk back from there past the opening quote to find the paren.
        name_open = m.start(1)
        k = src.rfind('(', m.start(), name_open)
        if k == -1:
            bindings.append(Binding(name=name, line=line, kind="usertype", truncated=True))
            seen_lines.add(line)
            continue
        open_idx = k
        body_start, body_end, ok = find_binding_body(src, open_idx)
        body = src[body_start:body_end]
        b = Binding(name=name, line=line, kind="usertype", truncated=not ok)
        if ok:
            for member_name, value, member_line in extract_pairs(body, body_start):
                # Skip sol:: control keys.
                if value.startswith('sol::'):
                    continue
                kind, detail = classify_member(value)
                b.members.append(Member(
                    name=member_name,
                    line=member_line,
                    kind=kind,
                    detail=detail,
                ))
        bindings.append(b)
        seen_lines.add(line)

    # ---- namespace tables ------------------------------------------------
    for m in NAMESPACE_TABLE_RE.finditer(src):
        name = m.group(1)
        line = line_of(m.start())
        if line in seen_lines:
            continue
        # Namespace tables are populated by subsequent `name_tbl["foo"] = ...`
        # assignments in source order. We collect them by walking forward
        # from the registration line, scoping to the same file.
        ns = Binding(name=name, line=line, kind="namespace")
        # Pattern: variable_name["member"] = <value>
        # Find the variable name used in the create_named_table call.
        # For `sol::table foo_tbl = lua.create_named_table("X")` the variable
        # is `foo_tbl`. For the bare `lua.create_named_table("X")` form there
        # is no local variable — emit a marker instead.
        before = src[:m.start()]
        var_match = re.search(r'(\w+)\s*=\s*$', before)
        ns.truncated = var_match is None
        if not ns.truncated:
            var_name = var_match.group(1)
            # Find all subsequent `var_name["member"] = value;` lines.
            for am in re.finditer(
                re.escape(var_name) + r'\[\s*"([^"]+)"\s*\]\s*=\s*([^;]+);',
                src[m.end():],
                re.MULTILINE,
            ):
                member_name = am.group(1)
                value = am.group(2).strip()
                # Strip trailing lambda closing brace + comment so the
                # signature hint is the leading token.
                value_head = value.split('{', 1)[0].split('(', 1)[0].strip()
                kind, detail = classify_member(value)
                if detail == 'unparseable' and value_head:
                    detail = value_head
                ns.members.append(Member(
                    name=member_name,
                    line=line_of(m.end() + am.start()),
                    kind=kind,
                    detail=detail,
                ))
        bindings.append(ns)
        seen_lines.add(line)

    # Sort by source line for deterministic registration-order output.
    bindings.sort(key=lambda b: b.line)
    return bindings


# ----- emitter --------------------------------------------------------------

def emit(bindings: List[Binding], src_path: str) -> str:
    out: List[str] = []
    out.append("# Lua API reference")
    out.append("")
    out.append("> Auto-generated by `engine/Tools/lua_api_docgen.py` from")
    out.append("> `engine/Fury/LuaBindings.cpp`. Do not edit by hand — the build")
    out.append("> regenerates this file on every change to the bindings source.")
    out.append("> If a binding is missing here, look at the source file directly;")
    out.append("> the docgen scanner intentionally skips sections it cannot")
    out.append("> introspect and emits an `unable to introspect` marker.")
    out.append("")
    out.append(f"Source: `{src_path}`")
    out.append("")
    out.append("---")
    out.append("")

    for b in bindings:
        header = f"## `{b.name}`"
        if b.kind == "usertype":
            header += " (usertype)"
        else:
            header += " (namespace table)"
        out.append(header)
        out.append("")
        out.append(f"_Registered at `{src_path}:{b.line}`._")
        out.append("")

        if b.truncated:
            out.append(f"<!-- docgen: unable to introspect, see {src_path}:{b.line} -->")
            out.append("")
            continue

        if not b.members:
            out.append("_No members registered._")
            out.append("")
            continue

        out.append("| Member | Kind | Signature hint |")
        out.append("|--------|------|----------------|")
        for m in b.members:
            out.append(f"| `{m.name}` | {m.kind} | {m.detail or '—'} |")
        out.append("")

    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--src", default="engine/Fury/LuaBindings.cpp",
        help="Path to LuaBindings.cpp (relative to repo root or absolute).")
    ap.add_argument("--out", default="docs/LUA_API.md",
        help="Path to write the generated Markdown to.")
    args = ap.parse_args()

    src_path = os.path.abspath(args.src)
    if not os.path.exists(src_path):
        # Try resolving relative to the script's repo root (one up from Tools/).
        repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))))
        candidate = os.path.join(repo_root, args.src)
        if os.path.exists(candidate):
            src_path = candidate
        else:
            print(f"lua_api_docgen: source not found: {args.src}", file=sys.stderr)
            return 1

    bindings = scan(src_path)
    text = emit(bindings, src_path)

    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as f:
        f.write(text)
    print(f"lua_api_docgen: wrote {out_path} ({len(bindings)} bindings)")
    return 0


if __name__ == "__main__":
    sys.exit(main())