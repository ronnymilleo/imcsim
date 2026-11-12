#!/usr/bin/env bash
# Tutorial, step 8: the operating point of a divider of four 1k resistors from 5 V, its wires colored by voltage
# from blue at ground to red at the source, with the scale at the bottom left; a crop of the canvas.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# How far the circuit is panned down, so it sits just over the scale, and the height of the crop above the bottom of
# the canvas
PAN_DOWN=250
CROP_HEIGHT=370
# The canvas ends this far above the bottom of the window, over the status bar
CANVAS_BOTTOM_MARGIN=40

docs_launch --wire-colors Voltage
docs_open_fixture divider
ui_drag "$DOCS_CANVAS_X" "$DOCS_CANVAS_Y" "$DOCS_CANVAS_X" $((DOCS_CANVAS_Y + PAN_DOWN)) middle
docs_pick_analysis Operating
docs_run
docs_park_pointer
canvas_bottom=$((DOCS_HEIGHT - CANVAS_BOTTOM_MARGIN))
docs_save tutorial_heat_map "$((DOCS_OUTPUT_X - CANVAS_LEFT - 15))x${CROP_HEIGHT}+${CANVAS_LEFT}+$((canvas_bottom - CROP_HEIGHT))"
