#!/bin/sh
# Touch the simulator's screen: click on-screen items (tabs, buttons) rather than panel keys.
#
#   Kick/tools/sim_click.sh NAME...          # click the first accessible element titled NAME
#   Kick/tools/sim_click.sh --at X Y         # click at pixel (X,Y) of the 1024x600 Kick screen
#   Kick/tools/sim_click.sh --list [FILTER]  # print accessible elements: role, title, desc, frame
#
# NAME matches a JUCE component's accessibility title or description exactly, e.g. the
# Preferences tabs "Info", "Hardware", "CV", "Ethernet", "WiFi", or the header button
# "Resources". Every window of the app is searched, so popups are reachable too. --at maps a
# point on the Kick screen to wherever the panel currently draws it, for untitled components.
# Both post a real mouse click (CGEvent), like a finger on the touchscreen.
#
# The helper is a small Swift program (sim_ax.swift) compiled on first use into
# Kick/build/sim_ax. Same requirements as sim_press.sh: the simulator is running
# (sim_launch.sh) and the terminal has Accessibility permission.
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
BUILD="$TOOLS/../build"
PID=${KICK_SIM_PID:-$(cat "$BUILD/.sim_pid" 2>/dev/null || pgrep -x KT_KickEM | head -1)}
[ -n "$PID" ] && kill -0 "$PID" 2>/dev/null || { echo "KT_KickEM is not running" >&2; exit 1; }

AX="$BUILD/sim_ax"
if [ ! -x "$AX" ] || [ "$TOOLS/sim_ax.swift" -nt "$AX" ]; then
    swiftc -O -o "$AX" "$TOOLS/sim_ax.swift"
fi

case "$1" in
    --list) "$AX" "$PID" list "$2" ;;
    --at)   "$AX" "$PID" at "$2" "$3"; sleep 0.3 ;;
    *)      for NAME in "$@"; do "$AX" "$PID" click "$NAME"; sleep 0.3; done ;;
esac
