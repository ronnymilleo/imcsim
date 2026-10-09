# =============================================================================
# Immediate Mode Circuit Simulator
# =============================================================================
#
# Author: Ronny Milleo
# License: See LICENSE file in project root
# =============================================================================

cmake_minimum_required(VERSION 4.3)

project(imcsim
    VERSION 0.6.0
)

add_compile_definitions(IMCSIM_VERSION="${PROJECT_VERSION}")

# =============================================================================
# Overall project configs
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# Windows binaries carry the C++ runtime, so they run without the Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/${BUILD_TYPE_LOWER}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/${BUILD_TYPE_LOWER}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/${BUILD_TYPE_LOWER}/lib")

# =============================================================================
# Formatting Target (clang-format)
# =============================================================================

find_program(CLANG_FORMAT NAMES clang-format)
if(CLANG_FORMAT)
    # Collect all source files for formatting
    file(GLOB_RECURSE ALL_SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/app-core/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app-core/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/app-gui/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app-gui/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    )

    add_custom_target(format
            COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES} main.cpp
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Formatting source code with clang-format"
            VERBATIM
    )
else()
    message(STATUS "clang-format not found; skipping automatic source formatting")
    add_custom_target(format
            COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found, skipping formatting"
    )
endif()

# =============================================================================
# Dependencies and targets
# =============================================================================

add_subdirectory(vendor)
add_subdirectory(app-core)
add_subdirectory(app-gui)

add_executable(imcsim main.cpp)
if(WIN32)
    # The icon of the executable, which SDL also uses for its windows; packaging/windows/make_icon.py draws it
    target_sources(imcsim PRIVATE packaging/windows/imcsim.rc)
    # The numeric version of the file properties, which imcsim.rc cannot split out of IMCSIM_VERSION
    target_compile_definitions(imcsim PRIVATE
            $<$<COMPILE_LANGUAGE:RC>:IMCSIM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}>
            $<$<COMPILE_LANGUAGE:RC>:IMCSIM_VERSION_MINOR=${PROJECT_VERSION_MINOR}>
            $<$<COMPILE_LANGUAGE:RC>:IMCSIM_VERSION_PATCH=${PROJECT_VERSION_PATCH}>)
    # Release builds open no console window behind the program, and keep main() as entry point; Debug builds keep
    # the console for the logs of SDL and the GPU validation layers
    if(MSVC)
        target_link_options(imcsim PRIVATE $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>)
    endif()
endif()
target_include_directories(imcsim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imcsim PRIVATE imcsim-gui-lib)
add_dependencies(imcsim format)

# =============================================================================
# Testing
# =============================================================================

option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# Post-Build Commands
# =============================================================================

add_custom_target(copy_compile_commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json to project root"
        VERBATIM
)

add_dependencies(imcsim copy_compile_commands)

# =============================================================================
# Project Summary
# =============================================================================
# Installation
# =============================================================================

# Windows gets a folder to unzip anywhere, with ngspice next to the program; Linux the standard layout, with the
# desktop entry and icon. Both carry the licenses of what the program includes
if(WIN32)
    set(IMCSIM_DOC_DIR .)
    install(TARGETS imcsim RUNTIME DESTINATION .)
    install(FILES vendor/ngspice-windows/bin/ngspice.dll DESTINATION .)
    install(FILES vendor/ngspice-windows/licenses/ngspice-COPYING.txt RENAME ngspice-LICENSE.txt
            DESTINATION licenses)
else()
    include(GNUInstallDirs)
    set(IMCSIM_DOC_DIR ${CMAKE_INSTALL_DOCDIR})
    install(TARGETS imcsim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/imcsim.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/imcsim.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md DESTINATION ${IMCSIM_DOC_DIR})
install(FILES vendor/imgui/LICENSE.txt RENAME imgui-LICENSE.txt DESTINATION ${IMCSIM_DOC_DIR}/licenses)
install(FILES vendor/implot/LICENSE RENAME implot-LICENSE.txt DESTINATION ${IMCSIM_DOC_DIR}/licenses)
install(FILES vendor/json/LICENSE.MIT RENAME json-LICENSE.txt DESTINATION ${IMCSIM_DOC_DIR}/licenses)
install(FILES vendor/SDL/LICENSE.txt RENAME SDL-LICENSE.txt DESTINATION ${IMCSIM_DOC_DIR}/licenses)
install(FILES assets/fonts/Inter-LICENSE.txt assets/fonts/JetBrainsMono-LICENSE.txt
        DESTINATION ${IMCSIM_DOC_DIR}/licenses)

# =============================================================================

message(STATUS "")
message(STATUS "==================== Summary ====================")
message(STATUS "Project:      ${PROJECT_NAME}")
message(STATUS "Version:      ${PROJECT_VERSION}")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "=================================================")
message(STATUS "")
