#!/usr/bin/env bash
# Tutorial, step 5: the transient of the rectifier, its output voltage V(2) and the diode current I(D1), with their
# statistics.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture rectifier_probed
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
docs_pick_analysis Transient
docs_run
docs_park_pointer
docs_save tutorial_run
