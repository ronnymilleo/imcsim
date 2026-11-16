#!/usr/bin/env bash
# Drives a real imcsim through the paths a refactoring can break without any test noticing, once on each simulation
# engine: every analysis on a circuit made for it and its Output tab, the runs that repeat (a stepped parameter, a
# stepped source, Monte Carlo), what an engine refuses and says so, results dropped by an edit, undo and redo, a block
# opened and left, and a live run with a switch flipped and a potentiometer turned.
#
# It shares the machinery of the docs images (tools/docs_images/lib.sh, whose README lists what must be installed):
# an isolated instance on a Hyprland workspace, driven through gui-helper, with text found on screen by OCR. Leave
# the keyboard and mouse alone while it runs.
#
#   tools/gui_check/run.sh                 every section, on every engine
#   tools/gui_check/run.sh edit block      only some sections: preferences, analyses, repeated, refusals, edit,
#                                          block, live
#   GUI_CHECK_ENGINES=nodalys tools/gui_check/run.sh live    only one engine: ngspice, nodalys or both (the default)
#   GUI_CHECK_KEEP_OPEN=1 tools/gui_check/run.sh   leave the last instance open at the end, to look at it
#   IMCSIM_BIN=/path/to/imcsim tools/gui_check/run.sh        a copy of the binary, so a rebuild cannot change it midway
#
# Each engine gets an instance of its own, started with the engine written in its preferences.json (the real control
# is looked at by the preferences section, which opens Edit > Preferences and reads the Backend choice).
#
# A step passes when imcsim is still running after it and the proof it names is on screen: the status line at the
# bottom of the editor says the run is done, with no warnings (or says it failed, for a run that must be refused, with
# the reason), and a word of the result tab is there. That is more than a smoke check but still OCR, not a proof of
# the numbers: each step also saves a shot under $DOCS_SCRATCH/shots (by default ~/.cache/imcsim-gui-check/shots),
# named by engine and numbered in order, such as nodalys_03_transient.png, and those are to be looked at before a
# change is called good.
#
# The circuit of each analysis (examples/ unless noted; fixtures/ holds the stepped parameter and the live
# potentiometer, which no example saves):
#
#   Operating point        voltage_divider
#   Transient              led_driver, decade_counter (logic parts), noise_through_rc_low_pass (random source)
#   AC sweep               rc_low_pass_filter
#   DC sweep               zener_regulator
#   Noise                  noise_analysis_rc_low_pass
#   Transfer function      transfer_function_attenuator
#   Sensitivity            bias_sensitivity
#   Poles and zeros        rlc_low_pass_poles_and_zeros
#   S-parameters           s_parameters_lc_low_pass; fixtures/s_parameters_one_port (the same filter with its second
#                          port turned off): Nodalys gives S11, ngspice refuses it, since it needs two ports
#   Stability              stability_common_emitter (a two-port amplifier): the stability circles on the Smith
#                          chart, the Stability view and, on Nodalys, the Z quantity
#   Truth table            logic_truth_table
#   Harmonic balance       harmonic_balance_rectifier
#                          Nodalys only: with ngspice the step expects it disabled and refused with its reason
#   Stepped parameter      fixtures/parameter_step_rc_low_pass (R1 of the RC low-pass over a list)
#   Stepped source         bjt_output_characteristics (DC sweep of V1, stepping I1)
#   Monte Carlo            monte_carlo_rc_low_pass (AC sweep), monte_carlo_bias_point (operating point)
#   Live run               switches_and_or (a switch flipped), fixtures/potentiometer_live (the LED dimmer, with a
#                          probe, whose potentiometer is turned with the wheel)

set -euo pipefail
DOCS_SCRATCH=${DOCS_SCRATCH:-${XDG_CACHE_HOME:-$HOME/.cache}/imcsim-gui-check}
DOCS_KEEP_OPEN=${GUI_CHECK_KEEP_OPEN:-0}
source "$(dirname "$0")/../docs_images/lib.sh"
GUI_CHECK_FIXTURES="$(cd "$(dirname "$0")" && pwd)/fixtures"

