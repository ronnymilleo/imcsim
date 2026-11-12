#!/usr/bin/env bash
# Shared helpers of the scripts that retake the docs images. Source it from a script; it launches one isolated
# imcsim instance, places it on a Hyprland workspace of its own, drives it with gui-helper and saves the shots.
#
# Settings, all optional, from the environment:
#   IMCSIM_BIN          imcsim binary (default: out/release, then out/debug of this repository)
#   GUI_HELPER          gui-helper binary
#   DOCS_WORKSPACE      Hyprland workspace the instance runs on (default: 4)
#   DOCS_SCRATCH        folder for the isolated data folder, logs and raw shots
#   DOCS_RETURN_WORKSPACE  workspace to focus at the end (default: the one active at launch)
#   DOCS_KEEP_OPEN=1    leave the instance open at the end, to look at it (kill it by its PID afterwards)

set -euo pipefail

DOCS_TOOLS_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$DOCS_TOOLS_DIR/../.." && pwd)
IMAGES_DIR="$REPO_ROOT/docs/images"
FIXTURES_DIR="$DOCS_TOOLS_DIR/fixtures"
EXAMPLES_DIR="$REPO_ROOT/app-gui/examples"

GUI_HELPER=${GUI_HELPER:-/mnt/DevDrive/Develop/Projects/gui-helper/build/gui-helper}
DOCS_WORKSPACE=${DOCS_WORKSPACE:-4}
DOCS_SCRATCH=${DOCS_SCRATCH:-${XDG_CACHE_HOME:-$HOME/.cache}/imcsim-docs-images}

# Pauses, in seconds: a frame of the GUI is a few milliseconds, but SDL, ImGui popups and Hyprland animations need
# a moment to settle before the next input or the shot
SHORT_PAUSE=0.25
MEDIUM_PAUSE=0.6
LONG_PAUSE=1.5

# Layout of the main window. The command bar spans the window under the menu bar, with the analysis button, Live and
# Run at its right end; these are the only fixed coordinates the library uses
MENU_BAR_BOTTOM=28
COMMAND_BAR_Y=63
COMMAND_BAR_BOTTOM=90
# Wheel steps the shots of a small circuit zoom in by, so it fills more of the editor
SMALL_CIRCUIT_ZOOM_STEPS=2
# Left edge of the canvas, right of the part rail of the editor
CANVAS_LEFT=90
# The analysis button, measured from the right edge of the window
ANALYSIS_BUTTON_FROM_RIGHT=230
# Room the analysis list takes from the right edge of the window
ANALYSIS_LIST_WIDTH=400

DOCS_PID=""
DOCS_DIALOG_ANSWER=""
DOCS_WID=""
DOCS_ORIGINAL_WORKSPACE=""
DOCS_WIDTH=0
DOCS_HEIGHT=0

docs_fail() {
    echo "error: $*" >&2
    exit 1
}

# =============================================================================
# Prerequisites
# =============================================================================

docs_find_binary() {
    if [[ -n "${IMCSIM_BIN:-}" ]]; then
        [[ -x "$IMCSIM_BIN" ]] || docs_fail "IMCSIM_BIN is not executable: $IMCSIM_BIN"
        return
    fi
    # The most recently built of the release and debug binaries, since an old one shows an old version and window
    local candidate
    for candidate in "$REPO_ROOT/out/release/bin/imcsim" "$REPO_ROOT/out/debug/bin/imcsim"; do
        if [[ -x "$candidate" && (-z "${IMCSIM_BIN:-}" || "$candidate" -nt "$IMCSIM_BIN") ]]; then
            IMCSIM_BIN="$candidate"
        fi
    done
    [[ -n "${IMCSIM_BIN:-}" ]] || docs_fail "imcsim is not built; build it or set IMCSIM_BIN"
}

docs_check_tools() {
    [[ -x "$GUI_HELPER" ]] || docs_fail "gui-helper not found at $GUI_HELPER; set GUI_HELPER"
    local tool
    for tool in hyprctl grim magick wl-copy jq; do
        command -v "$tool" >/dev/null || docs_fail "$tool is missing"
    done
}

# =============================================================================
# Hyprland
# =============================================================================

