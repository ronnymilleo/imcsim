# =============================================================================
# Tests
# =============================================================================

add_subdirectory(${PROJECT_SOURCE_DIR}/vendor/Catch2 ${CMAKE_BINARY_DIR}/vendor/Catch2)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/vendor/Catch2/extras)

add_executable(imcsim-tests
            circuit_tests.cpp
            component_tests.cpp
            connectivity_tests.cpp
            helpers_tests.cpp
            schematic_file_tests.cpp
            schematic_tests.cpp
            simulator_tests.cpp
            spice_value_tests.cpp
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
catch_discover_tests(imcsim-tests)
