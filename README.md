# Gaussian splat renderer

## Dependencies

- **MSVC v143 C++ x64/x86 tools:** Download [Visual Studio 2022 Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe), select **Desktop development with C++**, and include the MSVC v143 tools. Use the default installation location expected by `build.sh`.
- **Windows 11 SDK (10.0.26100 series):** Select this component in the same Build Tools installer. It provides the Windows headers and libraries used by MSVC.
- **DirectX Shader Compiler (DXC) v1.9.2609:** Download [dxc_2026_09_29.zip](https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip) from the [official September 2026 release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609) and extract its contents into `deps/dxc/`. The files `dxc.exe`, `dxcompiler.dll`, and `dxil.dll` must be together in `deps/dxc/bin/x64/`. The build script uses this Windows compiler to compile all HLSL shaders into DXIL with Shader Model 6.2 and HLSL 2021, including typed buffer loads and stores in the compute shader.
- **Direct3D 12 compatible GPU and Windows graphics driver with Shader Model 6.2 support:** Install the current Windows driver from your GPU manufacturer (NVIDIA for the intended setup).
- **SDL3 3.4.18:** Download [SDL3-devel-3.4.18-VC.zip](https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip) and extract it into `deps/`, so the headers are at `deps/SDL3-3.4.18/include/SDL3/` and the x64 library and DLL are at `deps/SDL3-3.4.18/lib/x64/`.

## Build

From the repository directory on a Windows drive, run in WSL Bash:

```bash
./build.sh
```
