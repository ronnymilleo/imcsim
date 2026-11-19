# =============================================================================
# Nodalys simulation engine (autonomous clean-room solver, zero dependencies)
# =============================================================================

add_library(nodalys STATIC
    src/analyses/ac_analysis.cpp
    src/analyses/ac_sens_analysis.cpp
    src/analyses/ac_sens_bias.cpp
    src/analyses/ac_sens_elements.cpp
    src/analyses/analysis_preamble.cpp
    src/analyses/dc_analysis.cpp
    src/analyses/frequency_sweep.cpp
    src/analyses/hb_analysis.cpp
    src/analyses/hb_bias.cpp
    src/analyses/hb_residual.cpp
    src/analyses/hb_result.cpp
    src/analyses/hb_setup.cpp
    src/analyses/hb_solve.cpp
    src/analyses/hb_spectrum.cpp
    src/analyses/hb_workspace.cpp
    src/analyses/noise_analysis.cpp
    src/analyses/port_matrices.cpp
    src/analyses/pz_analysis.cpp
    src/analyses/pz_checks.cpp
    src/analyses/sens_analysis.cpp
    src/analyses/solution_readout.cpp
    src/analyses/sp_analysis.cpp
    src/analyses/tf_analysis.cpp
    src/analyses/transient_analysis.cpp
    src/api/circuit.cpp
    src/api/circuit_builder.cpp
    src/api/nodalys.cpp
    src/bridge/logic_bridge.cpp
    src/devices/behavioral_device.cpp
    src/devices/bjt_device.cpp
    src/devices/devices.cpp
    src/devices/diode_device.cpp
    src/devices/fuse_device.cpp
    src/devices/integration.cpp
    src/devices/mosfet_device.cpp
    src/devices/random_draws.cpp
    src/devices/smooth_logic.cpp
    src/devices/thermal.cpp
    src/devices/transient_noise.cpp
    src/devices/waveforms.cpp
    src/export/csv_exporter.cpp
    src/export/exporters.cpp
    src/export/json_exporter.cpp
    src/export/raw_exporter.cpp
    src/export/raw_plot.cpp
    src/expression/expression.cpp
    src/expression/expression_math.cpp
    src/expression/expression_parser.cpp
    src/linear/dense_matrix.cpp
    src/linear/fft.cpp
    src/linear/pencil_reduction.cpp
    src/linear/qz.cpp
    src/linear/root_refinement.cpp
    src/mna/mna_system.cpp
    src/newton/newton.cpp
    src/parser/model_cards.cpp
    src/parser/netlist_parser.cpp
    src/parser/netlist_subcircuits.cpp
    src/parser/source_waveforms.cpp
    src/parser/spice_number.cpp
    src/parser/spice_text.cpp
    src/parser/spice_values.cpp
    src/parser/subcircuit_elements.cpp
    src/parser/subcircuit_models.cpp
    src/parser/subcircuit_scope.cpp
    src/parser/subcircuit_text.cpp
    src/text/text_case.cpp
)

target_include_directories(nodalys
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(COMMAND imcsim_target_coverage)
    imcsim_target_coverage(nodalys)
endif()

# =============================================================================
# nodalys-cli executable (Standalone SPICE CLI)
# =============================================================================

# The command line is a library apart from main(), so the tests run it in their own process
add_library(nodalys-cli-lib STATIC
    src/cli/cli.cpp
)

target_include_directories(nodalys-cli-lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nodalys-cli-lib
    PUBLIC
        nodalys
    PRIVATE
        CLI11::CLI11
)

add_executable(nodalys-cli
    src/cli/main.cpp
)

set_target_properties(nodalys-cli PROPERTIES
    OUTPUT_NAME "nodalys"
)

target_link_libraries(nodalys-cli
    PRIVATE
        nodalys-cli-lib
)

if(COMMAND imcsim_target_coverage)
    imcsim_target_coverage(nodalys-cli-lib)
    imcsim_target_coverage(nodalys-cli)
endif()

# =============================================================================
# Testing
# =============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
