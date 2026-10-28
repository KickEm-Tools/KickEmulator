#!/bin/sh
# Turn the simulator's encoders and move its slider.
#
#   Kick/tools/sim_turn.sh P1 3        # Q-Link P1, 3 detents clockwise
#   Kick/tools/sim_turn.sh JOG -2      # jog wheel, 2 detents anticlockwise
#   Kick/tools/sim_turn.sh SLIDER 40   # slider to 40 (0..100, absolute)
#
# Encoders (P1..P4, JOG) are endless; the app receives a knob id, a direction and a step count.
# Each detent is sent as one EN_PKT_TYPE_KNOBS packet of magnitude 1 (cmd EN_KNOB_QLINK for
# P1-P4, EN_KNOB_ROTARY for the jog wheel), the packet a mouse drag on the knob sends per step.
# The slider sends one EN_PKT_TYPE_SLIDER / EN_SLIDER_TOUCH_INPUT packet with the value.
# Shorthand for "sim_ctl.sh turn" / "sim_ctl.sh slider" (in-app channel, works while locked).
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
KNOB=${1:?usage: sim_turn.sh P1..P4|JOG STEPS  or  sim_turn.sh SLIDER 0..100}
AMOUNT=${2:?usage: sim_turn.sh P1..P4|JOG STEPS  or  sim_turn.sh SLIDER 0..100}
case "$KNOB" in
    SLIDER|slider) exec "$TOOLS/sim_ctl.sh" slider "$AMOUNT" ;;
    *)             exec "$TOOLS/sim_ctl.sh" turn "$KNOB" "$AMOUNT" ;;
esac