# Runs a Lua snippet on the window of our instance, bound to the local `w`; prints what the snippet returns
docs_hypr_on_window() {
    hyprctl repl "local w=nil; for _,c in ipairs(hl.get_windows()) do if c.pid==$DOCS_PID then w=c; break end end;
        if not w then return 'WAIT' end; $1"
}

docs_monitor_origin() {
    hyprctl monitors -j | jq -r '.[] | select(.focused) | "\(.x) \(.y) \(.width) \(.height)"'
}

# Puts the window on the docs workspace and gives it the asked size: "full" for full screen, or WIDTHxHEIGHT for a
# floating window of exactly that size in the middle of the monitor
docs_place_window() {
    local size=$1
    local result=""
    for _ in $(seq 1 50); do
        result=$(docs_hypr_on_window "hl.dispatch(hl.dsp.window.move({workspace='$DOCS_WORKSPACE', follow=true,
            window=w})); return 'OK'")
        [[ "$result" == *OK* ]] && break
        sleep 0.1
    done
    [[ "$result" == *OK* ]] || docs_fail "Hyprland never listed the window of PID $DOCS_PID"
    sleep "$SHORT_PAUSE"
    if [[ "$size" == full ]]; then
        docs_hypr_on_window "hl.dispatch(hl.dsp.window.fullscreen({mode='fullscreen', action='set', window=w}));
            return 'OK'" >/dev/null
    else
        local width=${size%x*} height=${size#*x}
        local monitor_x monitor_y monitor_width monitor_height
        read -r monitor_x monitor_y monitor_width monitor_height < <(docs_monitor_origin)
        # Positions are in the layout of all monitors, so the monitor origin is added
        local x=$((monitor_x + (monitor_width - width) / 2)) y=$((monitor_y + (monitor_height - height) / 2))
        docs_hypr_on_window "hl.dispatch(hl.dsp.window.float({action='set', window=w}));
            hl.dispatch(hl.dsp.window.resize({x=$width, y=$height, window=w}));
            hl.dispatch(hl.dsp.window.move({x=$x, y=$y, window=w})); return 'OK'" >/dev/null
    fi
    hyprctl repl "hl.dispatch(hl.dsp.focus({workspace=$DOCS_WORKSPACE}))" >/dev/null
    docs_hypr_on_window "hl.dispatch(hl.dsp.focus({window=w})); return 'OK'" >/dev/null
    # Let the resize animation end, so the size read below is the final one
    sleep "$LONG_PAUSE"
}

