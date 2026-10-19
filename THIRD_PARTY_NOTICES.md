# Third-party notices

imcsim is licensed under the GNU General Public License, version 3 or (at your option) any later version
(`GPL-3.0-or-later`, see `LICENSE`). Its binaries include or bundle the following software, each under its own
license.

## Compiled into the program

| Component | License | Copyright | Source |
|---|---|---|---|
| Dear ImGui (docking branch) | MIT | Omar Cornut | https://github.com/ocornut/imgui |
| ImPlot | MIT | Evan Pezent | https://github.com/epezent/implot |
| nlohmann/json | MIT | Niels Lohmann | https://github.com/nlohmann/json |
| SDL 3 | zlib license | Sam Lantinga | https://github.com/libsdl-org/SDL |
| Inter | SIL Open Font License 1.1 | The Inter Project Authors | https://github.com/rsms/inter |
| JetBrains Mono | SIL Open Font License 1.1 | The JetBrains Mono Project Authors | https://github.com/JetBrains/JetBrainsMono |

Their license texts are in `vendor/imgui/LICENSE.txt`, `vendor/implot/LICENSE`, `vendor/json/LICENSE.MIT`,
`vendor/SDL/LICENSE.txt` and `assets/fonts/`, and are installed with the program under `share/doc/imcsim/licenses/`
(Linux), `licenses/` (the Windows zip) or `imcsim.app/Contents/Resources/licenses/` (macOS).

## Bundled with the AppImage

The AppImage also carries these libraries next to the program, unmodified, with their license texts in
`usr/share/doc/` inside the image:

| Component | License | Source |
|---|---|---|
| ngspice (shared library and digital code models) | Modified BSD license | https://ngspice.sourceforge.io/ |
| GCC runtime libraries (libgomp, libgcc_s) | GPL 3 with the GCC Runtime Library Exception | https://gcc.gnu.org/ |

## Bundled with the Windows zip

The zip carries ngspice next to the program, as `ngspice.dll` and its digital code models `digital.cm`, with its
license text in `licenses/`:

| Component | License | Source |
|---|---|---|
| ngspice 47 (shared library and digital code models) | Modified BSD license | https://ngspice.sourceforge.io/ |

It is built from the ngspice sources by `vendor/ngspice-windows/build.ps1`, which changes only the build
configuration (no OpenMP, no sound support), so it needs no other library.

## Bundled with the macOS app

The app carries ngspice in `Contents/Frameworks`, as `libngspice.0.dylib`, and its digital code models in
`Contents/PlugIns/digital.cm`, with its license text in `Contents/Resources/licenses/`:

| Component | License | Source |
|---|---|---|
| ngspice 47 (shared library and digital code models) | Modified BSD license | https://ngspice.sourceforge.io/ |

`packaging/macos/build.sh` builds it from the ngspice sources, without OpenMP, FFTW, readline or X11, so it needs no
library other than those of macOS.

The GPU driver and the graphics API (the Vulkan loader on Linux, Direct3D 12 on Windows, Metal on macOS) are not
bundled; they come from the system.

Catch2, in `vendor/Catch2`, only builds the tests and is not part of any binary.
