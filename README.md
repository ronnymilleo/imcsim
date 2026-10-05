# imcsim

Immediate Mode Circuit Simulator, built with Dear ImGui, SDL3 and Vulkan.

## Requirements

- Linux with a GPU and driver that support Vulkan
- CMake 4.3 or newer
- A C++23 compiler (tested with GCC 16)
- Ninja (or another CMake generator)
- SDL3 development files
- Vulkan headers and loader (the Vulkan SDK is not required)
- Git (the `vendor/imgui` submodule tracks the `docking` branch)

On Arch Linux:

```
sudo pacman -S cmake ninja gcc sdl3 vulkan-headers vulkan-icd-loader
```

Also install the Vulkan driver for your GPU (for example `vulkan-radeon`).

## Build and run

```
git clone --recurse-submodules <repository-url>
cd imcsim
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./out/debug/bin/imcsim
```

If you already cloned without submodules, run `git submodule update --init`.

The binary is written to `out/<build-type>/bin/`. Add `-DBUILD_TESTS=ON` to build the tests.

## Optional

- `clang-format`: if installed, the build formats the sources automatically.
- `vulkan-validation-layers`: only needed if you enable Vulkan validation.
