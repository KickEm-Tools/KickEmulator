#!/bin/sh
# Screenshot the running simulator by window id (works while it is covered by other windows).
#
#   Kick/tools/sim_shot.sh OUT.png            # the panel window ("Kick Simulator")
#   Kick/tools/sim_shot.sh --all OUT.png      # also every other window of the app (popups),
#                                             # saved as OUT-<n>.png
#
# The app's full-screen popups are separate windows (see docs/DEVELOPMENT.md 4.1), so a dialog
# does not appear in the panel capture; use --all when one is open.
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
ALL=0
[ "$1" = "--all" ] && { ALL=1; shift; }
OUT=${1:?usage: sim_shot.sh [--all] OUT.png}

PID=${KICK_SIM_PID:-$(cat "$TOOLS/../build/.sim_pid" 2>/dev/null || pgrep -x KT_KickEM | head -1)}
[ -n "$PID" ] && kill -0 "$PID" 2>/dev/null || { echo "KT_KickEM is not running" >&2; exit 1; }

# "<id>\t<name>" for each on-screen window of the process, panel first.
WINDOWS=$(osascript -l JavaScript - "$PID" <<'EOF'
ObjC.import("CoreGraphics");
function run(argv) {
    const pid = parseInt(argv[0]);
    const list = ObjC.castRefToObject($.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly, 0));
    const panel = [], others = [];
    for (let i = 0; i < list.count; i++) {
        const w = list.objectAtIndex(i);
        if (w.objectForKey("kCGWindowOwnerPID").js !== pid) continue;
        const n = w.objectForKey("kCGWindowName");
        const name = n.isNil() ? "" : n.js;
        const row = w.objectForKey("kCGWindowNumber").js + "\t" + name;
        (name === "Kick Simulator" ? panel : others).push(row);
    }
    return panel.concat(others).join("\n");
}
EOF
)

PANEL=$(printf '%s\n' "$WINDOWS" | head -1 | cut -f1)
[ -n "$PANEL" ] || { echo "no simulator window found" >&2; exit 1; }
screencapture -x -o -l "$PANEL" "$OUT"
echo "$OUT"

if [ "$ALL" = 1 ]; then
    n=1
    printf '%s\n' "$WINDOWS" | tail -n +2 | while IFS="	" read -r id name; do
        [ -n "$id" ] || continue
        f="${OUT%.png}-$n.png"
        screencapture -x -o -l "$id" "$f" && echo "$f"
        n=$((n + 1))
    done
fi
