# Docs images

Scripts that retake every image under `docs/images/`, so they can be refreshed at each release. Each script launches
an isolated imcsim, puts a circuit in, takes the shot and closes the instance; `update_all.sh` runs them all.

## Prerequisites

- Linux on Hyprland (0.56 or later, with `hyprctl repl` and its Lua dispatchers), on a monitor of at least 1920x1080
  at scale 1. The scripts take workspace 4 (`DOCS_WORKSPACE` changes it) and put the instance there in full screen,
  then return to the workspace you were on. They switch the screen to that workspace while they run, so leave the
  keyboard and mouse alone until they finish.
- gui-helper, built at
  `/mnt/DevDrive/Develop/Projects/gui-helper/build/gui-helper` (or set `GUI_HELPER`). It drives the window through
  XWayland, which is why imcsim runs with `SDL_VIDEO_DRIVER=x11`.
- `grim` (gui-helper's shots), `magick` (ImageMagick 7, for crops), `tesseract` (OCR, to find text on screen),
  `wl-copy`, `jq`.
- imcsim built: the newer of `out/release/bin/imcsim` and `out/debug/bin/imcsim`, or `IMCSIM_BIN`.

The instance gets its own data folder (`XDG_DATA_HOME` under `DOCS_SCRATCH`, by default
`~/.cache/imcsim-docs-images`) with a fixed `preferences.json` (Graphite, IEC symbols, plain wires, default live
effects) and no `imgui.ini`, so the default layout and the default look are shot whatever your own settings are.

## Running

```
tools/docs_images/update_all.sh            # every image; lists the scripts that failed at the end
tools/docs_images/tutorial_run.sh          # one image
tools/docs_images/theme_pair.sh "Cyber Slate"
DOCS_KEEP_OPEN=1 tools/docs_images/bode.sh # leave the instance open afterwards, to look at it
```

Look at every image before committing: a script checks the window size and that imcsim did not crash, not that the
picture is right.

## What each script shows

Full window, 1920x1080, Graphite:

| Script | Image | Shows |
|---|---|---|
| `screenshot.sh` | `screenshot.png` | README: the RC low-pass example after its transient, the playhead at about two thirds |
| `bode.sh` | `bode.png` | The small-signal BJT example (`hybrid_pi_model`) and its AC sweep |
| `dc_sweep.sh` | `dc_sweep.png` | The BJT output characteristics example and its stepped DC sweep |
| `tutorial_build.sh` | `tutorial_build.png` | Tutorial step 1: source, resistor, capacitor and ground (`fixtures/tutorial_rc.imcsim`) |
| `tutorial_search.sh` | `tutorial_search.png` | Step 2: the part search with `dio`, over the start of the rectifier |
| `tutorial_properties.sh` | `tutorial_properties.png` | Step 3: the popover of Vin1, set to Pulse |
| `tutorial_probe.sh` | `tutorial_probe.png` | Step 4: probe mode, with I(D1) and V(2) marked |
| `tutorial_run.sh` | `tutorial_run.png` | Step 5: the transient of the rectifier |
| `tutorial_value.sh` | `tutorial_value.png` | Step 6: `220` typed onto the selected R1 |
| `tutorial_cursors.sh` | `tutorial_cursors.png` | Step 7: cursors A on a peak of V(2) and B at the bottom of the sag (220 Ohm load) |
| `tutorial_math.sh` | `tutorial_math.png` | Step 7: the math channel V(2) x I(R1) |

Crops of the full window:

| Script | Image | Shows |
|---|---|---|
| `tutorial_heat_map.sh` | `tutorial_heat_map.png` | Step 8: a divider of four 1k resistors, wires colored by voltage, scale at the bottom left |
| `tutorial_export.sh` | `tutorial_export.png` | Step 9: the export options of the transient plot |
| `blocks_contents.sh` | `blocks_contents.png` | User guide: the inside of RC_LOWPASS from the Two RC filters example |
| `blocks_dialog.sh` | `blocks_dialog.png` | User guide: the pin and parameter dialog of RC_LOWPASS |
| `spice_import.sh` | `spice_import.png` | User guide: the Import SPICE library dialog on `fixtures/vendor.lib` |

Themes, 1280x800 floating window: `theme_pair.sh THEME` writes `theme_<name>_schematic.png` and
`theme_<name>_output.png` for docs/themes.md, the small-signal BJT example and its AC sweep. Its window starts from
`fixtures/tabs_1280x800.ini`, a layout where Schematic, Output and Properties share one node as tabs, so each image
shows one of them at full size.

## How the scripts work

- `lib.sh` holds everything shared: launch and cleanup (only the instance's own PID and its children are killed), the
  window placement, input through gui-helper, finding text with OCR, opening files, picking an analysis, menus and
  the shots.
- Circuits are opened with File > Open (Ctrl+O). imcsim asks SDL for its file dialogs, and SDL runs `zenity`; the
  scripts put `fake_zenity/` first on the `PATH` of the instance, whose `zenity` answers a file selection with the
  path the script wrote beforehand, so no dialog has to be driven. Anything else goes to the real zenity.
- The tutorial circuits are files in `fixtures/` (the rectifier at each step, the divider, the library to import);
  the others are the Examples of `app-gui/examples/`. A file keeps the analysis, its settings, the probes and the math
  channels, so a shot needs few clicks.
- Menu items, analysis names, tab names and labels are found on screen with tesseract (`docs_find_text`,
  `ui_click_text`, `docs_menu`) instead of fixed coordinates.
- Parts are clicked through the grid coordinates of their file: `docs_calibrate_on_wire` finds the longest
  horizontal wire on the canvas, whose grid ends the script gives, and works out the zoom and where grid point
  (0, 0) is; `docs_grid_point` turns grid points into window points.

## Coordinates that depend on the layout

When the layout changes, these are the constants to check, all named at the top of `lib.sh` or of the script:

- `lib.sh`: `MENU_BAR_BOTTOM`, `COMMAND_BAR_Y` and `COMMAND_BAR_BOTTOM` (the command bar under the menu bar),
  `ANALYSIS_BUTTON_FROM_RIGHT` (the analysis button, from the right edge), `ANALYSIS_LIST_WIDTH`, `CANVAS_LEFT` (right
  of the part rail), and `docs_layout`, which places the editor, Output and the side column from the shares of
  `Application::SetupDefaultLayout()` (18% for the side column, 56% of the rest for Output). `WIRE_COLOR` is the
  Graphite wire color the calibration looks for.
- `screenshot.sh`: `PLAYHEAD_FROM_LOOP`, how far right of the Loop label the playback slider is clicked.
- `tutorial_cursors.sh`: `CHECKBOX_FROM_LABEL` (the Cursors checkbox, left of its label); the time axis is read from
  its tick labels and the cursors are dragged from a third and two thirds of the run, where new cursors start.
- `tutorial_export.sh`, `tutorial_heat_map.sh`, `blocks_contents.sh`: the crop heights and the pans
  (`PAN_DOWN`, `PAN_UP`, `ZOOM_STEPS`).
- `blocks_dialog.sh` and `spice_import.sh`: the dialog sizes (`DIALOG_WIDTH`, `DIALOG_HEIGHT`), `TITLE_TO_TOP` and
  `LABEL_TO_EDGE_CHOICE` (from a port label to its edge choice).
- `theme_pair.sh`: `TAB_BAR_TOP`/`TAB_BAR_BOTTOM`, and `fixtures/tabs_1280x800.ini`, which needs retaking if the
  window titles or the dock layout change: launch an instance at 1280x800 (`docs_launch --size 1280x800` with
  `DOCS_KEEP_OPEN=1`), drag the Output and Properties
  tabs onto the Schematic tab bar, wait five seconds for ImGui to save, and copy `imgui.ini` from the data folder.