ENGINES=${GUI_CHECK_ENGINES:-"ngspice nodalys"}

# Seconds the first look at the status line waits, and the longest a run may take on a debug build, per kind of run
RUN_FIRST_LOOK=1.5
RUN_TIMEOUT=45
MONTE_CARLO_TIMEOUT=90
LIVE_PAUSE=3
# Wheel notches that turn the potentiometer, and the wiper step each one makes, in percent
WIPER_NOTCHES=3
# The Schematic button of the path bar that shows over the canvas while a block is open, from the left of the canvas
BLOCK_BAR_BUTTON_X=56
BLOCK_BAR_Y=168
# The middle of the Run button, pinned to the right end of the command bar, from the right edge of the window
RUN_BUTTON_FROM_RIGHT=32
# Time a tooltip takes to show once the pointer rests on its item
TOOLTIP_PAUSE=1.5
# A point of the Live toggle of the command bar that holds no letter of its label, from the right edge of the window
# and above the middle of the bar
LIVE_TOGGLE_FROM_RIGHT=130
LIVE_TOGGLE_ABOVE_MIDDLE=13
# The close button of a floating window, from the left end of its title
FLOATING_CLOSE_FROM_TITLE=408

ENGINE=""
STEP_NUMBER=0
TOTAL_STEPS=0
FAILED_STEPS=()

# =============================================================================
# Steps
# =============================================================================

# The words of the status line, left of the editor at the bottom of the window
read_status() {
    docs_read_words 0 $((DOCS_HEIGHT - 45)) "$DOCS_OUTPUT_X" $((DOCS_HEIGHT - 5))
}

# Saves the shot of a step under its number and engine, after moving the pointer out of the way (unless the second
# argument is keep_pointer, for a tooltip that the pointer holds open), and sets STEP_LABEL; imcsim having exited ends
# the whole check
take_shot() {
    STEP_NUMBER=$((STEP_NUMBER + 1))
    TOTAL_STEPS=$((TOTAL_STEPS + 1))
    STEP_LABEL=$(printf '%s_%02d_%s' "$ENGINE" "$STEP_NUMBER" "$1")
    if ! kill -0 "$DOCS_PID" 2>/dev/null; then
        echo "FAIL $STEP_LABEL: imcsim exited (see $DOCS_SCRATCH/app.log)"
        FAILED_STEPS+=("$STEP_LABEL")
        exit 1
    fi
    if [[ "${2:-}" != keep_pointer ]]; then
        docs_park_pointer
    fi
    sleep "$SHORT_PAUSE"
    "$GUI_HELPER" --window-id "$DOCS_WID" shot "$DOCS_SCRATCH/shots/$STEP_LABEL.png" >/dev/null 2>&1
}

pass_step() {
    echo "ok   $STEP_LABEL"
}

fail_step() {
    echo "FAIL $STEP_LABEL: $1"
    FAILED_STEPS+=("$STEP_LABEL")
}