docs_read_size() {
    local info
    info=$("$GUI_HELPER" --window-id "$DOCS_WID" info)
    local size
    size=$(awk '{for (i = 1; i <= NF; i++) if ($i ~ /^[1-9][0-9]*x[0-9]+$/) {print $i; exit}}' <<<"$info")
    DOCS_WIDTH=${size%x*}
    DOCS_HEIGHT=${size#*x}
}

# =============================================================================
# Launch and cleanup
# =============================================================================

docs_cleanup() {
    if [[ -n "$DOCS_PID" && "${DOCS_KEEP_OPEN:-0}" != 1 ]]; then
        # Our own file dialogs (zenity) are children of our instance; nothing else is touched
        pkill -P "$DOCS_PID" 2>/dev/null || true
        if kill -0 "$DOCS_PID" 2>/dev/null; then
            kill "$DOCS_PID" 2>/dev/null || true
            for _ in $(seq 1 20); do
                kill -0 "$DOCS_PID" 2>/dev/null || break
                sleep 0.1
            done
            kill -9 "$DOCS_PID" 2>/dev/null || true
        fi
    fi
    # Hyprland may move the focus once the window is gone, so wait for that before going back
    for _ in $(seq 1 20); do
        [[ "$(docs_hypr_on_window "return 'FOUND'" 2>/dev/null)" == *FOUND* ]] || break
        sleep 0.1
    done
    if [[ -n "$DOCS_ORIGINAL_WORKSPACE" ]]; then
        hyprctl repl "hl.dispatch(hl.dsp.focus({workspace=$DOCS_ORIGINAL_WORKSPACE}))" >/dev/null 2>&1 || true
    fi
}

# Launches an isolated instance and waits for its window.
#   --size full|WIDTHxHEIGHT   window size (default: full, the whole monitor)
#   --theme NAME               theme, as View > Theme names it (default: Graphite)
#   --wire-colors NAME         Plain, Nodes, Voltage, Current, Power (default: Plain)
#   --layout FILE              an imgui.ini to start from instead of the default layout
docs_launch() {
    local size=full theme=Graphite wire_colors=Plain layout=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --size) size=$2; shift 2 ;;
            --theme) theme=$2; shift 2 ;;
            --wire-colors) wire_colors=$2; shift 2 ;;
            --layout) layout=$2; shift 2 ;;
            *) docs_fail "docs_launch: unknown option $1" ;;
        esac
    done
    docs_check_tools
    docs_find_binary

    local data_dir="$DOCS_SCRATCH/data"
    DOCS_DIALOG_ANSWER="$DOCS_SCRATCH/dialog_answer"
    rm -rf "$data_dir"
    mkdir -p "$data_dir/imcsim" "$DOCS_SCRATCH/shots"
    if [[ -n "$layout" ]]; then
        cp "$layout" "$data_dir/imcsim/imgui.ini"
    fi
    # Every choice that changes the look is fixed here, so the shots do not depend on the user's own preferences
    jq -n --arg theme "$theme" --arg wire_colors "$wire_colors" '{
        version: 1, theme: $theme, symbols: "IEC", terminal_numbers: false, wire_colors: $wire_colors,
        live_effects: ["Fuse"], value_series: "Free",
        bring_output_to_front: true, rerun_on_edit: false, rerun_delay_seconds: 0.5}' \
        >"$data_dir/imcsim/preferences.json"

    # update_all.sh passes the workspace it started on, so a run of many scripts returns there
    DOCS_ORIGINAL_WORKSPACE=${DOCS_RETURN_WORKSPACE:-$(hyprctl activeworkspace -j | jq -r .id)}
    trap docs_cleanup EXIT

    local windows_before
    windows_before=" $("$GUI_HELPER" list | awk '{print $1}' | tr '\n' ' ') "
    : >"$DOCS_DIALOG_ANSWER"
    # gui-helper only sees X11 windows, hence XWayland; the file dialogs are answered by fake_zenity/zenity
    SDL_VIDEO_DRIVER=x11 SDL_FILE_DIALOG_DRIVER=zenity GDK_BACKEND=x11 GDK_DEBUG=no-portals \
        PATH="$DOCS_TOOLS_DIR/fake_zenity:$PATH" DOCS_DIALOG_ANSWER="$DOCS_DIALOG_ANSWER" \
        XDG_DATA_HOME="$data_dir" setsid "$IMCSIM_BIN" >"$DOCS_SCRATCH/app.log" 2>&1 &
    DOCS_PID=$!

    for _ in $(seq 1 100); do
        DOCS_WID=$("$GUI_HELPER" list | awk -v before="$windows_before" \
            '/imcsim/ && index(before, " " $1 " ") == 0 {print $1; exit}' || true)
        [[ -n "$DOCS_WID" ]] && break
        sleep 0.1
    done
    [[ -n "$DOCS_WID" ]] || docs_fail "the imcsim window never appeared (see $DOCS_SCRATCH/app.log)"

    docs_place_window "$size"
    docs_read_size
    local expected
    if [[ "$size" == full ]]; then
        local monitor_x monitor_y monitor_width monitor_height
        read -r monitor_x monitor_y monitor_width monitor_height < <(docs_monitor_origin)
        expected="${monitor_width}x${monitor_height}"
    else
        expected=$size
    fi
    [[ "${DOCS_WIDTH}x${DOCS_HEIGHT}" == "$expected" ]] ||
        docs_fail "the window is ${DOCS_WIDTH}x${DOCS_HEIGHT}, not $expected"
    docs_layout
    echo "imcsim PID $DOCS_PID, window $DOCS_WID, ${DOCS_WIDTH}x${DOCS_HEIGHT} on workspace $DOCS_WORKSPACE"
}

# Works out where the windows of the default layout are: the editor, Output and the side column share the width in
# the proportions of Application::SetupDefaultLayout() (side column 18%, Output 56% of the rest)
docs_layout() {
    DOCS_SIDE_X=$((DOCS_WIDTH * 82 / 100))
    DOCS_OUTPUT_X=$((DOCS_SIDE_X * 44 / 100))
    # The middle of the canvas, right of the part rail
    DOCS_CANVAS_X=$(((DOCS_OUTPUT_X + CANVAS_LEFT) / 2))
    DOCS_CANVAS_Y=$(((DOCS_HEIGHT + COMMAND_BAR_BOTTOM) / 2))
}

