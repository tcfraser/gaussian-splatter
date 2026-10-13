#!/usr/bin/env bash
set -euo pipefail

# Start each build with an empty output directory.
rm -rf build
mkdir build

# Initialize the Windows compiler environment and compile in that same shell.
# -- "vcvars64.bat" is the setup script for setting up the x64 compiler environment
# -- "cl" is MSVC's compiler and linker command
# -- https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options-listed-alphabetically
# -- "/link" passes options to the linker, "/SUBSYSTEM:CONSOLE" is the default (https://learn.microsoft.com/en-us/cpp/build/reference/subsystem-specify-subsystem?view=msvc-170)
cmd.exe /d /c call 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat' \
    '&&' cl /nologo /std:c++17 /EHsc /W4 \
    '/Ideps\SDL3-3.4.18\include' \
    '/Fobuild\hello_world.obj' '/Febuild\hello_world.exe' hello_world.cpp 'deps\SDL3-3.4.18\lib\x64\SDL3.lib' \
    /link /SUBSYSTEM:CONSOLE



cp deps/SDL3-3.4.18/lib/x64/SDL3.dll build/