# Saves the shot of a step and records whether it passed. WORD, a word of the result on screen, is a regular
# expression for a whole word. STATUS tells what the status line must say: done (a finished run, with no warnings),
# failed (a refused run, whose line must also match REASON), or nothing, to look at the word alone.
check_step() {
    local name=$1 word=${2:-} status_kind=${3:-} reason=${4:-}
    take_shot "$name"
    if [[ -n "$status_kind" ]]; then
        local status
        status=$(read_status)
        case "$status_kind" in
            done)
                if ! grep -qiE '\bdone\b' <<<"$status" || grep -qiE 'warnings|failed|stopped' <<<"$status"; then
                    fail_step "the status line is not a clean \"done\": \"$status\""
                    return 0
                fi
                ;;
            failed)
                if ! grep -qiE '\bfailed\b' <<<"$status" || ! grep -qiE "$reason" <<<"$status"; then
                    fail_step "the status line does not say it failed with \"$reason\": \"$status\""
                    return 0
                fi
                ;;
            *) docs_fail "check_step: unknown status $status_kind" ;;
        esac
    fi
    # A result has its words in the Output window; the other steps look for theirs anywhere
    local x0=0 y0=0 x1=$DOCS_WIDTH y1=$DOCS_HEIGHT
    if [[ -n "$status_kind" ]]; then
        x0=$DOCS_OUTPUT_X y0=$COMMAND_BAR_BOTTOM x1=$DOCS_SIDE_X
    fi
    if [[ -n "$word" ]] && ! docs_find_text "$word" "$x0" "$y0" "$x1" "$y1" >/dev/null; then
        fail_step "\"$word\" is not on screen"
        return 0
    fi
    pass_step
}

# Picks an analysis by the first word of its name; a word that is not in the list fails the step, not the script
pick_analysis() {
    if ! (docs_pick_analysis "$1") 2>/dev/null; then
        ui_key Escape
        return 1
    fi
}

# Opens an example, or a circuit of fixtures/, over whatever is open. Picking an analysis already marks a schematic as
# modified, so imcsim asks before it lets go of it: the question is answered with Discard, its last word on screen
# being the button
open_circuit() {
    local path="$GUI_CHECK_FIXTURES/$1.imcsim"
    [[ -f "$path" ]] || path="$EXAMPLES_DIR/$1.imcsim"
    [[ -f "$path" ]] || docs_fail "no such example or fixture: $1"
    echo "$path" >"$DOCS_DIALOG_ANSWER"
    ui_key ctrl+o
    sleep "$MEDIUM_PAUSE"
    local button
    if button=$(docs_find_text Discard | tail -n 1) && [[ -n "$button" ]]; then
        # shellcheck disable=SC2086
        ui_click $button
        sleep "$MEDIUM_PAUSE"
    fi
    : >"$DOCS_DIALOG_ANSWER"
    docs_check_alive "opening $1"
}

# Runs the selected analysis and waits until the status line says it ended, however it ended
run_and_wait() {
    local timeout=${1:-$RUN_TIMEOUT}
    ui_key F5
    sleep "$RUN_FIRST_LOOK"
    local waited=0
    while ! read_status | grep -qiE '\b(done|failed|stopped)\b'; do
        waited=$((waited + 1))
        if [[ $waited -ge $timeout ]]; then
            break
        fi
        sleep 1
    done
    docs_check_alive "running the analysis"
}

# Opens a circuit, runs the analysis saved in it and checks the result: NAME, CIRCUIT, WORD, then the timeout
check_circuit_run() {
    local name=$1 circuit=$2 word=$3 timeout=${4:-$RUN_TIMEOUT}
    open_circuit "$circuit"
    run_and_wait "$timeout"
    check_step "$name" "$word" "done"
}

# =============================================================================
# Sections
# =============================================================================

# The real control: Edit > Preferences shows the engine the instance was started with in its Backend choice
check_preferences() {
    docs_menu Edit 'Preferences.*'
    sleep "$MEDIUM_PAUSE"
    take_shot preferences_backend
    local label
    if ! label=$(docs_find_text Backend "$DOCS_OUTPUT_X" "$MENU_BAR_BOTTOM" "$DOCS_SIDE_X" "$DOCS_HEIGHT" | head -n 1) ||
        [[ -z "$label" ]]; then
        fail_step "the Backend choice is not on screen"
    else
        local label_x label_y
        read -r label_x label_y <<<"$label"
        local words
        words=$(docs_read_words $((label_x + 100)) $((label_y - 20)) $((label_x + 500)) $((label_y + 20)))
        if grep -qiw "$ENGINE" <<<"$words"; then
            pass_step
        else
            fail_step "the Backend choice reads \"$words\", not $ENGINE"
        fi
    fi
    # The window is floating over the Output window: its close button is at the right end of its title
    local title
    if title=$(docs_find_text Preferences "$DOCS_OUTPUT_X" "$MENU_BAR_BOTTOM" "$DOCS_SIDE_X" "$DOCS_HEIGHT" | head -n 1) &&
        [[ -n "$title" ]]; then
        local title_x title_y
        read -r title_x title_y <<<"$title"
        ui_click $((title_x + FLOATING_CLOSE_FROM_TITLE)) "$title_y"
    fi
    sleep "$SHORT_PAUSE"
}

