#pragma once

#include "splat_format.h"

#include <string>
#include <vector>

bool load_splat_file(const char* path, std::vector<QuaternionicSplat>& splats, std::string& error);
bool convert_splats(const std::vector<QuaternionicSplat>& source, std::vector<CovarianceSplat>& splats, std::string& error);
