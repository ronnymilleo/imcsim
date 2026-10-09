#!/usr/bin/env bash
# Builds imcsim as an AppImage inside an Ubuntu 24.04 container, the oldest base it supports, so the image runs on
# distributions with glibc 2.39 or newer. Run it from the repository root, with the submodules checked out:
#
#   docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 packaging/appimage/build.sh
#
# The release workflow runs the same script. The AppImage lands in out/appimage/.
set -euo pipefail

CMAKE_VERSION=4.3.5
SDL_VERSION=3.4.16
NGSPICE_VERSION=47
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage

SOURCE_DIR=$(pwd)
WORK_DIR=/tmp/imcsim-appimage
PREFIX=/opt/deps
mkdir -p "$WORK_DIR" "$PREFIX"

echo "== System packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates wget file git ninja-build pkg-config g++-14 gcc-14 make \
    autoconf automake libtool bison flex \
    libvulkan-dev libgomp1 \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libdbus-1-dev libudev-dev \
    libgtk-3-0
# GCC 14, the newest Ubuntu 24.04 offers; the ngspice configure looks for gcc by name, so the plain names point to it
ln -sf /usr/bin/gcc-14 /usr/local/bin/gcc
ln -sf /usr/bin/g++-14 /usr/local/bin/g++
ln -sf /usr/bin/gcc-14 /usr/local/bin/cc
ln -sf /usr/bin/g++-14 /usr/local/bin/c++
export CC=gcc CXX=g++

echo "== CMake $CMAKE_VERSION (Ubuntu 24.04 ships 3.28)"
wget -q "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz" \
    -O "$WORK_DIR/cmake.tar.gz"
tar -xzf "$WORK_DIR/cmake.tar.gz" -C "$WORK_DIR"
export PATH="$WORK_DIR/cmake-$CMAKE_VERSION-linux-x86_64/bin:$PATH"

echo "== SDL $SDL_VERSION (Ubuntu 24.04 has no SDL3)"
wget -q "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz" \
    -O "$WORK_DIR/sdl.tar.gz"
tar -xzf "$WORK_DIR/sdl.tar.gz" -C "$WORK_DIR"
cmake -S "$WORK_DIR/SDL3-$SDL_VERSION" -B "$WORK_DIR/sdl-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build "$WORK_DIR/sdl-build"
cmake --install "$WORK_DIR/sdl-build"

echo "== ngspice $NGSPICE_VERSION as a shared library (Ubuntu 24.04 ships 42; the project is tested with 47)"
wget -q "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/$NGSPICE_VERSION/ngspice-$NGSPICE_VERSION.tar.gz" \
    -O "$WORK_DIR/ngspice.tar.gz"
tar -xzf "$WORK_DIR/ngspice.tar.gz" -C "$WORK_DIR"
(
    cd "$WORK_DIR/ngspice-$NGSPICE_VERSION"
    ./configure --prefix="$PREFIX" --with-ngshared --enable-openmp --disable-debug --without-x
    make -j"$(nproc)"
    make install
)

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib/x86_64-linux-gnu/pkgconfig"
export LD_LIBRARY_PATH="$PREFIX/lib:$PREFIX/lib/x86_64-linux-gnu"

echo "== imcsim"
# The C++ runtime is linked statically, so the image does not depend on the libstdc++ of the host
cmake -S "$SOURCE_DIR" -B "$WORK_DIR/imcsim-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build "$WORK_DIR/imcsim-build"
rm -rf "$WORK_DIR/AppDir"
DESTDIR="$WORK_DIR/AppDir" cmake --install "$WORK_DIR/imcsim-build"
mkdir -p "$WORK_DIR/AppDir/usr/share/doc/ngspice" "$WORK_DIR/AppDir/usr/share/doc/sdl3"
cp "$WORK_DIR/ngspice-$NGSPICE_VERSION/COPYING" "$WORK_DIR/AppDir/usr/share/doc/ngspice/"
cp "$WORK_DIR/SDL3-$SDL_VERSION/LICENSE.txt" "$WORK_DIR/AppDir/usr/share/doc/sdl3/"

echo "== AppImage"
wget -q "$LINUXDEPLOY_URL" -O "$WORK_DIR/linuxdeploy"
chmod +x "$WORK_DIR/linuxdeploy"
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)
mkdir -p "$SOURCE_DIR/out/appimage"
cd "$SOURCE_DIR/out/appimage"
# Containers have no FUSE, so linuxdeploy runs extracted. libngspice is opened at run time by the program's own
# link, so it is found through the executable; the Vulkan loader stays on the host, next to its drivers
APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION="$VERSION" "$WORK_DIR/linuxdeploy" \
    --appdir "$WORK_DIR/AppDir" \
    --executable "$WORK_DIR/AppDir/usr/bin/imcsim" \
    --desktop-file "$WORK_DIR/AppDir/usr/share/applications/imcsim.desktop" \
    --icon-file "$WORK_DIR/AppDir/usr/share/icons/hicolor/scalable/apps/imcsim.svg" \
    --exclude-library 'libvulkan.so*' \
    --output appimage
ls -l "$SOURCE_DIR/out/appimage"
