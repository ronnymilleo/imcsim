# =============================================================================
# Application GUI
# =============================================================================

add_library(imcsim-gui-lib
        STATIC
            analysis/analysis_controls.cpp
            analysis/analysis_inputs.cpp
            analysis/analysis_settings_panel.cpp
            analysis/auto_rerun.cpp
            analysis/circuit_snapshot.cpp
            analysis/fuse_readings.cpp
            analysis/harmonic_balance_view.cpp
            analysis/logic_outputs.cpp
            analysis/monte_carlo_results.cpp
            analysis/parameter_step.cpp
            analysis/playback.cpp
            analysis/point_window.cpp
            analysis/pole_zero_view.cpp
            analysis/s_parameter_view.cpp
            analysis/sensitivity_view.cpp
            analysis/transient_window.cpp
            analysis/truth_table.cpp
            application.cpp
            document/block_nesting.cpp
            document/clipboard.cpp
            document/examples.cpp
            document/file_dialog.cpp
            document/ready_blocks.cpp
            document/schematic.cpp
            document/schematic_file.cpp
            document/schematic_file_actions.cpp
            document/simulation_settings.cpp
            document/spice_import.cpp
            document/subcircuit_pins.cpp
            drawing/current_flow.cpp
            drawing/fuse_flash.cpp
            drawing/glow_renderer.cpp
            drawing/heat_glow.cpp
            drawing/led_glow.cpp
            drawing/node_colors.cpp
            drawing/readout_placement.cpp
            drawing/schematic_canvas.cpp
            drawing/schematic_drawing.cpp
            drawing/schematic_overlays.cpp
            drawing/svg_writer.cpp
            drawing/text_fitting.cpp
            drawing/theme.cpp
            drawing/tool_icons.cpp
            drawing/trace_swatch.cpp
            editing/block_wiring.cpp
            editing/canvas_view.cpp
            editing/connectivity.cpp
            editing/editor_mode.cpp
            editing/element_factory.cpp
            editing/group_editing.cpp
            editing/live_input.cpp
            editing/part_catalog.cpp
            editing/part_names.cpp
            editing/probing.cpp
            editing/selection.cpp
            editing/wire_editing.cpp
            editing/wire_routing.cpp
            helpers.cpp
            part_editor.cpp
            plots/logic_bus.cpp
            plots/logic_buses.cpp
            plots/logic_trace.cpp
            plots/math_channel.cpp
            plots/math_channels.cpp
            plots/plot_export.cpp
            plots/plot_exporter.cpp
            plots/plot_figures.cpp
            plots/plot_helpers.cpp
            plots/smith_chart.cpp
            plots/spectrum.cpp
            plots/statistics_table.cpp
            plots/touchstone.cpp
            plots/trace_fit_state.cpp
            plots/trace_math.cpp
            plots/trace_statistics.cpp
            preferences.cpp
            ui_elements/ui_behavioral_source.cpp
            ui_elements/ui_bjt.cpp
            ui_elements/ui_capacitor.cpp
            ui_elements/ui_clock.cpp
            ui_elements/ui_controlled_source.cpp
            ui_elements/ui_controlled_switch.cpp
            ui_elements/ui_current_source.cpp
            ui_elements/ui_diode.cpp
            ui_elements/ui_element.cpp
            ui_elements/ui_fuse.cpp
            ui_elements/ui_ground.cpp
            ui_elements/ui_inductor.cpp
            ui_elements/ui_logic_box.cpp
            ui_elements/ui_logic_input.cpp
            ui_elements/ui_logic_output.cpp
            ui_elements/ui_logic_part.cpp
            ui_elements/ui_mosfet.cpp
            ui_elements/ui_net_label.cpp
            ui_elements/ui_op_amp.cpp
            ui_elements/ui_potentiometer.cpp
            ui_elements/ui_resistor.cpp
            ui_elements/ui_seven_segment.cpp
            ui_elements/ui_source.cpp
            ui_elements/ui_subcircuit.cpp
            ui_elements/ui_subcircuit_pin.cpp
            ui_elements/ui_switch.cpp
            ui_elements/ui_transformer.cpp
            ui_elements/ui_transistor.cpp
            ui_elements/ui_vcc.cpp
            ui_elements/ui_voltage_source.cpp
            ui_elements/ui_wire.cpp
            value_field.cpp
            windows/app_window.cpp
            windows/dock_layout.cpp
            windows/editor_toolbar.cpp
            windows/editor_window.cpp
            windows/netlist_window.cpp
            windows/output_tabs/ac_sweep_tab.cpp
            windows/output_tabs/dc_sweep_tab.cpp
            windows/output_tabs/harmonic_balance_tab.cpp
            windows/output_tabs/monte_carlo_tab.cpp
            windows/output_tabs/noise_tab.cpp
            windows/output_tabs/operating_point_tab.cpp
            windows/output_tabs/plot_tab.cpp
            windows/output_tabs/plot_widgets.cpp
            windows/output_tabs/pole_zero_tab.cpp
            windows/output_tabs/result_tables.cpp
            windows/output_tabs/s_parameter_tab.cpp
            windows/output_tabs/spectrum_tab.cpp
            windows/output_tabs/transient_tab.cpp
            windows/output_window.cpp
            windows/part_picker.cpp
            windows/part_rail.cpp
            windows/preferences_window.cpp
            windows/properties_window.cpp
            windows/simulation_window.cpp
            windows/spice_block_dialog.cpp
)

