#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Runs checks one after another for .github/workflows/test-firmware.yml and records the results.
#
#   .github/ci/run-checks.sh EVIDENCE "smoke transport ..." [arguments for every check...]
#
# Each check's evidence goes to EVIDENCE/<check>/ (its OUTDIR), its output to
# EVIDENCE/<check>/output.txt (and to the job log), and one line per check to EVIDENCE/results.tsv:
# check, status (pass, fail, skip, unknown), exit code, seconds. Exit status 0 when nothing failed.
# In GitHub Actions it also sets the step output result=pass|fail.
set -u
EV=$1
LIST=$2
shift 2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$EV"
: > "$EV/results.tsv"
FAILED=0
for c in $(printf '%s' "$LIST" | tr ',' ' '); do
    if [ ! -f "$ROOT/checks/$c" ] || [ ! -x "$ROOT/checks/$c" ]; then
        echo "::error::unknown check '$c': there is no executable checks/$c"
        printf '%s\tunknown\t2\t0\n' "$c" >> "$EV/results.tsv"
        FAILED=1
        continue
    fi
    mkdir -p "$EV/$c"
    echo "::group::checks/$c $*"
    t0=$(date +%s)
    "$ROOT/checks/$c" "$@" "$EV/$c" 2>&1 | tee "$EV/$c/output.txt"
    rc=${PIPESTATUS[0]}
    dt=$(( $(date +%s) - t0 ))
    echo "::endgroup::"
    case "$rc" in
        0) st=pass ;;
        3) st=skip ;;
        *) st=fail; FAILED=1 ;;
    esac
    printf '%s\t%s\t%s\t%s\n' "$c" "$st" "$rc" "$dt" >> "$EV/results.tsv"
    echo "checks/$c: $st (exit $rc, $dt s)"
    [ "$st" = fail ] && grep '^FAIL' "$EV/$c/output.txt" | sed "s/^/::error title=checks\/$c::/" | head -20
done
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    [ "$FAILED" -eq 0 ] && echo "result=pass" >> "$GITHUB_OUTPUT" || echo "result=fail" >> "$GITHUB_OUTPUT"
fi
exit "$FAILED"
