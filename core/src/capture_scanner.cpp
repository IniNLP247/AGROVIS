#include "agrovis/capture_scanner.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace agrovis {
namespace {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isImageExtension(const std::string& upperExt) {
    return upperExt == ".JPG" || upperExt == ".JPEG" || upperExt == ".PNG" ||
           upperExt == ".TIF" || upperExt == ".TIFF" || upperExt == ".DNG";
}

std::string extractDjiTimestampLabel(const std::string& stem) {
    // Expected DJI pattern: DJI_YYYYMMDDHHMMSS_NNNN
    if (stem.size() >= 18 && stem.rfind("DJI_", 0) == 0) {
        const std::string dt = stem.substr(4, 14);
        return dt.substr(0, 4) + "-" + dt.substr(4, 2) + "-" + dt.substr(6, 2) + " " +
               dt.substr(8, 2) + ":" + dt.substr(10, 2) + ":" + dt.substr(12, 2);
    }
    return stem;
}

}  // namespace

std::vector<CaptureGroup> CaptureScanner::scanDirectory(const std::string& directoryPath) {
    std::vector<CaptureGroup> results;
    std::error_code ec;
    const fs::path root(directoryPath);
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return results;
    }

    std::map<std::string, CaptureGroup> grouped;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;

        const fs::path p = entry.path();
        const std::string extUpper = toUpper(p.extension().string());
        if (!isImageExtension(extUpper)) continue;

        const std::string stem = p.stem().string();
        const std::string stemUpper = toUpper(stem);
        const uint64_t fileBytes = entry.file_size(ec);
        const std::string fullPath = p.string();

        std::string groupKey = stem;
        enum class BandRole { RGB, Blue, Green, Red, RedEdge, Nir };
        BandRole role = BandRole::RGB;
        SensorProfile profile = SensorProfile::GenericMultiTiff;

        if (endsWith(stemUpper, "_MS_NIR")) {
            groupKey = stem.substr(0, stem.size() - 7);
            role = BandRole::Nir;
            profile = SensorProfile::DjiMavic3M;
        } else if (endsWith(stemUpper, "_MS_RE")) {
            groupKey = stem.substr(0, stem.size() - 6);
            role = BandRole::RedEdge;
            profile = SensorProfile::DjiMavic3M;
        } else if (endsWith(stemUpper, "_MS_G")) {
            groupKey = stem.substr(0, stem.size() - 5);
            role = BandRole::Green;
            profile = SensorProfile::DjiMavic3M;
        } else if (endsWith(stemUpper, "_MS_R")) {
            groupKey = stem.substr(0, stem.size() - 5);
            role = BandRole::Red;
            profile = SensorProfile::DjiMavic3M;
        } else if (endsWith(stemUpper, "_MS_B")) {
            groupKey = stem.substr(0, stem.size() - 5);
            role = BandRole::Blue;
            profile = SensorProfile::GenericMultiTiff;
        } else if (endsWith(stemUpper, "_D")) {
            groupKey = stem.substr(0, stem.size() - 2);
            role = BandRole::RGB;
            profile = SensorProfile::DjiMavic3M;
        } else if (endsWith(stemUpper, "_NIR") || endsWith(stemUpper, "_850")) {
            groupKey = stem.substr(0, stem.size() - 4);
            role = BandRole::Nir;
            profile = SensorProfile::Pi5DualRgbNir850;
        } else if (endsWith(stemUpper, "_RGB")) {
            groupKey = stem.substr(0, stem.size() - 4);
            role = BandRole::RGB;
            profile = SensorProfile::Pi5DualRgbNir850;
        }

        auto& grp = grouped[groupKey];
        if (grp.groupId.empty()) {
            grp.groupId = groupKey;
            grp.timestampLabel = extractDjiTimestampLabel(groupKey);
            grp.sensorProfile = profile;
        } else if (profile == SensorProfile::DjiMavic3M) {
            grp.sensorProfile = SensorProfile::DjiMavic3M;
        }

        grp.totalBytes += fileBytes;
        switch (role) {
            case BandRole::RGB: grp.rgbPath = fullPath; break;
            case BandRole::Blue: grp.bluePath = fullPath; break;
            case BandRole::Green: grp.greenPath = fullPath; break;
            case BandRole::Red: grp.redPath = fullPath; break;
            case BandRole::RedEdge: grp.redEdgePath = fullPath; break;
            case BandRole::Nir: grp.nirPath = fullPath; break;
        }
    }

    results.reserve(grouped.size());
    for (auto& [key, grp] : grouped) {
        results.push_back(std::move(grp));
    }
    return results;
}

}  // namespace agrovis
