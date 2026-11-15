#!/usr/bin/env bash
# Tutorial, step 7: cursors A and B over the transient of the rectifier with its 220 Ohm load, A on a peak of V(2)
# and B at the bottom of the sag that follows, with the statistics between them.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# How far left of the "Cursors" label its checkbox is
CHECKBOX_FROM_LABEL=48
# Where the cursors go, in seconds: the end of the third pulse, where V(2) peaks, and just before the fourth
CURSOR_A_TIME=0.00249
CURSOR_B_TIME=0.00299
# The run lasts 5 ms; new cursors start at a third and two thirds of it
RUN_TIME=0.005

docs_launch
docs_open_fixture rectifier_220
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
docs_run
read -r label_x label_y < <(docs_find_text Cursors "$DOCS_OUTPUT_X" "$COMMAND_BAR_BOTTOM" "$DOCS_SIDE_X" 260)
ui_click $((label_x - CHECKBOX_FROM_LABEL)) "$label_y"
# The time axis, from its first and last tick labels under the plot; OCR tends to read 0 and 5 as O and S
read -r start_x tick_y < <(docs_find_text "0s|Os" "$DOCS_OUTPUT_X" 600 "$DOCS_SIDE_X" 800)
# The last tick is the rightmost match, since a misread 3.5ms can match too
read -r end_x _ < <(docs_find_text "5ms|Sms" "$DOCS_OUTPUT_X" 600 "$DOCS_SIDE_X" 800 | sort -n | tail -n 1)
((end_x - start_x > 300)) || docs_fail "the time axis was misread: 0s at $start_x, 5ms at $end_x"
time_x() {
    awk -v t="$1" -v x0="$start_x" -v x1="$end_x" -v span="$RUN_TIME" 'BEGIN {printf "%d", x0 + t / span * (x1 - x0)}'
}
# The cursors are grabbed in the middle of the plot, at their starting times
middle_y=$((tick_y - 250))
ui_drag "$(time_x "$(awk -v s="$RUN_TIME" 'BEGIN {print s / 3}')")" "$middle_y" "$(time_x "$CURSOR_A_TIME")" "$middle_y"
ui_drag "$(time_x "$(awk -v s="$RUN_TIME" 'BEGIN {print s * 2 / 3}')")" "$middle_y" "$(time_x "$CURSOR_B_TIME")" "$middle_y"
docs_park_pointer
docs_save tutorial_cursors