# Every analysis on a circuit made for it, each with the proof of its result
check_analyses() {
    check_circuit_run operating_point voltage_divider Voltage
    check_circuit_run transient led_driver Spectrum
    check_circuit_run ac_sweep rc_low_pass_filter Magnitude
    check_circuit_run dc_sweep zener_regulator Cursors
    check_circuit_run noise noise_analysis_rc_low_pass Share
    check_circuit_run transfer_function transfer_function_attenuator impedance
    check_circuit_run sensitivity bias_sensitivity Share
    check_circuit_run poles_and_zeros rlc_low_pass_poles_and_zeros Poles
    check_circuit_run s_parameters s_parameters_lc_low_pass Smith
    check_s_parameters_one_port
    check_s_parameter_views
    check_circuit_run truth_table logic_truth_table 'combinations;?'
    check_harmonic_balance
    # Logic parts and a random source in a transient, the two things the engines treat differently
    check_circuit_run transient_logic decade_counter Spectrum
    check_circuit_run transient_noise_source noise_through_rc_low_pass Spectrum
}

# The views of a two-port result: the stability circles on the Smith chart, the stability against frequency and, on
# Nodalys, which gives the matrix, the Z quantity. Each is reached by the word of its control in the Output window
check_s_parameter_views() {
    check_circuit_run s_parameters_two_port stability_common_emitter Smith
    ui_click_text circles "$DOCS_OUTPUT_X" "$COMMAND_BAR_BOTTOM" "$DOCS_SIDE_X" "$DOCS_HEIGHT"
    sleep "$MEDIUM_PAUSE"
    check_step s_parameters_stability_circles 'Worst'
    ui_click_text Stability "$DOCS_OUTPUT_X" "$COMMAND_BAR_BOTTOM" "$DOCS_SIDE_X" "$DOCS_HEIGHT"
    sleep "$MEDIUM_PAUSE"
    check_step s_parameters_stability 'unstable'
    if [[ "$ENGINE" == nodalys ]]; then
        ui_click_text Z "$DOCS_OUTPUT_X" "$COMMAND_BAR_BOTTOM" "$DOCS_SIDE_X" "$DOCS_HEIGHT"
        sleep "$MEDIUM_PAUSE"
        check_step s_parameters_impedance 'Magnitude'
    fi
}

# Nodalys measures a single port; ngspice needs two, so the run is refused with the reason
check_s_parameters_one_port() {
    open_circuit s_parameters_one_port
    run_and_wait
    if [[ "$ENGINE" == ngspice ]]; then
        check_step s_parameters_one_port_refused 'S-parameters' failed 'two'
    else
        check_step s_parameters_one_port Smith "done"
    fi
}

# Nodalys runs it and plots the spectra; ngspice cannot, so the run is refused with the reason
check_harmonic_balance() {
    open_circuit harmonic_balance_rectifier
    run_and_wait
    if [[ "$ENGINE" == ngspice ]]; then
        check_step harmonic_balance_refused 'Harmonic' failed 'does not run'
    else
        check_step harmonic_balance 'Amplitudes:?' "done"
    fi
}

