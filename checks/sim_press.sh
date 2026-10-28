#!/bin/sh
# Press simulator controls by their printed name through macOS accessibility.
#
#   Kick/tools/sim_press.sh "Pad 1" MAIN PUSH PLAY GRID "PAD MIX" E
#
# Key names are the captions on the panel: "Pad 1".."Pad 16", MAIN, BROWSE, "TRACK MIX",
# "TRACK MUTE", "NEXT SEQ", "TAP TEMPO", "NOTE REPEAT", MENU, SHIFT, "STEP SEQ", TC, SAMPLER,
# "SAMPLE EDIT", "PROG EDIT", "FULL LEVEL", "16 LEVELS", ERASE, LINK, A..D (banks), PUSH (the jog
# wheel click) and the transport keys RECORD, OVERDUB, STOP, PLAY, PLAYSTART.
#
# A name printed UNDER a key (GRID, SAVE, "PAD MIX", "PAD MUTE", FX, MASTER, LATCH, PREFS, AUTO,
# "ON/OFF", LOOP, "LINK EDIT", "MIDI CTRL", "HALF LVL", NOTES, E..H) is that key's SHIFT function:
# the script holds SHIFT, presses the key, and releases SHIFT, exactly as on the unit.
#
# The simulator (KT_KickEM) must already be running (sim_launch.sh records its pid; KICK_SIM_PID
# overrides), and the terminal must be allowed in
# System Settings > Privacy & Security > Accessibility.
set -e

TOOLS=$(cd "$(dirname "$0")" && pwd)
PID=${KICK_SIM_PID:-$(cat "$TOOLS/../build/.sim_pid" 2>/dev/null || pgrep -x KT_KickEM | head -1)}
[ -n "$PID" ] && kill -0 "$PID" 2>/dev/null || { echo "KT_KickEM is not running" >&2; exit 1; }

# Transport keys carry icons, not text, so their accessibility names are their ids.
transport_name() {
    case "$1" in
        RECORD|REC)    echo "key 42" ;;
        OVERDUB)       echo "key 43" ;;
        STOP)          echo "key 44" ;;
        PLAY)          echo "key 45" ;;
        PLAYSTART|"PLAY START") echo "key 46" ;;
        *)             echo "$1" ;;
    esac
}

# SHIFT function -> the key it is printed under.
shift_base() {
    case "$1" in
        GRID)        echo "MAIN" ;;
        SAVE)        echo "BROWSE" ;;
        "PAD MIX")   echo "TRACK MIX" ;;
        "PAD MUTE")  echo "TRACK MUTE" ;;
        FX)          echo "NEXT SEQ" ;;
        MASTER)      echo "TAP TEMPO" ;;
        LATCH)       echo "NOTE REPEAT" ;;
        PREFS)       echo "MENU" ;;
        AUTO)        echo "STEP SEQ" ;;
        "ON/OFF")    echo "TC" ;;
        LOOP)        echo "SAMPLER" ;;
        "LINK EDIT") echo "SAMPLE EDIT" ;;
        "MIDI CTRL") echo "PROG EDIT" ;;
        "HALF LVL")  echo "FULL LEVEL" ;;
        NOTES)       echo "16 LEVELS" ;;
        E) echo "A" ;;  F) echo "B" ;;  G) echo "C" ;;  H) echo "D" ;;
        *)           echo "" ;;
    esac
}

click() {
    osascript - "$PID" "$1" <<'EOF'
on run argv
    set pid to (item 1 of argv) as integer
    set btn to item 2 of argv
    tell application "System Events"
        set p to first process whose unix id is pid
        set frontmost of p to true
        -- Address the panel by name: the app opens its popups as extra windows.
        click button btn of window "Kick Simulator" of p
    end tell
end run
EOF
    sleep 0.3
}

for NAME in "$@"; do
    BASE=$(shift_base "$NAME")
    if [ -n "$BASE" ]; then
        click SHIFT            # hold
        click "$BASE"          # reported by the surface as the SHIFT function
        click SHIFT            # release
    else
        click "$(transport_name "$NAME")"
    fi
done
