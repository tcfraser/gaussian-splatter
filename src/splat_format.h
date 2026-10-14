#pragma once

#include <cstddef>
#include <cstdint>

// One record in a .splat file; the Windows x64 target uses the file's little-endian float representation.
struct QuaternionicSplat {
    float position[3];
    float scale[3];                         // Standard deviations, already linear.
    std::uint8_t rgba[4];                   // Red, green, blue, opacity.
    std::uint8_t rotation[4];               // Quaternion w, x, y, z, encoded as q * 128 + 128.
};

// CPU preprocessing produces this layout; project_splat.comp.hlsl reads the same 40 bytes.
struct CovarianceSplat {
    float position[3];
    std::uint32_t rgba;                     // R in bits 0-7, G in 8-15, B in 16-23, opacity in 24-31.
    float covariance[6];                   // Symmetric 3D covariance: xx, xy, xz, yy, yz, zz.
};
