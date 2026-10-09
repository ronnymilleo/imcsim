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
| Inter | SIL Open Font License 1.1 | The Inter Project Authors | https://github.com/rsms/inter |
| JetBrains Mono | SIL Open Font License 1.1 | The JetBrains Mono Project Authors | https://github.com/JetBrains/JetBrainsMono |

Their license texts are in `vendor/imgui/LICENSE.txt`, `vendor/implot/LICENSE`, `vendor/json/LICENSE.MIT` and
`assets/fonts/`, and are installed with the program under `share/doc/imcsim/licenses/`.

## Bundled with the AppImage

The AppImage also carries these libraries next to the program, unmodified, with their license texts in
`usr/share/doc/` inside the image:

| Component | License | Source |
|---|---|---|
| ngspice (shared library) | Modified BSD license | https://ngspice.sourceforge.io/ |
| SDL 3 | zlib license | https://github.com/libsdl-org/SDL |
| GCC runtime libraries (libgomp, libgcc_s) | GPL 3 with the GCC Runtime Library Exception | https://gcc.gnu.org/ |

The Vulkan loader and the GPU driver are not bundled; they come from the system.

Catch2, in `vendor/Catch2`, only builds the tests and is not part of any binary.
