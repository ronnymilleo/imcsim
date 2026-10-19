# ngspice for Windows

ngspice 47 as a shared library for the MSVC build, so a Windows checkout builds without downloads. Linux takes
ngspice from the system through pkg-config instead.

`bin/ngspice.dll`, `lib/ngspice.lib` and `include/ngspice/sharedspice.h` come from `build.ps1`, which builds the
[ngspice 47 sources](https://sourceforge.net/projects/ngspice/files/ng-spice-rework/47/) with the Visual Studio
project they ship. It takes the `Release|x64` configuration, the official `ReleaseOMP` one without OpenMP, and
turns off the sound support of its `config.h`. The official package needs three more DLLs: the OpenMP runtime of
Visual Studio, which Microsoft does not allow redistributing, and libsndfile and libsamplerate, which only the
`sndprint` command and voltage sources read from WAV files use, neither of them part of imcsim. This build loads
none of them: only DLLs of Windows, and its C runtime is linked statically.

`bin/digital.cm` holds the digital code models the logic gates and flip-flops run on, built by the `digital` project
of the same sources (as `digital64.cm`, after the `cmpp` tool that generates its C files). The build copies it next
to the program, where ngspice looks for a code model given by a relative name, and the program loads it when ngspice
starts.

ngspice keeps its modified BSD license, in `licenses/`.

To update ngspice, set the new version and the SHA-256 of its source archive in `build.ps1`, run it from a
Developer PowerShell of Visual Studio, and run the tests.