docs_check_alive() {
    kill -0 "$DOCS_PID" 2>/dev/null || docs_fail "imcsim exited during: $1 (see $DOCS_SCRATCH/app.log)"
}

# =============================================================================
# Input, in window coordinates
# =============================================================================

ui() {
    "$GUI_HELPER" --window-id "$DOCS_WID" "$@" >/dev/null
}

# ImGui reads the hover of one frame and the click of the next, so the pointer moves there first
ui_click() {
    ui move "$1" "$2"
    sleep 0.1
    ui click "$1" "$2" "${3:-left}"
    sleep "$SHORT_PAUSE"
}

ui_double_click() {
    ui move "$1" "$2"
    sleep 0.1
    ui doubleclick "$1" "$2"
    sleep "$SHORT_PAUSE"
}

ui_move() {
    ui move "$1" "$2"
    sleep "$SHORT_PAUSE"
}

ui_drag() {
    ui move "$1" "$2"
    sleep 0.1
    ui drag "$@"
    sleep "$SHORT_PAUSE"
}

ui_key() {
    ui key "$1"
    sleep "$SHORT_PAUSE"
}

ui_text() {
    ui text "$1"
    sleep "$SHORT_PAUSE"
}

# =============================================================================
# Finding text on screen
# =============================================================================

