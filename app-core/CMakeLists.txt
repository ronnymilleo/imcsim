# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
            analysis_suggestions.cpp
            circuit.cpp
            components/bjt.cpp
            components/capacitor.cpp
            components/clock.cpp
            components/component.cpp
            components/controlled_source.cpp
            components/current_source.cpp
            components/diode.cpp
            components/ground.cpp
            components/inductor.cpp
            components/logic_input.cpp
            components/logic_part.cpp
            components/mosfet.cpp
            components/net_label.cpp
            components/op_amp.cpp
            components/potentiometer.cpp
            components/relay.cpp
            components/resistor.cpp
            components/source.cpp
            components/switch.cpp
            components/transformer.cpp
            components/vcc.cpp
            components/voltage_controlled_switch.cpp
            components/voltage_source.cpp
            simulator.cpp
            spice_value.cpp
            standard_values.cpp
)

target_include_directories(imcsim-core-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/components
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

target_link_libraries(imcsim-core-lib
        PRIVATE
            ${NGSPICE_LINK_TARGET}
)

imcsim_target_coverage(imcsim-core-lib)
