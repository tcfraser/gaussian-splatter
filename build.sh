#!/usr/bin/env bash
set -euo pipefail

# DXC is a build dependency; keep its executable and supporting DLLs together.
if [[ ! -f deps/dxc/bin/x64/dxc.exe || ! -f deps/dxc/bin/x64/dxcompiler.dll || ! -f deps/dxc/bin/x64/dxil.dll ]]; then
    printf '%s\n' 'DXC is missing. Extract the Windows DXC release into deps/dxc as described in README.md.' >&2
    exit 1
fi

# Start each build with an empty output directory.
rm -rf build
mkdir build

# Initialize the Windows compiler environment and compile in that same shell.
# -- "vcvars64.bat" is the setup script for setting up the x64 compiler environment
# -- "dxc.exe" compiles HLSL to DXIL: -T selects the shader profile, -E names the entry function, and -Fo sets the output file
# -- "vs_6_2", "ps_6_2", and "cs_6_2" select shader model 6.2 for the vertex, pixel (fragment), and compute shaders
# -- "-HV 2021" selects HLSL 2021; the compute shader uses typed buffer loads and stores
# -- "cl" is MSVC's compiler and linker command
# -- "/Fo" names the output directory so each C++ source gets its own object file
# -- https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options-listed-alphabetically
# -- "/link" passes options to the linker, "/SUBSYSTEM:CONSOLE" is the default (https://learn.microsoft.com/en-us/cpp/build/reference/subsystem-specify-subsystem?view=msvc-170)
cmd.exe /d /c call 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T vs_6_2 -HV 2021 -E main -Fo 'build\triangle.vert.dxil' 'src\shaders\triangle.vert.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T ps_6_2 -HV 2021 -E main -Fo 'build\triangle.frag.dxil' 'src\shaders\triangle.frag.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T cs_6_2 -HV 2021 -E main -Fo 'build\project_splat.comp.dxil' 'src\shaders\project_splat.comp.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T cs_6_2 -HV 2021 -E histogram -Fo 'build\radix_histogram.comp.dxil' 'src\shaders\radix_sort.comp.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T cs_6_2 -HV 2021 -E scan_blocks -Fo 'build\radix_scan_blocks.comp.dxil' 'src\shaders\radix_sort.comp.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T cs_6_2 -HV 2021 -E scan_sums -Fo 'build\radix_scan_sums.comp.dxil' 'src\shaders\radix_sort.comp.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T cs_6_2 -HV 2021 -E scatter -Fo 'build\radix_scatter.comp.dxil' 'src\shaders\radix_sort.comp.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T vs_6_2 -HV 2021 -E main -Fo 'build\splat_quad.vert.dxil' 'src\shaders\splat_quad.vert.hlsl' \
    '&&' 'deps\dxc\bin\x64\dxc.exe' -T ps_6_2 -HV 2021 -E main -Fo 'build\draw_splat.frag.dxil' 'src\shaders\draw_splat.frag.hlsl' \
    '&&' cl /nologo /std:c++17 /EHsc /W4 \
    '/Ideps\SDL3-3.4.18\include' \
    /Fobuild/ '/Febuild\main.exe' 'src\main.cpp' 'src\demo.cpp' 'src\camera.cpp' 'src\shader.cpp' 'src\triangle_demo.cpp' 'src\splat_format_handler.cpp' 'src\splat_renderer.cpp' 'src\radix_sort.cpp' \
    'deps\SDL3-3.4.18\lib\x64\SDL3.lib' \
    /link /SUBSYSTEM:CONSOLE

cp deps/SDL3-3.4.18/lib/x64/SDL3.dll build/
