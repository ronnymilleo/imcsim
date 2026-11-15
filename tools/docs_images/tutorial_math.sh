#!/usr/bin/env bash
# Tutorial, step 7: the math channel M1 = V(2) x I(R1), the power the 220 Ohm load takes, plotted on an axis of its
# own beside V(2) and I(D1), with its statistics. The channel comes with fixtures/rectifier_math.imcsim.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_fixture rectifier_math
docs_zoom_canvas "$SMALL_CIRCUIT_ZOOM_STEPS"
docs_run
docs_park_pointer
docs_save tutorial_math
