#!/usr/bin/env bash
# Tutorial, step 1: the completed source, resistor, capacitor and ground circuit, drawn in the empty editor.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture tutorial_rc
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
docs_park_pointer
docs_save tutorial_build
