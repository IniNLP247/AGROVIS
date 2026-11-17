#pragma once

#include <optional>
#include <string>
#include <vector>

#include "agrovis/types.hpp"

namespace agrovis {

/// Core spectral calculation, band validation, and workbench preview engine.
class SpectralEngine {
public:
    /// Returns metadata specs for all supported spectral indices.
    static const std::vector<SpectralIndexSpec>& allIndexSpecs();

    /// Lookup metadata spec for a given SpectralIndex.
    static const SpectralIndexSpec& specFor(SpectralIndex idx);

    /// Returns true if the given sensor hardware provides the bands needed for @p idx.
    static bool isIndexSupported(SensorProfile sensor, SpectralIndex idx);

    /// Human-readable sensor profile name.
    static std::string sensorDisplayName(SensorProfile sensor);

    /// Evaluates a custom spectral math expression over B, G, R, RE, NIR reflectance values.
    /// Returns std::nullopt if syntax is invalid or division by ~0 occurs.
    static std::optional<float> evaluateCustomFormula(
        const std::string& formula,
        float b, float g, float r, float re, float nir,
        std::string* errorOut = nullptr);

    /// Generates a physically grounded synthetic test plot for immediate UI & index verification.
    static WorkbenchOutput processSyntheticSample(const ProcessingConfig& config);

    /// Loads a real CaptureGroup from disk (Phase 1 preview; Phase 2 will add full radiometry/alignment).
    static WorkbenchOutput processCaptureGroup(
        const CaptureGroup& group,
        const ProcessingConfig& config);

    /// Probes reflectance & computed index values at pixel coordinate (@p x, @p y).
    static PixelProbe probePixel(const WorkbenchOutput& output, int x, int y);

    /// Maps a normalized [0, 1] scalar to an RGB triplet using the selected colormap.
    static void mapColor(ColormapKind kind, float t, uint8_t& r, uint8_t& g, uint8_t& b);
};

}  // namespace agrovis
