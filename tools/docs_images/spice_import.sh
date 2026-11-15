#!/usr/bin/env bash
# User guide, Blocks: the Import SPICE library dialog on fixtures/vendor.lib with GENERIC_OPAMP picked, the vendor's
# comment naming its five ports, its parameters, and the pins INP and INN on the left, VP on top, VN at the bottom
# and OUT on the right; a crop of the dialog.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# Size of the dialog, which is centered across the window; its top is found from its title
DIALOG_WIDTH=662
DIALOG_HEIGHT=556
# From the middle of the word "SPICE" in the title up to the top of the dialog
TITLE_TO_TOP=15
# From the port label of a row to the edge choice of the same row
LABEL_TO_EDGE_CHOICE=268

open_import_dialog() {
    docs_menu File SPICE
}

docs_launch
docs_answer_dialog "$FIXTURES_DIR/vendor.lib" open_import_dialog
sleep "$MEDIUM_PAUSE"
dialog_x=$(((DOCS_WIDTH - DIALOG_WIDTH) / 2))
read -r _ title_y < <(docs_find_text SPICE "$dialog_x" 0 $((dialog_x + DIALOG_WIDTH)) "$DOCS_HEIGHT")
dialog_y=$((title_y - TITLE_TO_TOP))
# The edges are guessed from the port names; VN, the negative supply, goes at the bottom
read -r label_x label_y < <(docs_find_text VN "$dialog_x" "$dialog_y" $((dialog_x + 150)) $((dialog_y + DIALOG_HEIGHT)))
ui_click $((label_x + LABEL_TO_EDGE_CHOICE)) "$label_y"
ui_click_text bottom "$dialog_x" "$label_y" $((dialog_x + DIALOG_WIDTH)) $((label_y + 200))
docs_park_pointer
docs_save spice_import "${DIALOG_WIDTH}x${DIALOG_HEIGHT}+${dialog_x}+${dialog_y}"
