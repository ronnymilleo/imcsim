#!/usr/bin/env bash
# README and tutorial: the "BJT output characteristics" example, a 2N3904 whose collector voltage is swept for five
# base currents, each curve labeled with its current.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_example bjt_output_characteristics
docs_pick_analysis DC
docs_run
docs_park_pointer
docs_save dc_sweep
