# =============================================================================
# Nodalys Tests
# =============================================================================

include(Catch)

add_executable(nodalys-tests
    fft_tests.cpp
    hb_analysis_tests.cpp
    linear_tests.cpp
    parser_and_export_tests.cpp
    sens_analysis_tests.cpp
    smooth_logic_tests.cpp
)

target_include_directories(nodalys-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/nodalys/src
)

target_link_libraries(nodalys-tests
    PRIVATE
        nodalys
        Catch2::Catch2WithMain
)

if(COMMAND imcsim_target_coverage)
    imcsim_target_coverage(nodalys-tests)
endif()

catch_discover_tests(nodalys-tests)
