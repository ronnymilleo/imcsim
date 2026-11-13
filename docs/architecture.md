# Architecture

The include dependencies between the folders of imcsim, drawn from the `#include` lines of the sources by
`tools/include_layers.py`. An arrow from A to B means that files of A include files of B, and its number counts
those includes. The folders of each library are listed from the lowest layer to the highest, and a folder may only
include its own layer and the ones below it. A dashed red arrow goes the other way: it is a known violation, listed
in `tools/include_layers_baseline.json`, which `tools/include_layers.py --check` lets only shrink. Tracked
`app-core` and `app-gui` sources are read; `vendor/` and `tests/` are not.

Regenerate this file after a change that adds, removes or moves an include, with the project virtual environment
(the script uses only the standard library):

```
.venv/bin/python tools/include_layers.py
```

The CI runs `python tools/include_layers.py --check`, which fails when this file is out of date or when an include
goes up a layer without being in the baseline.

## Libraries

```mermaid
flowchart TD
    app_gui[app-gui]
    app_core[app-core]
    nodalys[nodalys]
    app_core -->|3| nodalys
    app_gui -->|270| app_core
```

## app-core

```mermaid
flowchart TD
    app_core_spice[spice]
    app_core_circuit[circuit]
    app_core_simulation[simulation]
    app_core_backends[backends]
    app_core_backends -->|65| app_core_circuit
    app_core_backends -->|45| app_core_simulation
    app_core_backends -->|2| app_core_spice
    app_core_circuit -->|8| app_core_spice
    app_core_simulation -->|77| app_core_circuit
    app_core_simulation -->|2| app_core_spice
```

Layers, lowest first:

1. `spice`
2. `circuit`
3. `simulation`
4. `backends`

## app-gui

```mermaid
flowchart TD
    app_gui_base[base]
    app_gui_canvas[canvas]
    app_gui_ui_elements[ui_elements]
    app_gui_schematic[schematic]
    app_gui_results[results]
    app_gui_analysis[analysis]
    app_gui_plots[plots]
    app_gui_overlays[overlays]
    app_gui_windows[windows]
    app_gui_root[root]
    app_gui_analysis -->|5| app_gui_base
    app_gui_analysis -->|6| app_gui_results
    app_gui_analysis -->|8| app_gui_schematic
    app_gui_canvas -->|1| app_gui_base
    app_gui_overlays -->|1| app_gui_analysis
    app_gui_overlays -->|8| app_gui_base
    app_gui_overlays -->|2| app_gui_canvas
    app_gui_overlays -->|14| app_gui_results
    app_gui_overlays -->|5| app_gui_schematic
    app_gui_overlays -->|6| app_gui_ui_elements
    app_gui_plots -->|10| app_gui_base
    app_gui_plots -->|10| app_gui_results
    app_gui_plots -->|8| app_gui_schematic
    app_gui_results -->|2| app_gui_base
    app_gui_results -->|7| app_gui_schematic
    app_gui_results -->|5| app_gui_ui_elements
    app_gui_root -->|3| app_gui_analysis
    app_gui_root -->|1| app_gui_base
    app_gui_root -->|1| app_gui_overlays
    app_gui_root -->|2| app_gui_results
    app_gui_root -->|5| app_gui_schematic
    app_gui_root -->|9| app_gui_windows
    app_gui_schematic -->|23| app_gui_base
    app_gui_schematic -->|1| app_gui_canvas
    app_gui_schematic -->|61| app_gui_ui_elements
    app_gui_ui_elements -->|7| app_gui_base
    app_gui_ui_elements -->|2| app_gui_canvas
    app_gui_windows -->|11| app_gui_analysis
    app_gui_windows -->|55| app_gui_base
    app_gui_windows -->|3| app_gui_canvas
    app_gui_windows -->|3| app_gui_overlays
    app_gui_windows -->|54| app_gui_plots
    app_gui_windows -->|40| app_gui_results
    app_gui_windows -->|109| app_gui_schematic
    app_gui_windows -->|3| app_gui_ui_elements
```

Layers, lowest first:

1. `base`
2. `canvas`
3. `ui_elements`
4. `schematic`
5. `results`
6. `analysis`
7. `plots`
8. `overlays`
9. `windows`
10. `root`
