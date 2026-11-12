# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
            backends/ngspice/ngspice_backend.cpp
            backends/ngspice/ngspice_library.cpp
            backends/ngspice/ngspice_readers.cpp
            backends/nodalys/compiler.cpp
            backends/nodalys/model_parser.cpp
            backends/nodalys/nodalys_backend.cpp
            backends/nodalys/parameter_conversion.cpp
            circuit/behavioral_expression.cpp
            circuit/circuit.cpp
            circuit/components/behavioral_source.cpp
            circuit/components/bjt.cpp
            circuit/components/capacitor.cpp
            circuit/components/clock.cpp
            circuit/components/component.cpp
            circuit/components/controlled_source.cpp
            circuit/components/current_source.cpp
            circuit/components/diode.cpp
            circuit/components/fuse.cpp
            circuit/components/ground.cpp
            circuit/components/inductor.cpp
            circuit/components/logic_input.cpp
            circuit/components/logic_output.cpp
            circuit/components/logic_part.cpp
            circuit/components/mosfet.cpp
            circuit/components/net_label.cpp
            circuit/components/op_amp.cpp
            circuit/components/potentiometer.cpp
            circuit/components/relay.cpp
            circuit/components/resistor.cpp
            circuit/components/seven_segment.cpp
            circuit/components/source.cpp
            circuit/components/subcircuit.cpp
            circuit/components/subcircuit_pin.cpp
            circuit/components/switch.cpp
            circuit/components/transformer.cpp
            circuit/components/vcc.cpp
            circuit/components/voltage_controlled_switch.cpp
            circuit/components/voltage_source.cpp
            circuit/library_model.cpp
            circuit/spice_library.cpp
            simulation/analysis_suggestions.cpp
            simulation/circuit_checks.cpp
            simulation/circuit_copy.cpp
            simulation/fuse_state.cpp
            simulation/live_change.cpp
            simulation/live_pace.cpp
            simulation/monte_carlo.cpp
            simulation/pole_zero.cpp
            simulation/ratings.cpp
            simulation/run_control.cpp
            simulation/s_parameters.cpp
            simulation/sensitivity.cpp
            simulation/simulation_backend.cpp
            simulation/simulation_results.cpp
            simulation/simulation_service.cpp
            simulation/simulator.cpp
            simulation/stepped_circuit.cpp
            spice/spice_inclusions.cpp
            spice/spice_text.cpp
            spice/spice_value.cpp
            spice/standard_values.cpp
)

target_include_directories(imcsim-core-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
)

# ngspice runs as a shared library: pkg-config finds it on Linux and macOS, and on Windows the build of
# vendor/ngspice-windows (see the README there) is linked, and its DLL copied next to the binaries on every build
if(WIN32)
    set(NGSPICE_WINDOWS_DIR "${PROJECT_SOURCE_DIR}/vendor/ngspice-windows")
    add_library(ngspice UNKNOWN IMPORTED)
    set_target_properties(ngspice PROPERTIES
            IMPORTED_LOCATION "${NGSPICE_WINDOWS_DIR}/lib/ngspice.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${NGSPICE_WINDOWS_DIR}/include"
    )
    set(NGSPICE_LINK_TARGET ngspice)
    # ngspice opens the digital code models by a name relative to the program, so they sit next to it as well
    add_custom_target(ngspice-runtime
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NGSPICE_WINDOWS_DIR}/bin/ngspice.dll"
                    "${NGSPICE_WINDOWS_DIR}/bin/digital.cm" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            VERBATIM
    )
    add_dependencies(imcsim-core-lib ngspice-runtime)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NGSPICE REQUIRED IMPORTED_TARGET ngspice)
    set(NGSPICE_LINK_TARGET PkgConfig::NGSPICE)
    # The macOS bundle carries the library, so the install rules of the project need its file and prefix
    if(APPLE)
        set(ngspice_library ${NGSPICE_LINK_LIBRARIES})
        list(FILTER ngspice_library INCLUDE REGEX "ngspice")
        set(NGSPICE_LIBRARY_FILE ${ngspice_library} PARENT_SCOPE)
        set(NGSPICE_PREFIX ${NGSPICE_PREFIX} PARENT_SCOPE)
    endif()
endif()

# The simulation service runs jobs on a worker thread
find_package(Threads REQUIRED)

target_link_libraries(imcsim-core-lib
        PUBLIC
            Threads::Threads
            nodalys
        PRIVATE
            ${NGSPICE_LINK_TARGET}
)

imcsim_target_coverage(imcsim-core-lib)