target_include_directories(imcsim-gui-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# =============================================================================
# Example schematics
# =============================================================================

# Each example is compiled in as a string, so the binary needs no data files next to it. Editing or adding an
# example reruns CMake on the next build
file(GLOB EXAMPLE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.imcsim)
set(EXAMPLE_FILE_ENTRIES "")
foreach(example_file IN LISTS EXAMPLE_FILES)
    get_filename_component(example_name ${example_file} NAME)
    file(READ ${example_file} example_text)
    string(APPEND EXAMPLE_FILE_ENTRIES "    {\"${example_name}\", R\"imcsim(${example_text})imcsim\"},\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${example_file})
endforeach()
configure_file(example_files.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/example_files.h @ONLY)

# =============================================================================
# Ready blocks
# =============================================================================

# The ready logic blocks are compiled in the same way, each a file with one instance and its definition
file(GLOB READY_BLOCK_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ready_blocks/*.imcsim)
set(READY_BLOCK_FILE_ENTRIES "")
foreach(block_file IN LISTS READY_BLOCK_FILES)
    get_filename_component(block_name ${block_file} NAME)
    file(READ ${block_file} block_text)
    string(APPEND READY_BLOCK_FILE_ENTRIES "    {\"${block_name}\", R\"imcsim(${block_text})imcsim\"},\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${block_file})
endforeach()
configure_file(ready_block_files.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/ready_block_files.h @ONLY)

# =============================================================================
# Bundled fonts
# =============================================================================

# The fonts are compiled in as byte arrays, like the examples, so text looks the same on every system
function(read_font_bytes font_file name)
    file(READ ${font_file} font_hex HEX)
    string(LENGTH "${font_hex}" hex_length)
    math(EXPR font_size "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_bytes "${font_hex}")
    set(${name}_BYTES "${font_bytes}" PARENT_SCOPE)
    set(${name}_SIZE ${font_size} PARENT_SCOPE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${font_file})
endfunction()
read_font_bytes(${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf INTER_FONT)
read_font_bytes(${PROJECT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf MONOSPACE_FONT)
configure_file(font_files.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/font_files.h @ONLY)

target_link_libraries(imcsim-gui-lib
        PUBLIC
            imcsim-core-lib
            imgui
            implot
        PRIVATE
            nlohmann_json::nlohmann_json
)

imcsim_target_coverage(imcsim-gui-lib)
