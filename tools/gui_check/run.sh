#!/usr/bin/env bash
# Drives a real imcsim through the paths a refactoring can break without any test noticing: every analysis and its
# Output tab, results dropped by an edit, undo and redo, a block opened and left, a live run with a switch flipped.
#
# It shares the machinery of the docs images (tools/docs_images/lib.sh, whose README lists what must be installed):
# an isolated instance on a Hyprland workspace, driven through gui-helper, with text found on screen by OCR. Leave
# the keyboard and mouse alone while it runs.
#
#   tools/gui_check/run.sh                 every step
#   GUI_CHECK_KEEP_OPEN=1 tools/gui_check/run.sh   leave the instance open at the end, to look at it
#
# A step passes when imcsim is still running after it and, where a word is given, that word is on screen. That is a
# smoke check, not a proof: each step also saves a shot under $DOCS_SCRATCH/shots (by default
# ~/.cache/imcsim-gui-check/shots), numbered in order, and those are to be looked at before a change is called good.

set -euo pipefail
DOCS_SCRATCH=${DOCS_SCRATCH:-${XDG_CACHE_HOME:-$HOME/.cache}/imcsim-gui-check}
DOCS_KEEP_OPEN=${GUI_CHECK_KEEP_OPEN:-0}
source "$(dirname "$0")/../docs_images/lib.sh"

# Long enough for the slowest example run on a debug build
RUN_PAUSE=4
MONTE_CARLO_PAUSE=10
LIVE_PAUSE=3

STEP_NUMBER=0
FAILED_STEPS=()

# Saves the shot of a step and records whether it passed: imcsim alive and, when WORD is given, WORD on screen
check_step() {
    local name=$1 word=${2:-}
    STEP_NUMBER=$((STEP_NUMBER + 1))
    local label
    label=$(printf '%02d_%s' "$STEP_NUMBER" "$name")
    if ! kill -0 "$DOCS_PID" 2>/dev/null; then
        echo "FAIL $label: imcsim exited (see $DOCS_SCRATCH/app.log)"
        FAILED_STEPS+=("$label")
        exit 1
    fi
    docs_park_pointer
    sleep "$SHORT_PAUSE"
    "$GUI_HELPER" --window-id "$DOCS_WID" shot "$DOCS_SCRATCH/shots/$label.png" >/dev/null 2>&1
    if [[ -n "$word" ]] && ! docs_find_text "$word" >/dev/null; then
        echo "FAIL $label: \"$word\" is not on screen"
        FAILED_STEPS+=("$label")
        return 0
    fi
    echo "ok   $label"
}

# Picks an analysis by the first word of its name; a word that is not in the list fails the step, not the script
pick_analysis() {
    if ! (docs_pick_analysis "$1") 2>/dev/null; then
        ui_key Escape
        return 1
    fi
}

# Opens an example and runs the analysis saved in it
run_example() {
    local example=$1 word=${2:-} pause=${3:-$RUN_PAUSE}
    docs_open_example "$example"
    docs_run "$pause"
    check_step "$example" "$word"
}

# Every analysis the circuit of one amplifier can run, each from its default settings
check_analyses_of_one_circuit() {
    docs_open_example common_emitter_amplifier
    check_step opened
    local analysis
    for analysis in Operating Transient AC DC Noise Transfer Sensitivity Poles; do
        if pick_analysis "$analysis"; then
            docs_run "$RUN_PAUSE"
            check_step "amplifier_$analysis"
        else
            STEP_NUMBER=$((STEP_NUMBER + 1))
            echo "FAIL $(printf '%02d' "$STEP_NUMBER")_amplifier_$analysis: not in the analysis list"
            FAILED_STEPS+=("amplifier_$analysis")
        fi
    done
}

# The analyses that need a circuit made for them, and the runs that repeat
check_examples() {
    run_example voltage_divider
    run_example rc_low_pass_filter
    run_example bjt_output_characteristics
    run_example bias_sensitivity
    run_example rlc_low_pass_poles_and_zeros
    run_example s_parameters_lc_low_pass
    run_example logic_truth_table
    run_example noise_through_rc_low_pass
    run_example decade_counter
    run_example monte_carlo_rc_low_pass "" "$MONTE_CARLO_PAUSE"
    run_example monte_carlo_bias_point "" "$MONTE_CARLO_PAUSE"
}

# An edit drops the results at once; undo and redo are edits too, and a new run brings results back
check_edit_drops_results() {
    docs_open_example rc_low_pass_filter
    docs_run "$RUN_PAUSE"
    check_step before_edit
    docs_paste_file "$EXAMPLES_DIR/voltage_divider.imcsim" "$DOCS_CANVAS_X" $((DOCS_HEIGHT - 200))
    ui_key Escape
    check_step after_edit
    ui_key ctrl+z
    sleep "$MEDIUM_PAUSE"
    check_step after_undo
    docs_run "$RUN_PAUSE"
    check_step rerun_after_undo
    ui_key ctrl+y
    sleep "$MEDIUM_PAUSE"
    check_step after_redo
    # A file opened over a result leaves none of it behind
    docs_open_example voltage_divider
    check_step opened_over_result
}

# Inside a block the schematic shows no result of the root circuit, and they are back on the way out
check_block() {
    docs_open_example rc_blocks
    docs_run "$RUN_PAUSE"
    check_step block_root_result
    # The input wire runs from the source at (0, 0) to the IN pin of X1 at (5, 0); X1 sits at (8, 0)
    docs_calibrate_on_wire 0 5 0
    # shellcheck disable=SC2046
    ui_click $(docs_grid_point 8 0)
    docs_menu Edit Contents
    sleep "$MEDIUM_PAUSE"
    check_step block_inside
    ui_key Escape
    sleep "$MEDIUM_PAUSE"
    check_step block_left
}

# A live transient goes on while a switch is flipped by a click, then pauses on Stop
check_live_run() {
    docs_open_example switches_and_or
    if ! pick_analysis Transient; then
        echo "FAIL live run: Transient is not in the analysis list"
        FAILED_STEPS+=("live_run")
        return 0
    fi
    ui_click_text Live $((DOCS_WIDTH / 2)) "$MENU_BAR_BOTTOM" "$DOCS_WIDTH" "$COMMAND_BAR_BOTTOM"
    sleep "$SHORT_PAUSE"
    check_step live_armed
    # The top wire runs from (8, 4) to (21, 4); SW2 stands at (8, 12)
    docs_calibrate_on_wire 8 21 4
    docs_run "$LIVE_PAUSE"
    check_step live_running
    # shellcheck disable=SC2046
    ui_click $(docs_grid_point 8 12)
    sleep "$LIVE_PAUSE"
    check_step live_switch_flipped
    ui_key F5
    sleep "$MEDIUM_PAUSE"
    check_step live_paused
}

rm -rf "$DOCS_SCRATCH/shots"
docs_launch
check_analyses_of_one_circuit
check_examples
check_edit_drops_results
check_block
check_live_run

echo
echo "shots: $DOCS_SCRATCH/shots"
if [[ ${#FAILED_STEPS[@]} -gt 0 ]]; then
    echo "${#FAILED_STEPS[@]} step(s) failed: ${FAILED_STEPS[*]}"
    exit 1
fi
echo "all $STEP_NUMBER steps passed; look at the shots before calling the change good"
