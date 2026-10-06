# =============================================================================
# Application GUI
# =============================================================================

add_library(imcsim-gui-lib
        STATIC
            application.cpp
            editor.cpp
            helpers.cpp
            ui_elements/ui_capacitor.cpp
            ui_elements/ui_element.cpp
            ui_elements/ui_resistor.cpp
            vulkan_context.cpp
)

target_include_directories(imcsim-gui-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/ui_elements
)

target_link_libraries(imcsim-gui-lib
        PUBLIC
            imcsim-core-lib
            imgui
)
