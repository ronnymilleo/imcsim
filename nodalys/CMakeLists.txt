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
