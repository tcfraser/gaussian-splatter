# Gaussian splat renderer

## Dependencies

- **MSVC v143 C++ x64/x86 tools:** Download [Visual Studio 2022 Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe), select **Desktop development with C++**, and include the MSVC v143 tools. Use the default installation location expected by `build.sh`.
- **Windows 11 SDK (10.0.26100 series):** Select this component in the same Build Tools installer.
- **SDL3 3.4.18:** Download [SDL3-devel-3.4.18-VC.zip](https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip) and extract it into `deps/`, so the headers are at `deps/SDL3-3.4.18/include/SDL3/` and the x64 library and DLL are at `deps/SDL3-3.4.18/lib/x64/`.

## Build

From the repository directory on a Windows drive, run in WSL Bash:

```bash
./build.sh
```
