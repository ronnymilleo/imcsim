# ngspice for Windows

ngspice 47 as a shared library for the MSVC build, so a Windows checkout builds without downloads. Linux takes
ngspice from the system through pkg-config instead.

| File | Origin |
|---|---|
| `bin/ngspice.dll`, `lib/ngspice.lib`, `include/ngspice/sharedspice.h` | `Spice64_dll` of [ngspice-47_dll_64.7z](https://sourceforge.net/projects/ngspice/files/ng-spice-rework/47/), the official MSVC build |
| `bin/sndfile.dll` | `bin/` of [libsndfile-1.2.2-win64.zip](https://github.com/libsndfile/libsndfile/releases/tag/1.2.2) |
| `bin/samplerate.dll` | `bin/` of [libsamplerate-0.2.2-win64.zip](https://github.com/libsndfile/libsamplerate/releases/tag/0.2.2) |

`ngspice.dll` loads the last two, which its package leaves out. It also loads `libomp140.x86_64.dll`, the LLVM
OpenMP runtime of Visual Studio: Microsoft does not allow redistributing it, so it stays out of this folder and the
build copies it from the Visual Studio that compiles the project.

The licenses are in `licenses/`: ngspice keeps its modified BSD license, libsndfile the LGPL 2.1 (its source is
at the link above) and libsamplerate the BSD 2-clause license.

To update ngspice, replace the three ngspice files with the ones of the new package and run the tests.
