# imcsim

Immediate Mode Circuit Simulator, built with Dear ImGui, SDL3 and Vulkan. Circuits are simulated with
[ngspice](https://ngspice.sourceforge.io/).

## Requirements

- Linux with a GPU and driver that support Vulkan
- CMake 4.3 or newer
- A C++23 compiler (tested with GCC 16)
- Ninja (or another CMake generator)
- SDL3 development files
- Vulkan headers and loader (the Vulkan SDK is not required)
- ngspice built as a shared library (`libngspice` and its `sharedspice.h` header; tested with ngspice 47)
- pkg-config, which CMake uses to find ngspice
- Git, for the submodules in `vendor/`: Dear ImGui (`docking` branch), nlohmann/json and Catch2

On Arch Linux:

```
sudo pacman -S cmake ninja gcc pkgconf sdl3 vulkan-headers vulkan-icd-loader ngspice
```

The Arch `ngspice` package already includes the shared library. Also install the Vulkan driver for your GPU
(for example `vulkan-radeon`).

On Ubuntu 25.04 or newer (older releases do not package SDL3):

```
sudo apt install ninja-build g++ pkgconf libsdl3-dev libvulkan-dev mesa-vulkan-drivers libngspice0-dev
sudo snap install cmake --classic
```

Ubuntu's own `cmake` package is older than 4.3, hence the snap. The default `g++` must support C++23; if the build
fails on missing standard library features, install a newer one (for example `g++-15`) and configure with
`-DCMAKE_CXX_COMPILER=g++-15`. These Ubuntu instructions are not tested regularly; please report what differs.

Other distributions usually split ngspice into a development package, such as `libngspice-devel` on Fedora.
If CMake reports `No package 'ngspice' found`, the shared library or its `ngspice.pc` file is missing.

## Build and run

```
git clone --recurse-submodules <repository-url>
cd imcsim
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./out/debug/bin/imcsim
```

If you already cloned without submodules, run `git submodule update --init`.

The binary is written to `out/<build-type>/bin/`.

## Tests

The tests use Catch2 and run some circuits through ngspice, so they need the same libraries as the application:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Optional

- `clang-format`: if installed, the build formats the sources automatically.
- `vulkan-validation-layers`: only needed if you enable Vulkan validation.

## License

imcsim is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version
(`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the full text.

The libraries in `vendor/` and ngspice keep their own licenses.
