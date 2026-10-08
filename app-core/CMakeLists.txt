# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
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

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGSPICE REQUIRED IMPORTED_TARGET ngspice)

target_link_libraries(imcsim-core-lib
        PRIVATE
            PkgConfig::NGSPICE
)
