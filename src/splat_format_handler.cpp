#include "splat_format_handler.h"
#include "splat_format.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

bool load_splat_file(const char* path, std::vector<QuaternionicSplat>& splats, std::string& error)
{
    splats.clear();
    error.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Could not open .splat file: " + std::string(path);
        return false;
    }

    const std::streamoff byte_count = file.tellg();
    if (byte_count <= 0 || byte_count % static_cast<std::streamoff>(sizeof(QuaternionicSplat)) != 0) {
        error = "The .splat file must have a positive size divisible by 32 bytes.";
        return false;
    }
    const std::uintmax_t count = static_cast<std::uintmax_t>(byte_count) / sizeof(QuaternionicSplat);
    if (count > splats.max_size() || byte_count > (std::numeric_limits<std::streamsize>::max)()) {
        error = "The .splat file is too large to load.";
        return false;
    }

    splats.resize(static_cast<std::size_t>(count));
    file.seekg(0, std::ios::beg);
    // The records are trivially copyable and have the exact file layout, checked in splat_format.h.
    if (!file.read(reinterpret_cast<char*>(splats.data()), static_cast<std::streamsize>(byte_count))) {
        splats.clear();
        error = "Could not read all .splat records.";
        return false;
    }
    return true;
}

static bool convert_splat(const QuaternionicSplat& source, CovarianceSplat& splat, std::string& error)
{
    float variances[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(source.position[axis]) || !std::isfinite(source.scale[axis]) || source.scale[axis] <= 0.0f) {
            error = "Position must be finite and scale must be finite and positive.";
            return false;
        }
        variances[axis] = source.scale[axis] * source.scale[axis];
        if (!std::isfinite(variances[axis]) || variances[axis] <= 0.0f) {
            error = "Squared scale is outside the supported float range.";
            return false;
        }
        splat.position[axis] = source.position[axis];
    }

    float quaternion[4];
    float length_squared = 0.0f;
    for (std::size_t component = 0; component < 4; ++component) {
        quaternion[component] = (static_cast<float>(source.rotation[component]) - 128.0f) / 128.0f;
        length_squared += quaternion[component] * quaternion[component];
    }
    if (length_squared == 0.0f) {
        error = "Rotation quaternion has zero length.";
        return false;
    }
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    for (float& component : quaternion) {
        component *= inverse_length;
    }

    // Byte rounding changes the quaternion's length; normalization above restores a pure rotation.
    const float w = quaternion[0];
    const float x = quaternion[1];
    const float y = quaternion[2];
    const float z = quaternion[3];
    // R maps the splat's local axes into the file's coordinate system, using column vectors.
    const float rotation[3][3] = {
        {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - w * z), 2.0f * (x * z + w * y)},
        {2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - w * x)},
        {2.0f * (x * z - w * y), 2.0f * (y * z + w * x), 1.0f - 2.0f * (x * x + y * y)},
    };

    // Sigma = R * diag(scale squared) * transpose(R); only the six upper triangular entries are stored.
    std::size_t component = 0;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = row; column < 3; ++column) {
            float covariance = 0.0f;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                covariance += rotation[row][axis] * variances[axis] * rotation[column][axis];
            }
            if (!std::isfinite(covariance)) {
                error = "Computed covariance is outside the supported float range.";
                return false;
            }
            splat.covariance[component++] = covariance;
        }
    }

    // Cast before shifting so the opacity byte is shifted as an unsigned 32-bit value.
    splat.rgba = static_cast<std::uint32_t>(source.rgba[0])
               | (static_cast<std::uint32_t>(source.rgba[1]) << 8)
               | (static_cast<std::uint32_t>(source.rgba[2]) << 16)
               | (static_cast<std::uint32_t>(source.rgba[3]) << 24);
    return true;
}

bool convert_splats(const std::vector<QuaternionicSplat>& source, std::vector<CovarianceSplat>& splats, std::string& error)
{
    splats.clear();
    error.clear();
    if (source.empty()) {
        error = "There are no splats to convert.";
        return false;
    }

    splats.resize(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (!convert_splat(source[i], splats[i], error)) {
            error = "Splat " + std::to_string(i) + ": " + error;
            splats.clear();
            return false;
        }
    }
    return true;
}
