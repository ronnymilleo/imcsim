#!/usr/bin/env bash
# Tutorial, step 9: the plot export options, opened with Export... over the transient of the rectifier with its
# math channel; a crop of the Output window from its tab bar to the bottom of the options.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# Height of the crop below the tab bar of the Output window, enough for the export options
CROP_HEIGHT=260

docs_launch
docs_open_fixture rectifier_math
docs_run
ui_click_text "Export.*" "$DOCS_OUTPUT_X" "$COMMAND_BAR_BOTTOM" "$DOCS_SIDE_X" 200
docs_park_pointer
# From the left edge of the Output window to its right edge, under its title tab
docs_save tutorial_export "$((DOCS_SIDE_X - DOCS_OUTPUT_X))x${CROP_HEIGHT}+${DOCS_OUTPUT_X}+$((COMMAND_BAR_BOTTOM + 40))"
