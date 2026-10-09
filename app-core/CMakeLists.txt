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

# ngspice runs as a shared library: pkg-config finds it on Linux, and on Windows its official MSVC build lives in
# vendor/ngspice-windows (see the README there). Its DLLs are copied next to the binaries on every build, together
# with the OpenMP runtime that ngspice.dll loads, taken from the Visual Studio that compiles the project because
# Microsoft does not allow redistributing it
if(WIN32)
    set(NGSPICE_WINDOWS_DIR "${PROJECT_SOURCE_DIR}/vendor/ngspice-windows")
    add_library(ngspice UNKNOWN IMPORTED)
    set_target_properties(ngspice PROPERTIES
            IMPORTED_LOCATION "${NGSPICE_WINDOWS_DIR}/lib/ngspice.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${NGSPICE_WINDOWS_DIR}/include"
    )
    set(NGSPICE_LINK_TARGET ngspice)

    string(REGEX REPLACE "/Tools/MSVC/.*$" "" MSVC_VC_DIR "${CMAKE_CXX_COMPILER}")
    file(GLOB OPENMP_RUNTIMES
            "${MSVC_VC_DIR}/Redist/MSVC/*/debug_nonredist/x64/Microsoft.VC*.OpenMP.LLVM/libomp140.x86_64.dll")
    if(NOT OPENMP_RUNTIMES)
        message(FATAL_ERROR "libomp140.x86_64.dll not found under ${MSVC_VC_DIR}/Redist: build with MSVC from Visual Studio")
    endif()
    list(SORT OPENMP_RUNTIMES COMPARE NATURAL ORDER DESCENDING)
    list(GET OPENMP_RUNTIMES 0 OPENMP_RUNTIME)
    file(GLOB NGSPICE_RUNTIME_DLLS "${NGSPICE_WINDOWS_DIR}/bin/*.dll")
    add_custom_target(ngspice-runtime
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NGSPICE_RUNTIME_DLLS} "${OPENMP_RUNTIME}"
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
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
