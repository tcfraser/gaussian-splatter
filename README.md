# Gaussian splat renderer

## Overview

This project is a small barebones project for me to learn how 3D Gaussian Splats are rendered.
This application is built using SDL3 GPU with Direct3D 12,
The source is written in C++ with shaders written in HLSL, compiled using MSVC and DXC, respectively.

The included examples made by other talented people and are licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The authors and original sources are listed in the [examples acknowledgements](#examples-acknowledgements).

## Build & Run

From the repository directory in WSL Bash, build and pass the path to the `.splat` file you want to view, for example:

```bash
./build.sh && ./build/main.exe "examples/plant/plant.splat"
```

The application requires one file path; relative paths are resolved from the current working directory, and absolute paths must use Windows syntax because the renderer is a native Windows executable.
The demo centers the loaded splats at the world origin, fits the camera distance and clipping planes to their size, and orbits once every ten seconds while looking at the origin.
It keeps the existing convention that input data has -Y pointing up; the `.splat` format does not store an up-axis convention.

## The `.splat` format

All examples use the `.splat` layout described in the [format documentation](https://xeokit.github.io/sdk/docs/api/documents/formats.gaussiansplat.3D_Gaussian_Splatting_Format_Guide.html).
The `.splat` file contains consecutive 32-byte records, one per splat, with no header or trailer.
Floats use little-endian byte order (the least significant byte comes first).

| Byte offsets | Storage | Meaning |
|---|---|---|
| 0–11 | Three 32-bit floats | Position: x, y, z |
| 12–23 | Three 32-bit floats | Scale: standard deviation for each of three local axes |
| 24–27 | Four unsigned bytes | Red, green, blue, opacity |
| 28–31 | Four unsigned bytes | Rotation quaternion in w, x, y, z order |

Color and opacity are decoded by dividing each byte by `255.0f`.
A quaternion represents a rotation using four components; decode each component with `(float(byte) - 128.0f) / 128.0f`, then normalize the quaternion to length one to compensate for byte rounding.
Scales are already linear, and this format stores fixed RGB colors without spherical harmonics.
The file size must be a multiple of 32 bytes, and the splat count is `file_size / 32`.

## Dependency Details

- **MSVC v143 C++ x64/x86 tools:** Download [Visual Studio 2022 Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe), select **Desktop development with C++**, and include the MSVC v143 tools. Use the default installation location expected by `build.sh`.
- **Windows 11 SDK (10.0.26100 series):** Select this component in the same Build Tools installer. It provides the Windows headers and libraries used by MSVC.
- **DirectX Shader Compiler (DXC) v1.9.2609:** Download [dxc_2026_09_29.zip](https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip) from the [official September 2026 release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609) and extract its contents into `deps/dxc/`. The files `dxc.exe`, `dxcompiler.dll`, and `dxil.dll` must be together in `deps/dxc/bin/x64/`. The build script uses this Windows compiler to compile all HLSL shaders into DXIL with Shader Model 6.2 and HLSL 2021, including typed buffer loads and stores in the compute shader.
- **Direct3D 12 compatible GPU and Windows graphics driver with Shader Model 6.2 support:** Install the current Windows driver from your GPU manufacturer (NVIDIA for the intended setup).
- **SDL3 3.4.18:** Download [SDL3-devel-3.4.18-VC.zip](https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip) and extract it into `deps/`, so the headers are at `deps/SDL3-3.4.18/include/SDL3/` and the x64 library and DLL are at `deps/SDL3-3.4.18/lib/x64/`.

## Examples acknowledgements

Thank you to the following authors for making their examples available.

| Example name | Author names | Source (original) | License |
|---|---|---|---|
| [Houseplant](examples/plant/plant.splat) | Marcel Padilla | [Marcel Padilla's splats repository](https://github.com/marcelpadilla/splats/tree/c3e5ec0a8ddb3ab26f28a067a29d272fe1a23411/data/plant) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [The Fruit Bowl](<examples/the fruit bowl/the fruit bowl.splat>) | [SuperDelphi](https://superspl.at/user/superdelphi) | [Original SuperSplat scene](https://superspl.at/scene/d8ba72a6) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [LEGO Bonsai Tree v2](<examples/lego bonsai tree/lego bonsai tree.splat>) | [omar egeh](https://superspl.at/user/omaregeh) | [Original SuperSplat scene](https://superspl.at/scene/14d76a85) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| [Summer lilac (Schmetterlingsflieder)](<examples/summer lilac/summer lilac.splat>) | [Alfred Duemlein](https://superspl.at/user/alfred2010) | [Original SuperSplat scene](https://superspl.at/scene/fa93ef59) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |

The [original LEGO Bonsai Tree scene](https://superspl.at/scene/14d76a85) additionally credits its source photographs and camera data to the "bonsai" capture from [Mip-NeRF 360](https://jonbarron.info/mipnerf360/), by Jonathan T. Barron, Ben Mildenhall, Dor Verbin, Pratul P. Srinivasan, and Peter Hedman.

