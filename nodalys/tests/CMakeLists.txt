# =============================================================================
# Nodalys Tests
# =============================================================================

include(Catch)

add_executable(nodalys-tests
    ac_sens_analysis_tests.cpp
    ac_sens_netlist_tests.cpp
    analysis_preamble_tests.cpp
    behavioral_device_tests.cpp
    bjt_device_tests.cpp
    classified_lines_tests.cpp
    expression_tests.cpp
    fft_tests.cpp
    frequency_sweep_tests.cpp
    fuse_device_tests.cpp
    hb_analysis_tests.cpp
    hb_bias_tests.cpp
    hb_setup_tests.cpp
    hb_spectrum_tests.cpp
    hb_workspace_tests.cpp
    json_exporter_tests.cpp
    linear_tests.cpp
    mosfet_device_tests.cpp
    netlist_deck_tests.cpp
    netlist_elements_tests.cpp
    netlist_line_nodes_tests.cpp
    netlist_nodes_tests.cpp
    netlist_subcircuits_tests.cpp
    nodeset_tests.cpp
    parser_and_export_tests.cpp
    pole_zero_oracle.cpp
    pz_analysis_tests.cpp
    random_draws_tests.cpp
    raw_plot_tests.cpp
    sens_analysis_tests.cpp
    source_line_tests.cpp
    source_waveforms_tests.cpp
    spice_values_tests.cpp
    subcircuit_text_tests.cpp
    smooth_logic_tests.cpp
    solution_readout_tests.cpp
    sp_analysis_tests.cpp
    text_case_tests.cpp
    thermal_tests.cpp
    transient_analysis_tests.cpp
    transient_noise_tests.cpp
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
