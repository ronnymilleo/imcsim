# =============================================================================
# Application GUI
# =============================================================================

add_library(imcsim-gui-lib
        STATIC
            application.cpp
            connectivity.cpp
            current_flow.cpp
            element_factory.cpp
            examples.cpp
            helpers.cpp
            node_colors.cpp
            plot_helpers.cpp
            probing.cpp
            schematic.cpp
            schematic_file.cpp
            theme.cpp
            trace_math.cpp
            trace_statistics.cpp
            ui_elements/ui_bjt.cpp
            ui_elements/ui_capacitor.cpp
            ui_elements/ui_current_source.cpp
            ui_elements/ui_diode.cpp
            ui_elements/ui_element.cpp
            ui_elements/ui_ground.cpp
            ui_elements/ui_inductor.cpp
            ui_elements/ui_mosfet.cpp
            ui_elements/ui_resistor.cpp
            ui_elements/ui_source.cpp
            ui_elements/ui_transistor.cpp
            ui_elements/ui_vcc.cpp
            ui_elements/ui_voltage_source.cpp
            ui_elements/ui_wire.cpp
            value_field.cpp
            vulkan_context.cpp
            windows/app_window.cpp
            windows/editor_window.cpp
            windows/netlist_window.cpp
            windows/output_window.cpp
            windows/properties_window.cpp
            windows/simulation_window.cpp
            wire_editing.cpp
)

target_include_directories(imcsim-gui-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/ui_elements
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

target_link_libraries(imcsim-gui-lib
        PUBLIC
            imcsim-core-lib
            imgui
            implot
        PRIVATE
            nlohmann_json::nlohmann_json
)
