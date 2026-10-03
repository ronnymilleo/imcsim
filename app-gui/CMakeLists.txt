# =============================================================================
# Application GUI
# =============================================================================

add_library(imcsim-gui-lib
        STATIC
        application.cpp
)

target_include_directories(imcsim-gui-lib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(imcsim-gui-lib
        PUBLIC
        imcsim-core-lib
        imgui
)

# =============================================================================
# End of file
# =============================================================================
