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
    app_gui -->|218| app_core
```

## app-core

```mermaid
flowchart TD
    app_core_spice[spice]
    app_core_model[model]
    app_core_components[components]
    app_core_simulation[simulation]
    app_core_ngspice[ngspice]
    app_core_nodalys[nodalys]
    app_core_components -->|3| app_core_model
    app_core_components -->|1| app_core_spice
    app_core_model -->|23| app_core_components
    app_core_model -->|2| app_core_spice
    app_core_ngspice -->|2| app_core_components
    app_core_ngspice -->|1| app_core_model
    app_core_ngspice -->|12| app_core_simulation
    app_core_ngspice -->|1| app_core_spice
    app_core_nodalys -->|26| app_core_components
    app_core_nodalys -->|4| app_core_model
    app_core_nodalys -->|4| app_core_simulation
    app_core_nodalys -->|3| app_core_spice
    app_core_simulation -->|28| app_core_components
    app_core_simulation -->|16| app_core_model
    app_core_simulation -.->|1| app_core_ngspice
    app_core_simulation -->|3| app_core_spice
    app_core_spice -.->|2| app_core_components
    app_core_spice -.->|2| app_core_model
    linkStyle 14 stroke:#d33,stroke-width:2px
    linkStyle 16 stroke:#d33,stroke-width:2px
    linkStyle 17 stroke:#d33,stroke-width:2px
```

Layers, lowest first:

1. `spice`
2. `model`, `components`
3. `simulation`
4. `ngspice`, `nodalys`

## app-gui

```mermaid
flowchart TD
    app_gui_ui_elements[ui_elements]
    app_gui_editing[editing]
    app_gui_document[document]
    app_gui_analysis[analysis]
    app_gui_plots[plots]
    app_gui_drawing[drawing]
    app_gui_windows[windows]
    app_gui_root[root]
    app_gui_analysis -->|6| app_gui_document
    app_gui_analysis -.->|4| app_gui_drawing
    app_gui_analysis -->|5| app_gui_editing
    app_gui_analysis -.->|6| app_gui_plots
    app_gui_analysis -.->|3| app_gui_root
    app_gui_analysis -->|3| app_gui_ui_elements
    app_gui_document -.->|3| app_gui_analysis
    app_gui_document -.->|2| app_gui_drawing
    app_gui_document -->|13| app_gui_editing
    app_gui_document -.->|4| app_gui_plots
    app_gui_document -.->|5| app_gui_root
    app_gui_document -->|8| app_gui_ui_elements
    app_gui_drawing -->|9| app_gui_analysis
    app_gui_drawing -->|2| app_gui_document
    app_gui_drawing -->|4| app_gui_editing
    app_gui_drawing -->|1| app_gui_plots
    app_gui_drawing -.->|8| app_gui_root
    app_gui_drawing -->|10| app_gui_ui_elements
    app_gui_editing -->|2| app_gui_document
    app_gui_editing -.->|1| app_gui_drawing
    app_gui_editing -.->|8| app_gui_root
    app_gui_editing -->|44| app_gui_ui_elements
    app_gui_plots -->|1| app_gui_analysis
    app_gui_plots -->|5| app_gui_document
    app_gui_plots -.->|7| app_gui_drawing
    app_gui_plots -->|1| app_gui_editing
    app_gui_root -->|4| app_gui_analysis
    app_gui_root -->|5| app_gui_document
    app_gui_root -->|6| app_gui_drawing
    app_gui_root -->|3| app_gui_editing
    app_gui_root -->|1| app_gui_ui_elements
    app_gui_root -->|7| app_gui_windows
    app_gui_ui_elements -.->|5| app_gui_drawing
    app_gui_ui_elements -.->|2| app_gui_plots
    app_gui_ui_elements -.->|2| app_gui_root
    app_gui_windows -->|24| app_gui_analysis
    app_gui_windows -->|29| app_gui_document
    app_gui_windows -->|32| app_gui_drawing
    app_gui_windows -->|26| app_gui_editing
    app_gui_windows -->|58| app_gui_plots
    app_gui_windows -.->|12| app_gui_root
    app_gui_windows -->|2| app_gui_ui_elements
    linkStyle 1 stroke:#d33,stroke-width:2px
    linkStyle 3 stroke:#d33,stroke-width:2px
    linkStyle 4 stroke:#d33,stroke-width:2px
    linkStyle 6 stroke:#d33,stroke-width:2px
    linkStyle 7 stroke:#d33,stroke-width:2px
    linkStyle 9 stroke:#d33,stroke-width:2px
    linkStyle 10 stroke:#d33,stroke-width:2px
    linkStyle 16 stroke:#d33,stroke-width:2px
    linkStyle 19 stroke:#d33,stroke-width:2px
    linkStyle 20 stroke:#d33,stroke-width:2px
    linkStyle 24 stroke:#d33,stroke-width:2px
    linkStyle 32 stroke:#d33,stroke-width:2px
    linkStyle 33 stroke:#d33,stroke-width:2px
    linkStyle 34 stroke:#d33,stroke-width:2px
    linkStyle 40 stroke:#d33,stroke-width:2px
```

Layers, lowest first:

1. `ui_elements`
2. `editing`, `document`
3. `analysis`
4. `plots`
5. `drawing`
6. `windows`
7. `root`
