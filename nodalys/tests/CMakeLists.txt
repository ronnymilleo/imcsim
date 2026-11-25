# =============================================================================
# Nodalys Tests
# =============================================================================

include(Catch)

add_executable(nodalys-tests
    analyses/ac_sens/ac_sens_analysis_tests.cpp
    analyses/ac_sens/ac_sens_netlist_tests.cpp
    analyses/analysis_preamble_tests.cpp
    analyses/frequency_sweep_tests.cpp
    analyses/hb/hb_analysis_tests.cpp
    analyses/hb/hb_bias_tests.cpp
    analyses/hb/hb_setup_tests.cpp
    analyses/hb/hb_spectrum_tests.cpp
    analyses/hb/hb_workspace_tests.cpp
    analyses/nodeset_tests.cpp
    analyses/pz/pole_zero_oracle.cpp
    analyses/pz/pz_analysis_tests.cpp
    analyses/sens_analysis_tests.cpp
    analyses/solution_readout_tests.cpp
    analyses/sp_analysis_tests.cpp
    analyses/transient_analysis_tests.cpp
    api/circuit_immutability_tests.cpp
    devices/behavioral_device_tests.cpp
    devices/bjt_device_tests.cpp
    devices/fuse_device_tests.cpp
    devices/mosfet_device_tests.cpp
    devices/random_draws_tests.cpp
    devices/smooth_logic_tests.cpp
    devices/thermal_tests.cpp
    devices/thyristor_device_tests.cpp
    devices/transient_noise_tests.cpp
    devices/transmission_line_tests.cpp
    devices/waveforms_tests.cpp
    export/json_exporter_tests.cpp
    export/raw_plot_tests.cpp
    expression/expression_tests.cpp
    linear/fft_tests.cpp
    linear/linear_tests.cpp
    mna/mna_system_tests.cpp
    parser/classified_lines_tests.cpp
    parser/netlist_deck_tests.cpp
    parser/netlist_elements_tests.cpp
    parser/netlist_line_nodes_tests.cpp
    parser/netlist_nodes_tests.cpp
    parser/netlist_subcircuits_tests.cpp
    parser/parser_and_export_tests.cpp
    parser/source_line_tests.cpp
    parser/source_waveforms_tests.cpp
    parser/spice_values_tests.cpp
    parser/subcircuit_text_tests.cpp
    text/text_case_tests.cpp
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
