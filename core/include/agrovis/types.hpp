#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace agrovis {

/// Supported camera / sensor hardware profiles.
enum class SensorProfile {
    DjiMavic3M,        ///< RGB + G(560), R(650), RE(730), NIR(860)
    Pi5DualRgbNir850,  ///< Pi AI RGB + Pi Cam 3 NoIR w/ 850nm filter (no RedEdge)
    GenericMultiTiff   ///< User-supplied multi-band TIFF folder
};

/// Supported vegetation / spectral index identifiers.
enum class SpectralIndex {
    RGB,
    NDVI,
    NDRE,
    GNDVI,
    SAVI,
    OSAVI,
    MSAVI2,
    EVI,
    CIgreen,
    CIrededge,
    LCI,
    VARI,
    Custom
};

/// Multi-band geometric registration strategy.
enum class AlignmentMode {
    Off,
    Homography,
    DenseOpticalFlow
};

/// Radiometric calibration strategy.
enum class RadiometryMode {
    RawDN,
    ExifNormalized,
    ReferencePanel
};

/// False-color visualization palette.
enum class ColormapKind {
    RdYlGn,
    Viridis,
    Turbo,
    Grayscale
};

/// Execution target for local edge vs remote SaaS worker.
enum class ExecutionMode {
    LocalCore,
    CloudSaaS
};

/// ML segmentation / detection model slot.
enum class MlModelMode {
    None,
    CanopySoilMask,
    StressAnomalySeg,
    CustomOnnx
};

/// Static metadata describing a spectral index and sensor requirements.
struct SpectralIndexSpec {
    SpectralIndex id;
    std::string shortName;
    std::string displayName;
    std::string formula;
    bool requiresNir{false};
    bool requiresRedEdge{false};
    bool requiresBlue{false};
    float defaultMin{-1.0f};
    float defaultMax{1.0f};
};

/// Grouped files belonging to a single multi-band capture trigger.
struct CaptureGroup {
    std::string groupId;
    std::string timestampLabel;
    SensorProfile sensorProfile{SensorProfile::DjiMavic3M};
    std::string rgbPath;
    std::string bluePath;
    std::string greenPath;
    std::string redPath;
    std::string redEdgePath;
    std::string nirPath;
    int width{0};
    int height{0};
    uint64_t totalBytes{0};

    [[nodiscard]] bool hasRgb() const { return !rgbPath.empty(); }
    [[nodiscard]] bool hasBlue() const { return !bluePath.empty(); }
    [[nodiscard]] bool hasGreen() const { return !greenPath.empty(); }
    [[nodiscard]] bool hasRed() const { return !redPath.empty(); }
    [[nodiscard]] bool hasRedEdge() const { return !redEdgePath.empty(); }
    [[nodiscard]] bool hasNir() const { return !nirPath.empty(); }

    [[nodiscard]] int spectralBandCount() const {
        return (hasBlue() ? 1 : 0) + (hasGreen() ? 1 : 0) + (hasRed() ? 1 : 0) +
               (hasRedEdge() ? 1 : 0) + (hasNir() ? 1 : 0);
    }
};

/// Configuration passed into the spectral engine.
struct ProcessingConfig {
    SensorProfile sensorProfile{SensorProfile::DjiMavic3M};
    SpectralIndex index{SpectralIndex::NDVI};
    AlignmentMode alignment{AlignmentMode::Homography};
    RadiometryMode radiometry{RadiometryMode::ExifNormalized};
    ColormapKind colormap{ColormapKind::RdYlGn};
    MlModelMode mlModel{MlModelMode::CanopySoilMask};
    bool maskSoil{true};
    bool maskWater{true};
    bool maskShadow{false};
    bool maskSkyGlare{true};
    float clampMin{0.08f};
    float clampMax{0.90f};
    std::string customFormula{"(NIR - R) / (NIR + R)"};
};

/// Zonal health & summary telemetry for the active capture.
struct AnalysisSummary {
    double soilPct{0.0};
    double stressedPct{0.0};
    double moderatePct{0.0};
    double healthyPct{0.0};
    double meanIndex{0.0};
    double stdDevIndex{0.0};
    double canopyCoverPct{0.0};
    double processingMs{0.0};
    std::string statusNote;
    bool indexSupported{true};
};

/// Tightly packed RGBA8888 pixel buffer.
struct RgbaImage {
    int width{0};
    int height{0};
    std::vector<uint8_t> pixels;

    [[nodiscard]] bool empty() const {
        return width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * height * 4;
    }
};

/// Full output bundle for the viewport and cursor pixel inspector.
struct WorkbenchOutput {
    RgbaImage baseRgb;
    RgbaImage indexOverlay;
    int width{0};
    int height{0};
    std::vector<float> blueBand;
    std::vector<float> greenBand;
    std::vector<float> redBand;
    std::vector<float> redEdgeBand;
    std::vector<float> nirBand;
    std::vector<float> indexValues;
    std::vector<uint8_t> validMask;  ///< 1 = active canopy/valid pixel, 0 = masked
    AnalysisSummary summary;
};

/// Pixel-level readout for mouse hover inspection.
struct PixelProbe {
    bool valid{false};
    bool masked{false};
    int x{0};
    int y{0};
    float blue{0.0f};
    float green{0.0f};
    float red{0.0f};
    float redEdge{0.0f};
    float nir{0.0f};
    float indexValue{0.0f};
};

}  // namespace agrovis
