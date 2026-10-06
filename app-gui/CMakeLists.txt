# =============================================================================
# Application GUI
# =============================================================================

add_library(imcsim-gui-lib
        STATIC
            application.cpp
            application.h
            editor.cpp
            editor.h
            ui_element.cpp
            ui_element.h
            vulkan_context.cpp
            vulkan_context.h
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
