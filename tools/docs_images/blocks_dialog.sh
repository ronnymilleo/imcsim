#!/usr/bin/env bash
# User guide, Blocks: the block dialog of RC_LOWPASS (Edit > Block Pin and Parameter Settings... while inside it),
# with the pins IN on the left and OUT on the right and the parameters R = 1000 and C = 1e-07; a crop of the dialog.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# Size of the dialog, which opens in the middle of the window
DIALOG_WIDTH=540
DIALOG_HEIGHT=396

docs_launch
docs_enter_rc_block
docs_menu Edit Pin
docs_park_pointer
docs_save blocks_dialog \
    "${DIALOG_WIDTH}x${DIALOG_HEIGHT}+$(((DOCS_WIDTH - DIALOG_WIDTH) / 2))+$(((DOCS_HEIGHT - DIALOG_HEIGHT) / 2))"
