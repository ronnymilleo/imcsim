# =============================================================================
# Nodalys simulation engine (autonomous clean-room solver, zero dependencies)
# =============================================================================

add_library(nodalys STATIC
    src/linear/dense_matrix.cpp
    src/mna/mna_system.cpp
    src/devices/devices.cpp
    src/devices/nonlinear.cpp
    src/devices/waveforms.cpp
    src/newton/newton.cpp
    src/analyses/dc_analysis.cpp
    src/analyses/transient_analysis.cpp
    src/analyses/ac_analysis.cpp
    src/analyses/tf_analysis.cpp
    src/analyses/noise_analysis.cpp
    src/api/circuit.cpp
    src/api/circuit_builder.cpp
    src/api/nodalys.cpp
    src/parser/spice_number.cpp
    src/parser/netlist_parser.cpp
    src/export/raw_exporter.cpp
    src/export/csv_exporter.cpp
    src/export/json_exporter.cpp
    src/export/exporters.cpp
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

add_executable(nodalys-cli
    src/cli/main.cpp
)

set_target_properties(nodalys-cli PROPERTIES
    OUTPUT_NAME "nodalys"
)

target_link_libraries(nodalys-cli
    PRIVATE
        nodalys
        CLI11::CLI11
)

if(COMMAND imcsim_target_coverage)
    imcsim_target_coverage(nodalys-cli)
endif()
