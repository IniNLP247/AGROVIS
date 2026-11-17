#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "agrovis/radiometry.hpp"
#include "agrovis/types.hpp"

namespace agrovis{

    // Input bundle of calibrated bands + optional RGB prior to spatial registration
    struct RawCaptureInput{
        RgbaImage rgb;
        BandRadiometryMeta rgbMeta;

        FloatBand blue;
        BandRadiometryMeta blueMeta;

        FloatBand green;
        BandRadiometryMeta greenMeta;

        FloatBand red;
        BandRadiometryMeta redMeta;

        FloatBand redEdge;
        BandRadiometryMeta redEdgeMeta;

        FloatBand nir;
        BandRadiometryMeta nirMeta;
    };

    // Spatial co-registration of multiple bands into a single anchor coordinate frame
    struct AlignedCaptureCube{
        int width{0};
        int height{0};

        RgbaImage baseRgb; // RGB composite for display
        FloatBand blue;
        FloatBand green;
        FloatBand red;
        FloatBand redEdge;
        FloatBand nir;

        // 1 where every active band has valid coverage (not a warped border), 0 otherwise
        std::vector<uint8_t> validMask;

        double meanCorrelation{0.0f};
        std::string statusNote; // Human-readable summary of registration quality
    };

    /// Registers all available bands (and RGB) into a common anchor frame.
    /// Warps each band at most once via composed homography/affine transforms.
    AlignedCaptureCube alignedCaptureCube(
        const RawCaptureInput& input,
        bool enableAligned = true);
}