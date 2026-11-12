#!/usr/bin/env bash
# README: the RC low-pass example after its transient, its probed nodes and their voltages at the playhead in the
# editor, and the transient of both nodes with their statistics in the Output window.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# How far right of the "Loop" label to click on the playback slider, about two thirds of the run
PLAYHEAD_FROM_LOOP=270

docs_launch
docs_open_example rc_low_pass_filter
docs_pick_analysis Transient
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
docs_run
read -r loop_x loop_y < <(docs_find_text Loop "$DOCS_OUTPUT_X" $((DOCS_HEIGHT / 2)) "$DOCS_SIDE_X" "$DOCS_HEIGHT")
ui_click $((loop_x + PLAYHEAD_FROM_LOOP)) "$loop_y"
docs_park_pointer
docs_save screenshot
