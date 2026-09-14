# CLAUDE.md

Notes for Claude Code (and other coding agents) working in this repo.

## Code style: comments

Write code as if it was always correct, then stop.

- **No history in comments.** No "previously X did Y so now we do Z", no "this fixes a bug where...", no "legacy" or "new" labels on code paths. The current code is the code.
- **One-liners or none.** Either a short single-line note above a non-obvious line, or no comment at all. Don't restate what the API name already says. No multi-paragraph essays.
- **Drop workarounds, then forget.** When removing a workaround, don't leave a comment explaining what it replaced. The replacement is self-evident from the code.
- **Rename, don't annotate.** If a function name no longer reflects what it does, rename it. Don't keep the stale name and add a comment.
- **ASCII only.** No em-dash, arrows, smart quotes, degree signs, multiplication sign, or ellipsis. Use `-`, `->`, `'...'`, `deg`, `x`, `...`.

## Code style: structure

- Match the surrounding code's density, naming, and idiom. Don't restyle adjacent code.
- Keep changes minimal and scoped to the task. No drive-by refactors.
- Comments are facts, not narratives. If a comment is needed, it states what the code does or why a non-obvious choice exists — not how the code was reached.

## Working in this repo

- **OpenSpec:** changes live in `openspec/changes/<name>/` with `proposal.md`, `design.md`, `specs/`, `tasks.md`. Use `openspec status --change <name> --json` and `openspec instructions apply --change <name> --json` to drive `/opsx:apply`.
- **Tests:** `tests/lua/*.lua` invoked headlessly via `./fury exec <scene> <script.lua>` from `examples/`.
- **Build dirs:** `build/` (normal), `build-asan/` (AddressSanitizer). Don't ship binaries from these — `examples/fury` and `examples/furye` are the outputs.
- **Memory:** auto-memory at `~/.claude/projects/-Users-sindney-Documents-git-furyengine-fury3d/memory/` persists across sessions. Add a new fact there only when it's not derivable from code/git.
