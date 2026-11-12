#!/usr/bin/env bash
# Tutorial, step 2: the part search of the command bar, opened with Space over the editor and given "dio", while
# the rectifier is being drawn (its source and ground are placed).

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture rectifier_start
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
# Space opens the search only while the pointer is over the editor
ui_click "$DOCS_CANVAS_X" $((DOCS_HEIGHT - 150))
ui_key space
ui_text dio
sleep "$MEDIUM_PAUSE"
docs_save tutorial_search
