# =============================================================================
# Nodalys Tests
# =============================================================================

include(Catch)

add_executable(nodalys-tests
    behavioral_device_tests.cpp
    expression_tests.cpp
    fft_tests.cpp
    fuse_device_tests.cpp
    hb_analysis_tests.cpp
    linear_tests.cpp
    netlist_elements_tests.cpp
    parser_and_export_tests.cpp
    sens_analysis_tests.cpp
    smooth_logic_tests.cpp
    transient_analysis_tests.cpp
)

target_include_directories(nodalys-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/nodalys/src
)

target_link_libraries(nodalys-tests
    PRIVATE
        nodalys
        nodalys-cli-lib
        Catch2::Catch2WithMain
)

if(COMMAND imcsim_target_coverage)
    imcsim_target_coverage(nodalys-tests)
endif()


catch_discover_tests(nodalys-tests)