# A stepped parameter, a stepped source and Monte Carlo, which keep many results at once
check_repeated() {
    check_circuit_run stepped_parameter parameter_step_rc_low_pass Shown
    check_circuit_run stepped_source bjt_output_characteristics curve
    check_circuit_run monte_carlo_ac monte_carlo_rc_low_pass Nominal "$MONTE_CARLO_TIMEOUT"
    check_circuit_run monte_carlo_bias_point monte_carlo_bias_point Nominal "$MONTE_CARLO_TIMEOUT"
}

# Opens the analysis list, rests the pointer on the entry of ANALYSIS and checks that the tooltip gives REASON; the
# entry is disabled, so a click would do nothing
check_refusal() {
    local name=$1 analysis=$2 reason=$3
    ui_click $((DOCS_WIDTH - ANALYSIS_BUTTON_FROM_RIGHT)) "$COMMAND_BAR_Y"
    sleep "$SHORT_PAUSE"
    local entry
    if ! entry=$(docs_find_text "$analysis" $((DOCS_WIDTH - ANALYSIS_LIST_WIDTH)) "$COMMAND_BAR_BOTTOM" "$DOCS_WIDTH" \
        $((DOCS_HEIGHT / 2)) | head -n 1) || [[ -z "$entry" ]]; then
        ui_key Escape
        take_shot "$name"
        fail_step "$analysis is not in the analysis list"
        return 0
    fi
    # shellcheck disable=SC2086
    ui_move $entry
    local entry_x entry_y
    read -r entry_x entry_y <<<"$entry"
    sleep "$TOOLTIP_PAUSE"
    take_shot "$name" keep_pointer
    # The tooltip is several words, so its text is read whole, in a box under the pointer
    local words
    words=$(docs_read_words $((entry_x - 150)) $((entry_y + 15)) "$DOCS_WIDTH" $((entry_y + 80)))
    if grep -qiE "$reason" <<<"$words"; then
        pass_step
    else
        fail_step "the tooltip of $analysis does not say \"$reason\": \"$words\""
    fi
    ui_key Escape
}

# What an engine cannot run is grayed out in the list, with the reason as the tooltip
check_refusals() {
    if [[ "$ENGINE" == ngspice ]]; then
        open_circuit harmonic_balance_rectifier
        check_refusal harmonic_balance_disabled Harmonic 'does not run'
    fi
    # Logic gates have no sensitivity analysis with either engine
    open_circuit logic_truth_table
    check_refusal sensitivity_disabled Sensitivity 'logic'
}

# An edit drops the results at once; undo and redo are edits too, and a new run brings results back
check_edit_drops_results() {
    open_circuit rc_low_pass_filter
    run_and_wait
    check_step before_edit Magnitude "done"
    # The bottom wire runs from (4, 10) to (14, 10); the capacitor C1 stands at (14, 7)
    docs_calibrate_on_wire 4 14 10
    # shellcheck disable=SC2046
    ui_click $(docs_grid_point 14 7)
    ui_key Delete
    sleep "$MEDIUM_PAUSE"
    check_step after_edit "Run"
    ui_key ctrl+z
    sleep "$MEDIUM_PAUSE"
    check_step after_undo "Run"
    run_and_wait
    check_step rerun_after_undo Magnitude "done"
    ui_key ctrl+y
    sleep "$MEDIUM_PAUSE"
    check_step after_redo "Run"
    # A file opened over a result leaves none of it behind
    open_circuit voltage_divider
    check_step opened_over_result
}

# Inside a block the schematic shows no result of the root circuit, and they are back on the way out
check_block() {
    open_circuit rc_blocks
    run_and_wait
    check_step block_root_result Magnitude "done"
    # The input wire runs from the source at (0, 0) to the IN pin of X1 at (5, 0); X1 sits at (8, 0)
    docs_calibrate_on_wire 0 5 0
    # shellcheck disable=SC2046
    ui_click $(docs_grid_point 8 0)
    docs_menu Edit Contents
    sleep "$MEDIUM_PAUSE"
    check_step block_inside RC_LOWPASS
    # The Schematic button of the path bar over the canvas applies the open block and returns to the root
    ui_click $((CANVAS_LEFT + BLOCK_BAR_BUTTON_X)) "$BLOCK_BAR_Y"
    sleep "$MEDIUM_PAUSE"
    check_step block_left FAST
}

