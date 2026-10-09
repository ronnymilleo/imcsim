# Builds the files of this folder from the ngspice sources, with the Visual Studio project ngspice ships: its
# Release|x64 configuration, which is the official ReleaseOMP one without OpenMP, and without the sound support of
# config.h, so ngspice.dll loads no DLL other than those of Windows. Run it from a Developer PowerShell of Visual
# Studio; the downloads are checked against their SHA-256 and unpacked in a temporary folder. The project names the
# toolset of Visual Studio 2022, so -Toolset gives the one to build with (v145 is the one of Visual Studio 2026).

param([string]$Toolset = 'v145')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$NgspiceVersion = '47'
$NgspiceSha256 = '894E649651F1838A14095E5A5439E7D3AA63E87EDE14D283173FDA4FCDEF675F'
$BisonVersion = '2.5.25'
$BisonSha256 = '8D324B62BE33604B2C45AD1DD34AB93D722534448F55A16CA7292DE32B6AC135'

function Get-CheckedDownload($Url, $File, $Sha256) {
    Invoke-WebRequest $Url -OutFile $File -UserAgent 'curl'
    if ((Get-FileHash $File -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Unexpected SHA-256 for $Url"
    }
}

$Work = Join-Path ([IO.Path]::GetTempPath()) 'imcsim-ngspice-build'
if (Test-Path $Work) {
    Remove-Item -Recurse -Force $Work
}
New-Item -ItemType Directory $Work | Out-Null

Get-CheckedDownload "https://downloads.sourceforge.net/project/ngspice/ng-spice-rework/$NgspiceVersion/ngspice-$NgspiceVersion.tar.gz" `
    "$Work\ngspice.tar.gz" $NgspiceSha256
# The tar of Windows, since a GNU tar first in the path (Git for Windows) reads C: as a remote host
& "$env:SystemRoot\System32\tar.exe" -xzf "$Work\ngspice.tar.gz" -C $Work
# The project calls bison from ..\..\flex-bison, relative to its visualc folder
Get-CheckedDownload "https://github.com/lexxmark/winflexbison/releases/download/v$BisonVersion/win_flex_bison-$BisonVersion.zip" `
    "$Work\flex-bison.zip" $BisonSha256
Expand-Archive "$Work\flex-bison.zip" "$Work\flex-bison"

$VisualC = "$Work\ngspice-$NgspiceVersion\visualc"
$Config = "$VisualC\src\include\ngspice\config.h"
(Get-Content $Config) -replace '^#define HAVE_LIBSNDFILE$', '/* #undef HAVE_LIBSNDFILE */' `
    -replace '^#define HAVE_LIBSAMPLERATE$', '/* #undef HAVE_LIBSAMPLERATE */' | Set-Content $Config -Encoding ASCII
$Project = "$VisualC\sharedspice.vcxproj"
(Get-Content $Project -Raw).Replace('sndfile.lib;samplerate.lib;', '') | Set-Content $Project -Encoding UTF8 -NoNewline

msbuild "$VisualC\sharedspice.sln" /m /v:minimal /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=$Toolset
if ($LASTEXITCODE -ne 0) {
    throw 'The ngspice build failed'
}

$Built = "$VisualC\sharedspice\Release.x64"
Copy-Item "$Built\ngspice.dll" "$PSScriptRoot\bin\" -Force
Copy-Item "$Built\ngspice.lib" "$PSScriptRoot\lib\" -Force
Copy-Item "$Work\ngspice-$NgspiceVersion\src\include\ngspice\sharedspice.h" "$PSScriptRoot\include\ngspice\" -Force
Remove-Item -Recurse -Force $Work
Write-Output "ngspice $NgspiceVersion built into $PSScriptRoot"
