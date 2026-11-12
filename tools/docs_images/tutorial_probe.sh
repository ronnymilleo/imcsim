#!/usr/bin/env bash
# Tutorial, step 4: probe mode on the rectifier, with the current of D1 and the output voltage V(2) marked in the
# colors of their traces.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture rectifier_probed
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
ui_click "$DOCS_CANVAS_X" $((DOCS_HEIGHT - 150))
ui_key p
docs_park_pointer
docs_save tutorial_probe
