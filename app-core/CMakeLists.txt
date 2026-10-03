# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
        lib_core.cpp
)

target_include_directories(imcsim-core-lib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(imcsim-core-lib
        PUBLIC
        #
)

# =============================================================================
# End of file
# =============================================================================
