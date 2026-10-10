# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
            analysis_suggestions.cpp
            circuit.cpp
            components/bjt.cpp
            components/capacitor.cpp
            components/component.cpp
            components/controlled_source.cpp
            components/current_source.cpp
            components/diode.cpp
            components/ground.cpp
            components/inductor.cpp
            components/mosfet.cpp
            components/op_amp.cpp
            components/resistor.cpp
            components/source.cpp
            components/vcc.cpp
            components/voltage_source.cpp
            simulator.cpp
            spice_value.cpp
)

target_include_directories(imcsim-core-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/components
)

# ngspice runs as a shared library: pkg-config finds it on Linux, and on Windows the build of vendor/ngspice-windows
# (see the README there) is linked, and its DLL copied next to the binaries on every build
if(WIN32)
    set(NGSPICE_WINDOWS_DIR "${PROJECT_SOURCE_DIR}/vendor/ngspice-windows")
    add_library(ngspice UNKNOWN IMPORTED)
    set_target_properties(ngspice PROPERTIES
            IMPORTED_LOCATION "${NGSPICE_WINDOWS_DIR}/lib/ngspice.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${NGSPICE_WINDOWS_DIR}/include"
    )
    set(NGSPICE_LINK_TARGET ngspice)
    add_custom_target(ngspice-runtime
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NGSPICE_WINDOWS_DIR}/bin/ngspice.dll"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ngspice.dll"
            VERBATIM
    )
    add_dependencies(imcsim-core-lib ngspice-runtime)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(NGSPICE REQUIRED IMPORTED_TARGET ngspice)
    set(NGSPICE_LINK_TARGET PkgConfig::NGSPICE)
endif()

target_link_libraries(imcsim-core-lib
        PRIVATE
            ${NGSPICE_LINK_TARGET}
)
