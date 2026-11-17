#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>


namespace agrovis{

    struct BandRadiometryMeta{
        std::string bandName;
        float centralWavelengthNm{0.0f};

        // Radiometric parameters
        bool hasBlackLevel{false};
        float blackLevel{0.0f};     // Dark current pedestal (eg; 3200 in 16-bit)

        bool hasGain{false};
        float sensorGain{1.0f};           // Analog gain multiplier (eg; 1.0, 2.0, 4.0)

        bool hasGainAdjustment{false};
        float gainAdjustment{1.0f}; // Gain adjustment multiplier

        bool hasExposure{false};
        float exposureUs{0.0f};        // Exposure time in microseconds

        bool hasIrradiance{false};
        float irradiance{1.0f};        // Downswelling DLS reading (E_sun * p_LS)

        // Vignette polynomial: V(r) = 1 + sum_{i=0...n} k[i] * r^(i+1)
        bool vignetteAlreadyApplied{false};
        bool hasVignette{false};
        float vignetteCx{-1.0f}; // < 0 signals fallback to width / 2
        float vignetteCy{-1.0f}; // < 0 signals fallback to height / 2
        std::array<float, 6> vignetteK{};

        // Geometric / alignemnt metadata
        bool dewarpAlreadyApplied{false};
        float focalLengthPx{0.0f}; // Focal length in pixels (fx, fy)
        float opticalCenterX{0.0f}; // Optical center in pixels (cx, cy)
        float opticalCenterY{0.0f};
        bool hasRelativeOpticalOffset{false};
        float relativeOpticalCenterX{0.0f}; // Relative optical center offset in pixels (cx, cy)
        float relativeOpticalCenterY{0.0f};
        bool hasPerspectiveDistortion{false};
        std::array<float, 5> distortionK1K2P1P2K3{}; // Distortion coefficients (k1, k2, p1, p2, k3)
        bool hasDewarpHMatrix{false};
        std::array<float, 9> dewarpHMatrix{1.0f, 0.0f, 0.0f,
                                           0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f}; // Dewarp homography matrix (3x3)


    };

    struct FloatBand{
        int width{0};
        int height{0};
        std::vector<float> data{};

        [[nodiscard]] bool empty() const{
            return width <= 0 || height <= 0 ||
                   data.size() < static_cast<size_t>(width) * static_cast<size_t>(height);
        }
    };

    /// Reads the header/tail bytes of @p filePath and extracts embedded XMP calibration tags.
    /// Always returns a valid struct with safe defaults if tags are missing.
    BandRadiometryMeta parseBandXmpFromFile(const std::string& filePath);

    /// Pure in-memory XMP parser (for unit testing without binary image files).
    BandRadiometryMeta parseBandXmpBuffer(std::string_view xmpXml);


    /// Computes the constant scalar multiplier applied after dark subtraction and vignetting.
    /// @param maxDn      255.0f for 8-bit, 65535.0f for 16-bit (or (1<<bits)-1).
    /// @param dlsScale   Uniform scale bringing DLS relative units into ~[0, 1] reflectance.
    float computeRadianceScale(
        const BandRadiometryMeta& meta,
        float maxDn,
        float dlsScale = 25.0f);

    /// Calibrates a tightly packed 16-bit single-channel raw band into float reflectance.
    FloatBand calibrateUint16Band(
        const uint16_t* pixels,
        int width,
        int height,
        int rowStridePixels,
        const BandRadiometryMeta& meta,
        float dlsScale = 25.0f);
    
    /// Calibrates a tightly packed 8-bit single-channel raw band into float reflectance.
    FloatBand calibrateUint8Band(
        const uint8_t* pixels,
        int width,
        int height,
        int rowStridePixels,
        const BandRadiometryMeta& meta,
        float dlsScale = 25.0f);


}