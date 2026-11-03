#!/usr/bin/env bash
# tests/ui_failure_probe.sh <artifact-dir>
# Runs the intentional UI failure probe and validates the failure evidence:
# non-zero exit, a JUnit entry identifying the failed test, and a capture PNG
# under the artifact dir. Exits 0 when the evidence is complete.
set -u

ARTIFACTS="${1:?usage: ui_failure_probe.sh <artifact-dir>}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
EXAMPLES="$REPO/examples"
mkdir -p "$ARTIFACTS"

rm -rf "$ARTIFACTS/imgui.ini" "$ARTIFACTS/junit.xml" "$ARTIFACTS/captures"

cd "$EXAMPLES" || exit 1
"./furye-ui-tests" Editor.lua --ui-test --render-thread=0 \
    --ui-test-group=perf --ui-test-filter failure_probe \
    --ui-test-artifacts "$ARTIFACTS" --ui-test-timeout 60 \
    > "$ARTIFACTS/probe.log" 2>&1
rc=$?

fail() { echo "ui_failure_probe FAIL: $1" >&2; exit 1; }

[ "$rc" -ne 0 ] || fail "probe exit $rc, expected non-zero"
grep -q 'failure_probe' "$ARTIFACTS/junit.xml" || fail "junit.xml missing failure_probe entry"
grep -q '<failure' "$ARTIFACTS/junit.xml" || fail "junit.xml has no failure record"
ls "$ARTIFACTS"/captures/*.png > /dev/null 2>&1 || fail "no capture PNG under $ARTIFACTS/captures"

echo "ui_failure_probe OK: exit=$rc, JUnit failure + capture present"