# Whether the Live toggle of the command bar is lit with the accent color, which it is only while armed and idle
is_live_armed() {
    local shot="$DOCS_SCRATCH/shots/live_toggle.png" pixel
    "$GUI_HELPER" --window-id "$DOCS_WID" shot "$shot" >/dev/null 2>&1
    pixel=$(magick "$shot" -format "%[pixel:p{$((DOCS_WIDTH - LIVE_TOGGLE_FROM_RIGHT)),$((COMMAND_BAR_Y - LIVE_TOGGLE_ABOVE_MIDDLE))}]" info:)
    local red blue
    IFS='(,)' read -r _ red _ blue _ <<<"$pixel"
    [[ "$red" -lt 80 && "$blue" -gt 150 ]]
}

# Arms Live mode on the transient of the circuit just opened, and starts the run; fails the section when the analysis
# or the Live toggle is not there
start_live_run() {
    local name=$1
    if ! pick_analysis Transient; then
        take_shot "$name"
        fail_step "Transient is not in the analysis list"
        return 1
    fi
    # Live mode stays armed from one circuit to the next, so the toggle is clicked only when it is off
    if ! is_live_armed; then
        ui_click_text Live $((DOCS_WIDTH / 2)) "$MENU_BAR_BOTTOM" "$DOCS_WIDTH" "$COMMAND_BAR_BOTTOM"
        sleep "$SHORT_PAUSE"
    fi
    if ! is_live_armed; then
        take_shot "$name"
        fail_step "the Live toggle is not armed"
        return 1
    fi
    return 0
}

# A live transient goes on while a switch is flipped by a click, then pauses on Stop and goes on with F5
check_live_switch() {
    open_circuit switches_and_or
    start_live_run live_switch || return 0
    check_step live_armed
    # The top wire runs from (8, 4) to (21, 4); SW2 stands at (8, 12)
    docs_calibrate_on_wire 8 21 4
    docs_run "$LIVE_PAUSE"
    check_step live_running live
    # shellcheck disable=SC2046
    ui_click $(docs_grid_point 8 12)
    sleep "$LIVE_PAUSE"
    check_step live_switch_flipped live
    # F5 only runs or continues, so the run is paused with the Run button, which reads Stop meanwhile
    ui_click $((DOCS_WIDTH - RUN_BUTTON_FROM_RIGHT)) "$COMMAND_BAR_Y"
    sleep "$MEDIUM_PAUSE"
    check_step live_paused paused
    ui_key F5
    sleep "$LIVE_PAUSE"
    check_step live_continued live
}

# The percentage written after the name of the potentiometer RV1, which sits at grid point (10, 9); it follows the
# wiper, so a change of it is the change the run was sent. Only its digits are kept, since OCR often drops the percent
# sign next to the outline of a part that the live run lets the wheel turn
read_wiper_percent() {
    local center_x=$1 center_y=$2 zoom=$3
    local box_left box_top box_right box_bottom
    box_left=$(awk -v x="$center_x" -v z="$zoom" 'BEGIN {printf "%d", x - 8 * z}')
    box_right=$(awk -v x="$center_x" -v z="$zoom" 'BEGIN {printf "%d", x - 0.5 * z}')
    box_top=$((center_y - 25))
    box_bottom=$((center_y + 25))
    docs_read_words "$box_left" "$box_top" "$box_right" "$box_bottom" | grep -oE '[0-9]+' | tail -n 1 || true
}

