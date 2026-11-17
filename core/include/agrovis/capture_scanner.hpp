#pragma once

#include <string>
#include <vector>

#include "agrovis/types.hpp"

namespace agrovis {

/// Scans a filesystem directory and groups paired multispectral files into CaptureGroups.
class CaptureScanner {
public:
    /// Discover captures in @p directoryPath (non-recursive).
    static std::vector<CaptureGroup> scanDirectory(const std::string& directoryPath);
};

}  // namespace agrovis
