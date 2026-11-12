#!/usr/bin/env bash
# Tutorial, step 3: the popover of the voltage source, set to Pulse with its default 0 to 5 V, 1 kHz settings,
# opened with a double click on the source of the rectifier.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture rectifier
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
# The bottom wire of fixtures/rectifier.imcsim runs from (4, 10) to (19, 10); Vin1 sits at (4, 7)
docs_calibrate_on_wire 4 19 10
ui_double_click $(docs_grid_point 4 7)
ui_move $((DOCS_OUTPUT_X - 40)) $((DOCS_HEIGHT - 80))
docs_save tutorial_properties
