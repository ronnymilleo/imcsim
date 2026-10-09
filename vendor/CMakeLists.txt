# =============================================================================
# SDL
# =============================================================================

# Built from the submodule, not taken from the system, and linked statically: every platform carries the same
# version inside the executable, with no library to ship next to it. Its GPU API renders Dear ImGui (Direct3D 12 on
# Windows, Vulkan on Linux), and its tests, examples and test library stay out
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
add_subdirectory(SDL)

# =============================================================================
# imgui
# =============================================================================

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

add_library(imgui
        STATIC
            ${IMGUI_DIR}/imgui.cpp
            ${IMGUI_DIR}/imgui_draw.cpp
            ${IMGUI_DIR}/imgui_tables.cpp
            ${IMGUI_DIR}/imgui_widgets.cpp
            ${IMGUI_DIR}/imgui_demo.cpp
            ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
            ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
            ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui
        PUBLIC
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
)

target_link_libraries(imgui
        PUBLIC
            SDL3::SDL3
)

target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# =============================================================================
# implot
# =============================================================================

# The demo is left out: it is not used and warns under C++23
set(IMPLOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/implot)

add_library(implot
        STATIC
            ${IMPLOT_DIR}/implot.cpp
            ${IMPLOT_DIR}/implot_items.cpp
)

target_include_directories(implot
        PUBLIC
            ${IMPLOT_DIR}
)

target_link_libraries(implot
        PUBLIC
            imgui
)

# =============================================================================
# nlohmann/json
# =============================================================================

# Header-only; its own tests and install rules stay off because it is not the top-level project
add_subdirectory(json)
