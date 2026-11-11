#!/usr/bin/env bash
# Retakes every image of the docs, one script at a time, and lists the scripts that failed at the end. Each script
# launches and closes its own instance, so one failure does not stop the others.

set -uo pipefail

cd "$(dirname "$0")"

SCRIPTS=(
    screenshot bode dc_sweep
    tutorial_build tutorial_search tutorial_properties tutorial_probe tutorial_run tutorial_value
    tutorial_cursors tutorial_math tutorial_heat_map tutorial_export
    blocks_contents blocks_dialog spice_import
)
# As View > Theme names them; theme_pair.sh writes both images of each
THEMES=(Graphite Ember Phosphor Oscilloscope "Cyber Slate" Blueprint Amber Paper Vintage)

# Every script goes back to the workspace this run started on
DOCS_RETURN_WORKSPACE=$(hyprctl activeworkspace -j | jq -r .id)
export DOCS_RETURN_WORKSPACE

failed=()
for script in "${SCRIPTS[@]}"; do
    echo "== $script"
    ./"$script".sh || failed+=("$script")
done
for theme in "${THEMES[@]}"; do
    echo "== theme_pair $theme"
    ./theme_pair.sh "$theme" || failed+=("theme_pair.sh \"$theme\"")
done

if [[ ${#failed[@]} -gt 0 ]]; then
    echo
    echo "Failed:"
    printf '  %s\n' "${failed[@]}"
    exit 1
fi
echo
echo "All images retaken; look at each one before committing"
