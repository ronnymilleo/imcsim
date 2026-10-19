# Run by cmake --install on macOS, once the bundle is in place: copies libngspice into imcsim.app/Contents/Frameworks
# and its digital code models into Contents/PlugIns, points the program at the copy instead of the build machine's, and
# signs them again, since changing a load path breaks the signature the linker made. The signature is ad hoc, which
# runs on any Mac but is not notarized.
#
# Expects IMCSIM_BUNDLE (the installed imcsim.app) and NGSPICE_LIBRARY_FILE (the library the program was linked to).

set(bundle "$ENV{DESTDIR}${IMCSIM_BUNDLE}")
set(program "${bundle}/Contents/MacOS/imcsim")

# The program loads the library by its install name, the path it had when the program was linked
file(REAL_PATH "${NGSPICE_LIBRARY_FILE}" library)
execute_process(COMMAND otool -D "${library}" OUTPUT_VARIABLE otool_output COMMAND_ERROR_IS_FATAL ANY)
string(REGEX REPLACE "^[^\n]*\n([^\n]*)\n?$" "\\1" install_name "${otool_output}")
get_filename_component(library_name "${install_name}" NAME)

message(STATUS "Bundling: ${library} as Contents/Frameworks/${library_name}")
file(MAKE_DIRECTORY "${bundle}/Contents/Frameworks")
# Homebrew installs libraries read-only, and install_name_tool must write the copy
file(COPY_FILE "${library}" "${bundle}/Contents/Frameworks/${library_name}")
file(CHMOD "${bundle}/Contents/Frameworks/${library_name}"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

execute_process(COMMAND install_name_tool -id "@rpath/${library_name}" "${bundle}/Contents/Frameworks/${library_name}"
        COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND install_name_tool -change "${install_name}" "@rpath/${library_name}" "${program}"
        COMMAND_ERROR_IS_FATAL ANY)
# A second install over the same bundle finds the run path already there
execute_process(COMMAND otool -l "${program}" OUTPUT_VARIABLE load_commands COMMAND_ERROR_IS_FATAL ANY)
if(NOT load_commands MATCHES "path @executable_path/../Frameworks ")
    execute_process(COMMAND install_name_tool -add_rpath "@executable_path/../Frameworks" "${program}"
            COMMAND_ERROR_IS_FATAL ANY)
endif()

# The digital code models, which the program loads from PlugIns when ngspice has not loaded them through its spinit; a
# build of ngspice without XSPICE has none, and the logic parts then stop with a message
get_filename_component(library_directory "${library}" DIRECTORY)
set(code_models "${library_directory}/ngspice/digital.cm")
if(EXISTS "${code_models}")
    message(STATUS "Bundling: ${code_models} as Contents/PlugIns/digital.cm")
    file(MAKE_DIRECTORY "${bundle}/Contents/PlugIns")
    file(COPY_FILE "${code_models}" "${bundle}/Contents/PlugIns/digital.cm")
    file(CHMOD "${bundle}/Contents/PlugIns/digital.cm" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    execute_process(COMMAND codesign --force --sign - "${bundle}/Contents/PlugIns/digital.cm"
            COMMAND_ERROR_IS_FATAL ANY)
else()
    message(WARNING "No digital code models at ${code_models}; the logic parts will not run")
endif()

execute_process(COMMAND codesign --force --sign - "${bundle}/Contents/Frameworks/${library_name}"
        COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND codesign --force --sign - "${bundle}" COMMAND_ERROR_IS_FATAL ANY)
