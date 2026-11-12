#!/usr/bin/env bash
# docs/themes.md: the pair of images of one theme, theme_<name>_schematic.png and theme_<name>_output.png. Both show
# the "Small-signal model of a BJT" example in a 1280x800 window whose Schematic, Output and Properties windows share
# one node as tabs (fixtures/tabs_1280x800.ini): the schematic after its AC sweep, then the Output window on it.
#
# Usage: theme_pair.sh THEME, with THEME as View > Theme names it, such as Graphite or "Cyber Slate".

set -euo pipefail
source "$(dirname "$0")/lib.sh"

[[ $# -eq 1 ]] || docs_fail "usage: theme_pair.sh THEME"
theme=$1
# "Cyber Slate" becomes cyber_slate, as the image names have it
slug=$(tr '[:upper:] ' '[:lower:]_' <<<"$theme")

# The tab bar of the shared node, under the command bar, where the Schematic tab is looked for
TAB_BAR_TOP=97
TAB_BAR_BOTTOM=126

docs_launch --size 1280x800 --theme "$theme" --layout "$FIXTURES_DIR/tabs_1280x800.ini"
docs_open_example hybrid_pi_model
docs_pick_analysis AC
# The run brings Output to the front
docs_run
docs_park_pointer
docs_save "theme_${slug}_output"
ui_click_text Schematic 0 "$TAB_BAR_TOP" $((DOCS_WIDTH / 2)) "$TAB_BAR_BOTTOM"
docs_park_pointer
docs_save "theme_${slug}_schematic"
