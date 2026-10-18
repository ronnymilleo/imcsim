# =============================================================================
# Tests
# =============================================================================

add_subdirectory(${PROJECT_SOURCE_DIR}/vendor/Catch2 ${CMAKE_BINARY_DIR}/vendor/Catch2)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/vendor/Catch2/extras)

add_executable(imcsim-tests
            analysis_suggestions_tests.cpp
            circuit_snapshot_tests.cpp
            clipboard_tests.cpp
            circuit_tests.cpp
            component_tests.cpp
            connectivity_tests.cpp
            element_factory_tests.cpp
            current_flow_tests.cpp
            examples_tests.cpp
            group_editing_tests.cpp
            heat_glow_tests.cpp
            helpers_tests.cpp
            led_glow_tests.cpp
            logic_trace_tests.cpp
            parameter_step_tests.cpp
            playback_tests.cpp
            plot_export_tests.cpp
            plot_helpers_tests.cpp
            probing_tests.cpp
            schematic_canvas_tests.cpp
            schematic_file_tests.cpp
            schematic_tests.cpp
            selection_tests.cpp
            simulator_tests.cpp
            spectrum_tests.cpp
            spice_value_tests.cpp
            standard_values_tests.cpp
            svg_writer_tests.cpp
            trace_fit_state_tests.cpp
            trace_math_tests.cpp
            trace_statistics_tests.cpp
            ui_element_tests.cpp
            ui_wire_tests.cpp
            wire_editing_tests.cpp
)

target_include_directories(imcsim-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(imcsim-tests
        PRIVATE
            imcsim-gui-lib
            Catch2::Catch2WithMain
)

include(Catch)
# Under LeakSanitizer, the suppression file hides the leaks of libngspice itself; other builds ignore the variable
catch_discover_tests(imcsim-tests
        PROPERTIES
            ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan_suppressions.txt"
)
