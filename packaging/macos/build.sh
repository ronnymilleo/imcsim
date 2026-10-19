#!/usr/bin/env bash
# Builds imcsim.app and a disk image of it for the architecture of this Mac. Run it from the repository root, with
# the submodules checked out:
#
#   packaging/macos/build.sh
#
# It needs the Xcode Command Line Tools, CMake 4.3 or newer, Ninja and pkg-config (brew install cmake ninja pkgconf).
# ngspice is built here from its sources for the oldest macOS the program supports, rather than taken from Homebrew,
# whose libraries only run on the macOS version they were built for. The disk image lands in out/macos/.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=deps.env
source "$SCRIPT_DIR/deps.env"

SOURCE_DIR=$(pwd)
OUT_DIR="$SOURCE_DIR/out/macos"
WORK_DIR="${TMPDIR:-/tmp}/imcsim-macos"
ARCH=$(uname -m)
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)
# The deployment target set in CMakeLists.txt, so ngspice runs everywhere the program does
MACOSX_DEPLOYMENT_TARGET=$(sed -n 's/^set(CMAKE_OSX_DEPLOYMENT_TARGET \([0-9.]*\) .*/\1/p' "$SOURCE_DIR/CMakeLists.txt")
export MACOSX_DEPLOYMENT_TARGET
# A folder already holding ngspice is reused as long as it was built from the same version, for the same target
PREFIX="${IMCSIM_MACOS_DEPS:-$OUT_DIR/deps}"
DEPS_STAMP="$PREFIX/.built-ngspice-$NGSPICE_VERSION-macos-$MACOSX_DEPLOYMENT_TARGET-$ARCH"
mkdir -p "$WORK_DIR" "$PREFIX"

build_ngspice() {
    rm -rf "${PREFIX:?}"/*
    echo "== ngspice $NGSPICE_VERSION as a shared library, for macOS $MACOSX_DEPLOYMENT_TARGET"
    curl -fsSL "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/$NGSPICE_VERSION/ngspice-$NGSPICE_VERSION.tar.gz" \
        -o "$WORK_DIR/ngspice.tar.gz"
    echo "$NGSPICE_SHA256  $WORK_DIR/ngspice.tar.gz" | shasum -a 256 -c -
    rm -rf "$WORK_DIR/ngspice-$NGSPICE_VERSION"
    tar -xzf "$WORK_DIR/ngspice.tar.gz" -C "$WORK_DIR"
    (
        cd "$WORK_DIR/ngspice-$NGSPICE_VERSION"
        # Apple's compiler has no OpenMP, and FFTW is left out so the library needs nothing from Homebrew
        ./configure --prefix="$PREFIX" --with-ngshared --disable-debug --disable-openmp --without-x \
            --with-fftw3=no --with-readline=no
        make -j"$(sysctl -n hw.ncpu)"
        make install
    )
    mkdir -p "$PREFIX/share/doc/ngspice"
    cp "$WORK_DIR/ngspice-$NGSPICE_VERSION/COPYING" "$PREFIX/share/doc/ngspice/"
    touch "$DEPS_STAMP"
}

if [ -f "$DEPS_STAMP" ]; then
    echo "== ngspice already built in $PREFIX"
else
    build_ngspice
fi

# The bundle may only carry libraries of the system besides ngspice itself
if otool -L "$PREFIX/lib/libngspice.dylib" | tail -n +2 | grep -vE '^\s+(/usr/lib/|/System/|.*libngspice)'; then
    echo "libngspice links to the libraries above, which the bundle does not carry" >&2
    exit 1
fi

echo "== imcsim"
# pkg-config finds this ngspice before any other installed, such as Homebrew's
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
cmake -S "$SOURCE_DIR" -B "$WORK_DIR/imcsim-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK_DIR/imcsim-build"
rm -rf "$WORK_DIR/stage"
# The install rules lay out the bundle: the program, ngspice in Frameworks, the notices and the licenses
cmake --install "$WORK_DIR/imcsim-build" --prefix "$WORK_DIR/stage"
codesign --verify --deep --strict "$WORK_DIR/stage/imcsim.app"
# The logic parts need the digital code models, which the install rules bundle when this ngspice has them
test -f "$WORK_DIR/stage/imcsim.app/Contents/PlugIns/digital.cm"

echo "== Disk image"
# The image opens on the bundle next to a link to Applications, to drag it into
ln -s /Applications "$WORK_DIR/stage/Applications"
mkdir -p "$OUT_DIR"
IMAGE="$OUT_DIR/imcsim-$VERSION-macos-$ARCH.dmg"
rm -f "$IMAGE"
hdiutil create -volname "imcsim $VERSION" -srcfolder "$WORK_DIR/stage" -fs HFS+ -format UDZO "$IMAGE"
ls -l "$OUT_DIR"
