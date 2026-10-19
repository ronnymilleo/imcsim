# =============================================================================
# Immediate Mode Circuit Simulator
# =============================================================================
#
# Author: Ronny Milleo
# License: See LICENSE file in project root
# =============================================================================

cmake_minimum_required(VERSION 4.3)

# The parsing of numbers uses std::from_chars on doubles, which the C++ library of macOS has from version 26. Only
# read on macOS, and set before project() so the compiler checks are made for it
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "Oldest macOS the program runs on")

project(imcsim
    VERSION 0.7.0
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

# Coverage instrumentation, applied per target through imcsim_target_coverage() so that nothing under vendor (SDL,
# imgui, implot, Catch2...) is measured
option(ENABLE_COVERAGE "Instrument the build for code coverage (GCC or Clang)" OFF)
if(ENABLE_COVERAGE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(WARNING "ENABLE_COVERAGE needs GCC or Clang; ${CMAKE_CXX_COMPILER_ID} builds without coverage")
endif()

function(imcsim_target_coverage target)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        target_compile_options(${target} PRIVATE --coverage)
        # PUBLIC carries the runtime requirement to consumers of static libraries
        target_link_options(${target} PUBLIC --coverage)
    endif()
endfunction()

add_subdirectory(vendor)
add_subdirectory(app-core)
add_subdirectory(app-gui)

add_executable(imcsim main.cpp)
if(WIN32)
    # The icon of the executable, which SDL also uses for its windows; packaging/windows/make_icon.py draws it
    target_sources(imcsim PRIVATE packaging/windows/imcsim.rc)
    # Merged by the linker into the manifest it embeds
    target_sources(imcsim PRIVATE packaging/windows/imcsim.manifest)
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
elseif(APPLE)
    # An application bundle, imcsim.app, with the icon packaging/macos/make_icns.py draws
    set_source_files_properties(packaging/macos/imcsim.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(imcsim PRIVATE packaging/macos/imcsim.icns)
    set_target_properties(imcsim PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER io.github.ronnymilleo.imcsim
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
endif()
target_include_directories(imcsim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imcsim PRIVATE imcsim-gui-lib)
imcsim_target_coverage(imcsim)
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

# Windows gets a folder to unzip anywhere, with ngspice next to the program; macOS the application bundle, with
# ngspice in its Frameworks folder; Linux the standard layout, with the desktop entry and icon. All carry the licenses
# of what the program includes
if(WIN32)
    set(IMCSIM_DOC_DIR .)
    install(TARGETS imcsim RUNTIME DESTINATION .)
    install(FILES vendor/ngspice-windows/bin/ngspice.dll DESTINATION .)
    install(FILES vendor/ngspice-windows/licenses/ngspice-COPYING.txt RENAME ngspice-LICENSE.txt
            DESTINATION licenses)
elseif(APPLE)
    set(IMCSIM_DOC_DIR imcsim.app/Contents/Resources)
    install(TARGETS imcsim BUNDLE DESTINATION .)
    # The license comes with the build of ngspice: Homebrew keeps it in the prefix, packaging/macos/build.sh in the
    # documentation folder
    find_file(NGSPICE_COPYING COPYING PATHS ${NGSPICE_PREFIX} ${NGSPICE_PREFIX}/share/doc/ngspice NO_DEFAULT_PATH
            REQUIRED)
    install(FILES ${NGSPICE_COPYING} RENAME ngspice-LICENSE.txt DESTINATION ${IMCSIM_DOC_DIR}/licenses)
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
# Last, since the signature seals every file of the bundle
if(APPLE)
    install(CODE "
        set(IMCSIM_BUNDLE \"\${CMAKE_INSTALL_PREFIX}/imcsim.app\")
        set(NGSPICE_LIBRARY_FILE \"${NGSPICE_LIBRARY_FILE}\")
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/bundle_ngspice.cmake\")
    ")
endif()

# =============================================================================

message(STATUS "")
message(STATUS "==================== Summary ====================")
message(STATUS "Project:      ${PROJECT_NAME}")
message(STATUS "Version:      ${PROJECT_VERSION}")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Coverage:     ${ENABLE_COVERAGE}")
message(STATUS "=================================================")
message(STATUS "")
