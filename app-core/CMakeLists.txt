# =============================================================================
# Application Core
# =============================================================================

add_library(imcsim-core-lib
        STATIC
            components/capacitor.cpp
            components/capacitor.h
            components/component.cpp
            components/component.h
            components/resistor.cpp
            components/resistor.h
            error_codes.h
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