# Prints the center "X Y" of the first word on screen that matches WORD, an extended regular expression for the whole
# word, such as "Transient" or "0s|Os" for a word OCR may misread, read with OCR; every match is printed, one per
# line in reading order, so "read" takes the first and "tail -n 1" the last
# from a fresh shot, only inside the box X0 Y0 X1 Y1 when given. Fails when nothing matches.
# Used instead of fixed coordinates for menu items, list entries and labels, so they follow the layout. Text on a
# colored button or tab reads badly; give a box around plain text where you can, since a small box reads best.
docs_find_text() {
    local word=$1 x0=${2:-0} y0=${3:-0} x1=${4:-$DOCS_WIDTH} y1=${5:-$DOCS_HEIGHT}
    local shot="$DOCS_SCRATCH/shots/ocr.png" scaled="$DOCS_SCRATCH/shots/ocr_scaled.png"
    "$GUI_HELPER" --window-id "$DOCS_WID" shot "$shot" >/dev/null 2>&1
    local crop="$((x1 - x0))x$((y1 - y0))+$x0+$y0"
    # Tesseract reads dark text on a light background best, at about three times the size of UI text; light themes
    # need no negation, and a hard threshold rescues text that the plain gray scale loses
    local filter position
    for filter in "-negate -normalize" "-negate -threshold 60%" "-normalize" "-threshold 40%"; do
        # shellcheck disable=SC2086
        magick "$shot" -crop "$crop" +repage -colorspace Gray -resize 300% $filter "$scaled"
        position=$(tesseract "$scaled" - --psm 11 tsv 2>/dev/null | awk -F'\t' -v word="$word" -v x0="$x0" -v y0="$y0" '
            NR > 1 && $12 ~ ("^(" word ")$") {print int(($7 + $9 / 2) / 3) + x0, int(($8 + $10 / 2) / 3) + y0}')
        if [[ -n "$position" ]]; then
            echo "$position"
            return 0
        fi
    done
    return 1
}

# Clicks the first word on screen that matches WORD, see docs_find_text
ui_click_text() {
    local position
    position=$(docs_find_text "$@" | head -n 1) && [[ -n "$position" ]] || docs_fail "\"$1\" is not on screen"
    # shellcheck disable=SC2086
    ui_click $position
}

ui_move_to_text() {
    local position
    position=$(docs_find_text "$@" | head -n 1) && [[ -n "$position" ]] || docs_fail "\"$1\" is not on screen"
    # shellcheck disable=SC2086
    ui_move $position
}

# =============================================================================
# Schematic coordinates
# =============================================================================

# Color of a plain wire in the Graphite theme; the calibration below looks for it
WIRE_COLOR="rgb(120,190,150)"
# Gaps along a wire that still count as one wire, in pixels: junction dots and probe marks break the color
WIRE_GAP=4

# Works out where the schematic is on screen from its longest horizontal wire, which must run from grid point
# (GX1, GY) to (GX2, GY) and be the longest straight run of wire color on the canvas. Sets the zoom and the window
# point of grid point (0, 0), which docs_grid_point uses. Run it after any zoom or pan, with nothing selected.
docs_calibrate_on_wire() {
    local grid_x1=$1 grid_x2=$2 grid_y=$3
    local shot="$DOCS_SCRATCH/shots/calibration.png"
    "$GUI_HELPER" --window-id "$DOCS_WID" shot "$shot" >/dev/null 2>&1
    local left=$CANVAS_LEFT top=$((COMMAND_BAR_BOTTOM + 50))
    local width=$((DOCS_OUTPUT_X - CANVAS_LEFT - 20)) height=$((DOCS_HEIGHT - top - 60))
    local run
    # A bitmap where the wire color is 0, read row by row for the longest run of it
    run=$(magick "$shot" -crop "${width}x${height}+$left+$top" +repage -alpha off -fuzz 12% -fill white \
        -opaque "$WIRE_COLOR" -fuzz 0 -fill black +opaque white -compress none pbm:- | awk -v gap="$WIRE_GAP" '
        NR == 1 {next}
        NR == 2 {width = $1; next}
        {for (i = 1; i <= NF; i++) pixels[count++] = $i}
        END {
            for (y = 0; y * width < count; y++) {
                start = -1; last = -100
                for (x = 0; x < width; x++) {
                    if (pixels[y * width + x] != 0) continue
                    if (x - last > gap) start = x
                    last = x
                    if (last - start > best) {best = last - start; best_x = start; best_y = y}
                }
            }
            print best_x, best_x + best, best_y
        }')
    local x1 x2 y
    read -r x1 x2 y <<<"$run"
    DOCS_ZOOM=$(awk -v x1="$x1" -v x2="$x2" -v g1="$grid_x1" -v g2="$grid_x2" 'BEGIN {print (x2 - x1) / (g2 - g1)}')
    DOCS_GRID_X0=$(awk -v x="$((left + x1))" -v g="$grid_x1" -v z="$DOCS_ZOOM" 'BEGIN {print x - g * z}')
    DOCS_GRID_Y0=$(awk -v y="$((top + y))" -v g="$grid_y" -v z="$DOCS_ZOOM" 'BEGIN {print y - g * z}')
    awk -v z="$DOCS_ZOOM" 'BEGIN {exit !(z >= 4 && z <= 200)}' || docs_fail "no wire found to calibrate on"
}

# Prints the window point "X Y" of grid point GX GY (fractions are fine), from the last docs_calibrate_on_wire
docs_grid_point() {
    awk -v gx="$1" -v gy="$2" -v x0="$DOCS_GRID_X0" -v y0="$DOCS_GRID_Y0" -v zoom="$DOCS_ZOOM" \
        'BEGIN {printf "%d %d\n", x0 + gx * zoom + 0.5, y0 + gy * zoom + 0.5}'
}

# =============================================================================
# Commands of imcsim
# =============================================================================

# Answers the next file dialog with PATH (see fake_zenity/zenity), then opens it with KEY, such as ctrl+o, or with
# a command, such as a function that clicks a menu item
docs_answer_dialog() {
    local path=$1 opener=$2
    echo "$path" >"$DOCS_DIALOG_ANSWER"
    if [[ "$(type -t "$opener")" == function ]]; then
        "$opener"
    else
        ui_key "$opener"
    fi
    # SDL hands the answer to imcsim on its next frames
    sleep "$MEDIUM_PAUSE"
    : >"$DOCS_DIALOG_ANSWER"
    docs_check_alive "the file dialog for $path"
}

# Opens a schematic file with File > Open (Ctrl+O)
docs_open_file() {
    [[ -f "$1" ]] || docs_fail "no such file: $1"
    docs_answer_dialog "$1" ctrl+o
}

# Opens one of the examples of File > Examples by its file name, such as rc_low_pass_filter
docs_open_example() {
    docs_open_file "$EXAMPLES_DIR/$1.imcsim"
}

# Opens the RC_LOWPASS block of the "Two RC filters" example (rc_blocks) for editing: X1 is selected with a click
# and entered with Edit > Edit Block Contents
docs_enter_rc_block() {
    docs_open_example rc_blocks
    # The input wire runs from the source at (0, 0) to the IN pin of X1 at (5, 0); X1 sits at (8, 0)
    docs_calibrate_on_wire 0 5 0
    ui_click $(docs_grid_point 8 0)
    docs_menu Edit Contents
}

# Opens one of the circuits in fixtures/ by its file name, such as rectifier
docs_open_fixture() {
    docs_open_file "$FIXTURES_DIR/$1.imcsim"
}

# Puts a schematic file on the clipboard and pastes it into the editor, placed with a click at X Y
docs_paste_file() {
    local path=$1 x=$2 y=$3
    wl-copy <"$path"
    sleep "$SHORT_PAUSE"
    ui_move "$x" "$y"
    ui_key ctrl+v
    ui_move $((x + 1)) "$y"
    ui_click "$x" "$y"
    docs_check_alive "pasting $path"
}

# Picks the analysis that Run runs, in the list under the analysis button of the command bar, by the first word of
# its name: Operating, Transient, AC, DC, Noise...; the list stays open, with the settings of that analysis
docs_open_analysis() {
    ui_click $((DOCS_WIDTH - ANALYSIS_BUTTON_FROM_RIGHT)) "$COMMAND_BAR_Y"
    sleep "$SHORT_PAUSE"
    ui_click_text "$1" $((DOCS_WIDTH - ANALYSIS_LIST_WIDTH)) "$COMMAND_BAR_BOTTOM" "$DOCS_WIDTH" $((DOCS_HEIGHT / 2))
}

# The same, closing the list afterwards
docs_pick_analysis() {
    docs_open_analysis "$1"
    ui_key Escape
}

# Zooms the schematic in by STEPS wheel steps. The wheel keeps the point under the pointer in place; a point left of
# the middle of the canvas leaves room for the labels left of a source, which the frame of an opened file puts
# close to the left edge
docs_zoom_canvas() {
    local x=$((CANVAS_LEFT + (DOCS_OUTPUT_X - CANVAS_LEFT) * 2 / 5))
    ui_move "$x" "$DOCS_CANVAS_Y"
    ui scroll "$x" "$DOCS_CANVAS_Y" up "$1"
    sleep "$SHORT_PAUSE"
}

# Moves the pointer to the empty bottom of the side column, so no hover or tooltip shows in the shot
docs_park_pointer() {
    ui_move $((DOCS_SIDE_X + 150)) $((DOCS_HEIGHT - 100))
}

# Opens a menu of the menu bar, such as Edit, and clicks the item that has the word ITEM_WORD, such as Contents for
# Edit Block Contents; the word must be unique in the menu
docs_menu() {
    local menu=$1 item=$2
    ui_click_text "$menu" 0 0 $((DOCS_WIDTH / 3)) "$MENU_BAR_BOTTOM"
    sleep "$SHORT_PAUSE"
    ui_click_text "$item" 0 "$MENU_BAR_BOTTOM" $((DOCS_WIDTH / 2)) "$DOCS_HEIGHT"
    sleep "$SHORT_PAUSE"
}

# Runs the selected analysis (F5) and waits for it
docs_run() {
    ui_key F5
    sleep "${1:-$LONG_PAUSE}"
    docs_check_alive "running the analysis"
}

# =============================================================================
# Shots
# =============================================================================

# Shoots the whole window into docs/images/NAME.png, or a part of it given as WIDTHxHEIGHT+X+Y
docs_save() {
    local name=$1 crop=${2:-}
    local raw="$DOCS_SCRATCH/shots/$name.png"
    docs_check_alive "before the shot of $name"
    # The pointer is left where the last step put it; the caller moves it away when its hover would show
    "$GUI_HELPER" --window-id "$DOCS_WID" shot "$raw" >/dev/null 2>&1
    local size
    size=$(magick identify -format '%wx%h' "$raw")
    [[ "$size" == "${DOCS_WIDTH}x${DOCS_HEIGHT}" ]] || docs_fail "the shot of $name is $size, not the window size"
    if [[ -n "$crop" ]]; then
        magick "$raw" -crop "$crop" +repage "$IMAGES_DIR/$name.png"
    else
        cp "$raw" "$IMAGES_DIR/$name.png"
    fi
    echo "saved docs/images/$name.png ($(magick identify -format '%wx%h' "$IMAGES_DIR/$name.png"))"
}
