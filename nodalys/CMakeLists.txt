# =============================================================================
# Nodalys simulation engine (autonomous clean-room solver, zero dependencies)
# =============================================================================

add_library(nodalys STATIC
    src/analyses/ac/ac_analysis.cpp
    src/analyses/ac_sens/ac_sens_analysis.cpp
    src/analyses/ac_sens/ac_sens_bias.cpp
    src/analyses/ac_sens/ac_sens_elements.cpp
    src/analyses/common/analysis_preamble.cpp
    src/analyses/common/frequency_sweep.cpp
    src/analyses/common/solution_readout.cpp
    src/analyses/dc/dc_analysis.cpp
    src/analyses/hb/hb_analysis.cpp
    src/analyses/hb/hb_bias.cpp
    src/analyses/hb/hb_residual.cpp
    src/analyses/hb/hb_result.cpp
    src/analyses/hb/hb_setup.cpp
    src/analyses/hb/hb_solve.cpp
    src/analyses/hb/hb_spectrum.cpp
    src/analyses/hb/hb_workspace.cpp
    src/analyses/noise/noise_analysis.cpp
    src/analyses/pz/pz_analysis.cpp
    src/analyses/pz/pz_checks.cpp
    src/analyses/sens/sens_analysis.cpp
    src/analyses/sp/port_matrices.cpp
    src/analyses/sp/sp_analysis.cpp
    src/analyses/tf/tf_analysis.cpp
    src/analyses/transient/transient_analysis.cpp
    src/analyses/transient/transient_collector.cpp
    src/api/circuit.cpp
    src/api/circuit_builder.cpp
    src/api/circuit_builder_macromodels.cpp
    src/api/circuit_builder_semiconductors.cpp
    src/api/circuit_builder_sources.cpp
    src/api/nodalys.cpp
    src/bridge/logic_bridge.cpp
    src/compiler/behavioral_binding.cpp
    src/compiler/circuit_compiler.cpp
    src/compiler/circuit_lowering.cpp
    src/compiler/compiled_circuit.cpp
    src/compiler/macromodels/electromechanical_macromodels.cpp
    src/compiler/macromodels/igbt_macromodel.cpp
    src/compiler/macromodels/logic_macromodels.cpp
    src/compiler/macromodels/opamp_macromodels.cpp
    src/compiler/source_specs.cpp
    src/devices/integration.cpp
    src/devices/passive/fuse_device.cpp
    src/devices/passive/passive_devices.cpp
    src/devices/passive/smooth_logic.cpp
    src/devices/passive/switch_device.cpp
    src/devices/passive/transmission_line_device.cpp
    src/devices/semiconductors/bjt_device.cpp
    src/devices/semiconductors/diode_device.cpp
    src/devices/semiconductors/igbt_device.cpp
    src/devices/semiconductors/mosfet_device.cpp
    src/devices/semiconductors/thyristor_device.cpp
    src/devices/sources/behavioral_device.cpp
    src/devices/sources/controlled_sources.cpp
    src/devices/sources/independent_sources.cpp
    src/devices/sources/random_draws.cpp
    src/devices/sources/transient_noise.cpp
    src/devices/sources/waveforms.cpp
    src/devices/thermal.cpp
    src/export/csv_exporter.cpp
    src/export/exporters.cpp
    src/export/json_exporter.cpp
    src/export/raw_exporter.cpp
    src/export/raw_plot.cpp
    src/expression/expression.cpp
    src/expression/expression_math.cpp
    src/expression/expression_parser.cpp
    src/expression/expression_vocabulary.cpp
    src/linear/dense_matrix.cpp
    src/linear/eigen/pencil_reduction.cpp
    src/linear/eigen/qz.cpp
    src/linear/eigen/qz_extract.cpp
    src/linear/eigen/qz_householder.cpp
    src/linear/eigen/root_refinement.cpp
    src/linear/fft.cpp
    src/mna/mna_solve.cpp
    src/mna/mna_system.cpp
    src/newton/newton.cpp
    src/parser/netlist/netlist_deck.cpp
    src/parser/netlist/netlist_directive_settings.cpp
    src/parser/netlist/netlist_directives.cpp
    src/parser/netlist/netlist_element_devices.cpp
    src/parser/netlist/netlist_element_sources.cpp
    src/parser/netlist/netlist_elements.cpp
    src/parser/netlist/netlist_nodes.cpp
    src/parser/netlist/netlist_subcircuits.cpp
    src/parser/netlist_parser.cpp
    src/parser/sources/source_line.cpp
    src/parser/sources/source_waveforms.cpp
    src/parser/subcircuit/subcircuit_element_sources.cpp
    src/parser/subcircuit/subcircuit_elements.cpp
    src/parser/subcircuit/subcircuit_expansion.cpp
    src/parser/subcircuit/subcircuit_lines.cpp
    src/parser/subcircuit/subcircuit_models.cpp
    src/parser/subcircuit/subcircuit_scope.cpp
    src/parser/subcircuit/subcircuit_text.cpp
    src/parser/syntax/classified_lines.cpp
    src/parser/syntax/element_arguments.cpp
    src/parser/syntax/expression_references.cpp
    src/parser/syntax/model_cards.cpp
    src/parser/syntax/spice_text.cpp
    src/parser/syntax/spice_values.cpp
    src/text/spice_number.cpp
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
    src/cli/cli_runner.cpp
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
