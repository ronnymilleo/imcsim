# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
            components/capacitor.cpp
            components/component.cpp
            components/inductor.cpp
            components/resistor.cpp
)

target_include_directories(imcsim-core-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/components
)

target_link_libraries(imcsim-core-lib
        PUBLIC
            #
)
