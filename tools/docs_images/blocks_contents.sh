#!/usr/bin/env bash
# User guide, Blocks: the inside of the RC_LOWPASS block of the "Two RC filters" example, open in the editor, with
# its IN and OUT pin markers, R1 = {R}, C1 = {C} and the path Schematic > RC_LOWPASS over the corner of the canvas;
# a crop of the top of the canvas.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

# Wheel steps that zoom in on the block, which opens small in the middle of the canvas
ZOOM_STEPS=6
# How far the block is panned up, toward the path over the corner of the canvas
PAN_UP=230
CROP_HEIGHT=420

docs_launch
docs_enter_rc_block
docs_park_pointer
ui_move "$DOCS_CANVAS_X" "$DOCS_CANVAS_Y"
ui scroll "$DOCS_CANVAS_X" "$DOCS_CANVAS_Y" up "$ZOOM_STEPS"
sleep "$SHORT_PAUSE"
ui_drag "$DOCS_CANVAS_X" "$DOCS_CANVAS_Y" "$DOCS_CANVAS_X" $((DOCS_CANVAS_Y - PAN_UP)) middle
docs_park_pointer
canvas_top=$((COMMAND_BAR_BOTTOM + 50))
docs_save blocks_contents "$((DOCS_OUTPUT_X - CANVAS_LEFT - 15))x${CROP_HEIGHT}+${CANVAS_LEFT}+${canvas_top}"
