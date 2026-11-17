#include "agrovis/capture_scanner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <string_view>

namespace fs = std::filesystem;

namespace agrovis {
namespace {

enum class BandRole { RGB, Blue, Green, Red, RedEdge, Nir };

struct SuffixRule {
    std::string_view suffix;
    BandRole role;
    SensorProfile profile;
};

constexpr std::array<SuffixRule, 13> kSuffixRules{{
    {"_MS_NIR", BandRole::Nir,     SensorProfile::DjiMavic3M},
    {"_MS_RE",  BandRole::RedEdge, SensorProfile::DjiMavic3M},
    {"_MS_G",   BandRole::Green,   SensorProfile::DjiMavic3M},
    {"_MS_R",   BandRole::Red,     SensorProfile::DjiMavic3M},
    {"_MS_B",   BandRole::Blue,    SensorProfile::GenericMultiTiff},
    {"_NIR",    BandRole::Nir,     SensorProfile::Pi5DualRgbNir850},
    {"_850",    BandRole::Nir,     SensorProfile::Pi5DualRgbNir850},
    {"_RE",     BandRole::RedEdge, SensorProfile::DjiMavic3M},
    {"_G",      BandRole::Green,   SensorProfile::DjiMavic3M},
    {"_R",      BandRole::Red,     SensorProfile::DjiMavic3M},
    {"_B",      BandRole::Blue,    SensorProfile::GenericMultiTiff},
    {"_RGB",    BandRole::RGB,     SensorProfile::Pi5DualRgbNir850},
    {"_D",      BandRole::RGB,     SensorProfile::DjiMavic3M},
}};

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool endsWith(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isImageExtension(std::string_view upperExt) {
    return upperExt == ".JPG" || upperExt == ".JPEG" || upperExt == ".PNG" ||
           upperExt == ".TIF" || upperExt == ".TIFF" || upperExt == ".DNG";
}

bool isIgnoredSubdir(std::string_view upperDir) {
    return upperDir == "MASKS" || upperDir == "MASK" ||
           upperDir == "OVERLAY" || upperDir == "OVERLAYS" ||
           upperDir == "METADATA" || upperDir == "LABELS" ||
           upperDir == "LABEL" || upperDir == "ANNOTATIONS" ||
           upperDir == "GROUNDTRUTH" || upperDir == "GROUND_TRUTH" ||
           upperDir == ".GIT" || upperDir == "__MACOSX";
}

bool isRgbSubdir(std::string_view upperDir) {
    return upperDir == "RGB" || upperDir == "COLOR" ||
           upperDir == "TRUECOLOR" || upperDir == "TRUE_COLOR";
}

std::string extractDjiTimestampLabel(const std::string& stem) {
    // Pattern 1: DJI_YYYYMMDDHHMMSS_NNNN
    if (stem.size() >= 18 && stem.rfind("DJI_", 0) == 0 &&
        std::isdigit(static_cast<unsigned char>(stem[4]))) {
        const std::string dt = stem.substr(4, 14);
        return dt.substr(0, 4) + "-" + dt.substr(4, 2) + "-" + dt.substr(6, 2) + " " +
               dt.substr(8, 2) + ":" + dt.substr(10, 2) + ":" + dt.substr(12, 2);
    }
    // Pattern 2: DJI_DateTime_YYYY_MM_DD_HH_MM[_NNNN...]
    if (stem.size() >= 29 && stem.rfind("DJI_DateTime_", 0) == 0) {
        std::string label = stem.substr(13, 4) + "-" + stem.substr(18, 2) + "-" +
                            stem.substr(21, 2) + " " + stem.substr(24, 2) + ":" +
                            stem.substr(27, 2);
        if (stem.size() >= 34 && stem[29] == '_') {
            label += " #" + stem.substr(30, 4);
        }
        return label;
    }
    return stem;
}

void scanSingleDirectory(
    const fs::path& dir,
    bool forceRgbRole,
    std::map<std::string, CaptureGroup>& grouped) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;

        const fs::path p = entry.path();
        const std::string extUpper = toUpper(p.extension().string());
        if (!isImageExtension(extUpper)) continue;

        const std::string stem = p.stem().string();
        const std::string stemUpper = toUpper(stem);
        const uint64_t fileBytes = entry.file_size(ec);
        const std::string fullPath = p.string();

        std::string groupKey = stem;
        BandRole role = BandRole::RGB;
        SensorProfile profile = SensorProfile::GenericMultiTiff;

        if (!forceRgbRole) {
            for (const auto& rule : kSuffixRules) {
                if (endsWith(stemUpper, rule.suffix) && stem.size() > rule.suffix.size()) {
                    groupKey = stem.substr(0, stem.size() - rule.suffix.size());
                    role = rule.role;
                    profile = rule.profile;
                    break;
                }
            }
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
    scanSingleDirectory(root, false, grouped);

    // Also inspect immediate non-excluded subdirectories (e.g. RGB/, Multispectral/).
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory(ec)) continue;
        const std::string dirUpper = toUpper(entry.path().filename().string());
        if (isIgnoredSubdir(dirUpper)) continue;
        scanSingleDirectory(entry.path(), isRgbSubdir(dirUpper), grouped);
    }

    results.reserve(grouped.size());
    for (auto& [key, grp] : grouped) {
        if (grp.hasRedEdge() || (grp.hasGreen() && grp.hasRed() && grp.hasNir())) {
            grp.sensorProfile = SensorProfile::DjiMavic3M;
        }
        results.push_back(std::move(grp));
    }
    return results;
}

}  // namespace agrovis
