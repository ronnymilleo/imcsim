#!/usr/bin/env bash
# README and tutorial: the "Small-signal model of a BJT" example, a 2N3904 stage next to its hybrid-pi model, and
# the magnitude and phase of both outputs from its AC sweep.

set -euo pipefail
source "$(dirname "$0")/lib.sh"

docs_launch
docs_open_example hybrid_pi_model
docs_pick_analysis AC
docs_run
docs_park_pointer
docs_save bode
