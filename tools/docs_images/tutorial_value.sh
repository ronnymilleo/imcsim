#!/usr/bin/env bash
# Tutorial, step 6: 220 typed straight onto the selected load resistor R1 of the rectifier, after its transient,
# before Enter.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture rectifier_probed
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
docs_run
docs_calibrate_on_wire 4 19 10
# R1 stands at (19, 7)
ui_click $(docs_grid_point 19 7)
ui_text 220
docs_park_pointer
docs_save tutorial_value