# A live transient on the LED dimmer: the wheel over the potentiometer turns its wiper, and the run goes on with the
# new value, which the label of the part shows
check_live_potentiometer() {
    open_circuit potentiometer_live
    start_live_run live_potentiometer || return 0
    # The wire at the bottom runs from (10, 11) to (12, 11); the potentiometer RV1 stands at (10, 9)
    docs_calibrate_on_wire 10 12 11
    docs_run "$LIVE_PAUSE"
    local pot_x pot_y
    read -r pot_x pot_y < <(docs_grid_point 10 9)
    local before after
    before=$(read_wiper_percent "$pot_x" "$pot_y" "$DOCS_ZOOM")
    ui_move "$pot_x" "$pot_y"
    ui scroll "$pot_x" "$pot_y" up "$WIPER_NOTCHES"
    sleep "$LIVE_PAUSE"
    after=$(read_wiper_percent "$pot_x" "$pot_y" "$DOCS_ZOOM")
    take_shot live_potentiometer_turned
    if [[ -z "$before" || -z "$after" ]]; then
        fail_step "the wiper percentage is not readable (before \"$before\", after \"$after\")"
    elif [[ "$before" == "$after" ]]; then
        fail_step "the wiper stayed at $before after the wheel turned"
    elif ! read_status | grep -qiE '\blive\b'; then
        fail_step "the run is not live after the wheel turned the wiper (wiper $before, then $after)"
    else
        echo "ok   $STEP_LABEL (wiper $before, then $after)"
    fi
}

# =============================================================================
# Main
# =============================================================================

# The sections to run, all of them unless some are named on the command line
SECTIONS=("$@")
[[ ${#SECTIONS[@]} -gt 0 ]] || SECTIONS=(preferences analyses repeated refusals edit block live)
for section in "${SECTIONS[@]}"; do
    case "$section" in
        preferences | analyses | repeated | refusals | edit | block | live) ;;
        *) docs_fail "unknown section: $section (preferences, analyses, repeated, refusals, edit, block, live)" ;;
    esac
done

START_SECONDS=$SECONDS
rm -rf "$DOCS_SCRATCH/shots"
mkdir -p "$DOCS_SCRATCH/shots"
LAUNCHED=0
for engine in $ENGINES; do
    case "${engine,,}" in
        ngspice) ENGINE=ngspice ENGINE_NAME=ngspice ;;
        nodalys) ENGINE=nodalys ENGINE_NAME=Nodalys ;;
        *) docs_fail "unknown engine: $engine (ngspice, nodalys)" ;;
    esac
    if [[ $LAUNCHED -eq 1 ]]; then
        # One instance per engine; only the one launched before is closed, by its PID
        docs_cleanup
        DOCS_PID=""
    fi
    echo
    echo "== $ENGINE_NAME =="
    docs_launch --engine "$ENGINE_NAME"
    LAUNCHED=1
    STEP_NUMBER=0
    # A key goes to the window under the pointer, which starts wherever the user left it
    ui_move "$DOCS_CANVAS_X" "$DOCS_CANVAS_Y"
    for section in "${SECTIONS[@]}"; do
        case "$section" in
            preferences) check_preferences ;;
            analyses) check_analyses ;;
            repeated) check_repeated ;;
            refusals) check_refusals ;;
            edit) check_edit_drops_results ;;
            block) check_block ;;
            live)
                check_live_switch
                check_live_potentiometer
                ;;
            *) docs_fail "unknown section: $section" ;;
        esac
    done
done

echo
echo "shots: $DOCS_SCRATCH/shots"
echo "$TOTAL_STEPS steps in $((SECONDS - START_SECONDS)) s"
if [[ ${#FAILED_STEPS[@]} -gt 0 ]]; then
    echo "${#FAILED_STEPS[@]} step(s) failed: ${FAILED_STEPS[*]}"
    exit 1
fi
echo "all $TOTAL_STEPS steps passed; look at the shots before calling the change good"
