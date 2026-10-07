# =============================================================================
# Application GUI
# =============================================================================

add_library(imcsim-gui-lib
        STATIC
            application.cpp
            connectivity.cpp
            element_factory.cpp
            helpers.cpp
            node_colors.cpp
            schematic.cpp
            schematic_file.cpp
            theme.cpp
            ui_elements/ui_capacitor.cpp
            ui_elements/ui_current_source.cpp
            ui_elements/ui_diode.cpp
            ui_elements/ui_element.cpp
            ui_elements/ui_ground.cpp
            ui_elements/ui_inductor.cpp
            ui_elements/ui_resistor.cpp
            ui_elements/ui_source.cpp
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
)

target_link_libraries(imcsim-gui-lib
        PUBLIC
            imcsim-core-lib
            imgui
            implot
        PRIVATE
            nlohmann_json::nlohmann_json
)
