#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "agrovis/capture_scanner.hpp"
#include "agrovis/engine.hpp"
#include "agrovis/radiometry.hpp"
#include "agrovis/registration.hpp"

namespace fs = std::filesystem;

namespace {

void touchFile(const fs::path& p) {
    std::ofstream f(p);
    f << "stub";
}

void testCaptureScannerGrouping() {
    const fs::path tmpDir = fs::temp_directory_path() / "agrovis_scanner_test";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);

    touchFile(tmpDir / "DJI_20230814123323_0002_D.JPG");
    touchFile(tmpDir / "DJI_20230814123323_0002_MS_G.TIF");
    touchFile(tmpDir / "DJI_20230814123323_0002_MS_R.TIF");
    touchFile(tmpDir / "DJI_20230814123323_0002_MS_RE.TIF");
    touchFile(tmpDir / "DJI_20230814123323_0002_MS_NIR.TIF");

    touchFile(tmpDir / "PLOT_A_001_RGB.PNG");
    touchFile(tmpDir / "PLOT_A_001_850.PNG");

    const auto groups = agrovis::CaptureScanner::scanDirectory(tmpDir.string());
    assert(groups.size() == 2);

    const auto& dji = groups[0];
    assert(dji.groupId == "DJI_20230814123323_0002");
    assert(dji.sensorProfile == agrovis::SensorProfile::DjiMavic3M);
    assert(dji.hasRgb() && dji.hasGreen() && dji.hasRed() && dji.hasRedEdge() && dji.hasNir());
    assert(dji.spectralBandCount() == 4);

    const auto& pi5 = groups[1];
    assert(pi5.groupId == "PLOT_A_001");
    assert(pi5.sensorProfile == agrovis::SensorProfile::Pi5DualRgbNir850);
    assert(pi5.hasRgb() && pi5.hasNir() && !pi5.hasRedEdge());

    fs::remove_all(tmpDir);
}

void testSensorIndexGating() {
    using namespace agrovis;
    assert(SpectralEngine::isIndexSupported(SensorProfile::DjiMavic3M, SpectralIndex::NDVI));
    assert(SpectralEngine::isIndexSupported(SensorProfile::DjiMavic3M, SpectralIndex::NDRE));
    assert(SpectralEngine::isIndexSupported(SensorProfile::DjiMavic3M, SpectralIndex::LCI));

    assert(SpectralEngine::isIndexSupported(SensorProfile::Pi5DualRgbNir850, SpectralIndex::NDVI));
    assert(SpectralEngine::isIndexSupported(SensorProfile::Pi5DualRgbNir850, SpectralIndex::GNDVI));
    assert(!SpectralEngine::isIndexSupported(SensorProfile::Pi5DualRgbNir850, SpectralIndex::NDRE));
    assert(!SpectralEngine::isIndexSupported(SensorProfile::Pi5DualRgbNir850, SpectralIndex::CIrededge));
    assert(!SpectralEngine::isIndexSupported(SensorProfile::Pi5DualRgbNir850, SpectralIndex::LCI));
}

void testCustomFormulaParser() {
    using namespace agrovis;
    std::string err;
    auto ndvi = SpectralEngine::evaluateCustomFormula("(NIR - R) / (NIR + R)", 0.03f, 0.08f, 0.05f, 0.25f, 0.55f, &err);
    assert(ndvi.has_value());
    assert(std::fabs(*ndvi - 0.833333f) < 1e-4f);

    auto divZero = SpectralEngine::evaluateCustomFormula("NIR / (R - R)", 0.0f, 0.0f, 0.2f, 0.0f, 0.5f, &err);
    assert(divZero.has_value());
    assert(std::fabs(*divZero) < 1e-5f);

    auto bad = SpectralEngine::evaluateCustomFormula("(NIR - SWIR)", 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, &err);
    assert(!bad.has_value());
    assert(!err.empty());
}

void testSyntheticPlotAndProbe() {
    using namespace agrovis;
    ProcessingConfig cfg;
    cfg.sensorProfile = SensorProfile::DjiMavic3M;
    cfg.index = SpectralIndex::NDVI;
    cfg.maskSoil = true;

    const auto out = SpectralEngine::processSyntheticSample(cfg);
    assert(out.width == 720 && out.height == 480);
    assert(!out.baseRgb.empty());
    assert(!out.indexOverlay.empty());
    assert(out.summary.canopyCoverPct > 20.0 && out.summary.canopyCoverPct < 90.0);
    assert(out.summary.meanIndex > 0.35);

    // Probe center of gray reference card (x=60, y=430) -> NDVI ~ 0, masked as non-canopy
    const auto cardProbe = SpectralEngine::probePixel(out, 60, 430);
    assert(cardProbe.valid);
    assert(cardProbe.masked);
    assert(std::fabs(cardProbe.red - 0.5f) < 1e-3f);
    assert(std::fabs(cardProbe.nir - 0.5f) < 1e-3f);
    assert(std::fabs(cardProbe.indexValue) < 1e-3f);
}

void testRadiometryXmpAndCalibration() {                                                                                                                
        using namespace agrovis;                                                                                                                            
        constexpr std::string_view kSampleXmp = R"(                                                                                                         
            <x:xmpmeta>                                                                                                                                     
              <rdf:RDF>                                                                                                                                     
                <rdf:Description                                                                                                                            
                  drone-dji:BandName="Red"                                                                                                                  
                  drone-dji:SensorGain="2.650"                                                                                                              
                  drone-dji:SensorGainAdjustment="0.539128"                                                                                                 
                  drone-dji:ExposureTime="1953"                                                                                                             
                  drone-dji:Irradiance="5999.527"                                                                                                           
                  drone-dji:VignettingFlag="0"                                                                                                              
                  drone-dji:VignettingData="-0.000127722, 1.740339e-06, -5.084979e-09, 8.132045e-12, -5.833179e-15, 1.587779e-18">                          
                  <Camera:BlackCurrent>3200</Camera:BlackCurrent>                                                                                           
                  <Camera:VignettingCenter><rdf:Seq><rdf:li>1286.5</rdf:li><rdf:li>946.0</rdf:li></rdf:Seq></Camera:VignettingCenter>                       
                </rdf:Description>                                                                                                                          
              </rdf:RDF>                                                                                                                                    
            </x:xmpmeta>)";                                                                                                                                 
                                                                                                                                                            
        const BandRadiometryMeta meta = parseBandXmpBuffer(kSampleXmp);                                                                                     
        assert(meta.hasBlackLevel && std::fabs(meta.blackLevel - 3200.0f) < 1e-3f);                                                                         
        assert(meta.hasGain && std::fabs(meta.sensorGain - 2.65f) < 1e-3f);                                                                                 
        assert(meta.hasIrradiance && std::fabs(meta.irradiance - 5999.527f) < 1e-2f);                                                                       
        assert(meta.hasVignette && !meta.vignetteAlreadyApplied);                                                                                           
        assert(std::fabs(meta.vignetteCx - 1286.5f) < 1e-2f);
  
        // At dark current (3200) -> calibrated value must clamp to 0.0
        const std::array<uint16_t, 4> pixels{3200, 8480, 8480, 15000};
        const FloatBand band = calibrateUint16Band(pixels.data(), 2, 2, 2, meta);
        assert(!band.empty());
        assert(std::fabs(band.data[0]) < 1e-6f);
        assert(band.data[3] > band.data[1]);
}

void testRegistrationSyntheticShift() {
    using namespace agrovis;
    constexpr int kW = 240;
    constexpr int kH = 180;

    auto makePattern = [&](int shiftX, int shiftY, float fg, float bg) -> FloatBand {
        FloatBand b;
        b.width = kW;
        b.height = kH;
        b.data.assign(static_cast<size_t>(kW) * kH, bg);
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                const int px = x - shiftX;
                const int py = y - shiftY;
                const bool rect = (px >= 50 && px <= 140 && py >= 40 && py <= 120);
                const bool ring = ((px - 165) * (px - 165) + (py - 90) * (py - 90)) <= 22 * 22;
                if (rect || ring) {
                    b.data[static_cast<size_t>(y) * kW + x] = fg;
                }
            }
        }
        return b;
    };

    RawCaptureInput input;
    input.nir = makePattern(0, 0, 0.55f, 0.15f);
    // Shift Red band by (+10, +6) px and invert contrast like real vegetation.
    input.red = makePattern(10, 6, 0.05f, 0.25f);

    const AlignedCaptureCube cube = alignedCaptureCube(input, true);
    assert(cube.width == kW && cube.height == kH);
    assert(!cube.nir.empty() && !cube.red.empty());
    assert(cube.validMask.size() == static_cast<size_t>(kW) * kH);

#if AGROVIS_HAS_OPENCV
    // At center of aligned rectangle (95, 80), Red should align onto dark canopy (0.05) not bright bg (0.25)
    const float alignedRedCenter = cube.red.data[static_cast<size_t>(80) * kW + 95];
    assert(alignedRedCenter < 0.12f);
    // Warped right-edge pixels exposed by shift should be flagged 0 in validMask
    assert(cube.validMask[static_cast<size_t>(90) * kW + (kW - 2)] == 0);
#endif
}

void testSubfolderDatasetGrouping() {
    const fs::path tmpDir = fs::temp_directory_path() / "agrovis_subfolder_test";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir / "RGB");
    fs::create_directories(tmpDir / "Multispectral");
    fs::create_directories(tmpDir / "Masks");
    fs::create_directories(tmpDir / "Overlay");
    fs::create_directories(tmpDir / "Metadata");

    const std::string stem =
        "DJI_DateTime_2024_06_02_13_42_0035_lat_10.3040603_lon_105.2619317_alt_20.018m";
    touchFile(tmpDir / "RGB" / (stem + ".JPG"));
    touchFile(tmpDir / "Multispectral" / (stem + "_G.TIF"));
    touchFile(tmpDir / "Multispectral" / (stem + "_R.TIF"));
    touchFile(tmpDir / "Multispectral" / (stem + "_RE.TIF"));
    touchFile(tmpDir / "Multispectral" / (stem + "_NIR.TIF"));
    touchFile(tmpDir / "Masks" / (stem + ".png"));
    touchFile(tmpDir / "Overlay" / (stem + ".JPG"));

    const auto groups = agrovis::CaptureScanner::scanDirectory(tmpDir.string());
    assert(groups.size() == 1);
    const auto& g = groups[0];
    assert(g.groupId == stem);
    assert(g.timestampLabel == "2024-06-02 13:42 #0035");
    assert(g.sensorProfile == agrovis::SensorProfile::DjiMavic3M);
    assert(g.hasRgb() && g.hasGreen() && g.hasRed() && g.hasRedEdge() && g.hasNir());
    assert(g.spectralBandCount() == 4);

    fs::remove_all(tmpDir);
}

void testWaterMaskFilter() {
    using namespace agrovis;

    auto makeCube = [](bool includeNir) {
        WorkbenchOutput out;
        out.width = 5;
        out.height = 1;
        out.baseRgb.width = 5;
        out.baseRgb.height = 1;
        out.baseRgb.pixels.resize(5 * 4, 255);
        out.blueBand.resize(5);
        out.greenBand.resize(5);
        out.redBand.resize(5);
        out.redEdgeBand.resize(5, 0.2f);
        out.nirBand.resize(5);

        auto setPx = [&](int i, uint8_t rgbR, uint8_t rgbG, uint8_t rgbB,
                         float g, float r, float nir) {
            out.baseRgb.pixels[i * 4 + 0] = rgbR;
            out.baseRgb.pixels[i * 4 + 1] = rgbG;
            out.baseRgb.pixels[i * 4 + 2] = rgbB;
            out.blueBand[i] = static_cast<float>(rgbB) / 255.0f;
            out.greenBand[i] = g;
            out.redBand[i] = r;
            out.nirBand[i] = includeNir ? nir : 0.0f;
        };

        // 0: Healthy rice (must remain active)
        setPx(0, 76, 112, 33, 0.09f, 0.04f, 0.50f);
        // 1: Deep/open water (NDWI(G, NIR) > 0)
        setPx(1, 32, 42, 46, 0.12f, 0.09f, 0.03f);
        // 2: Shallow sky-reflecting paddy puddle (NDVI ~ 0.41, B/R > 0.83, suppressed NIR)
        setPx(2, 88, 102, 90, 0.10f, 0.08f, 0.19f);
        // 3: Ripening yellow rice (NDVI ~ 0.46, B/R ~ 0.58 -> must NOT be masked as water)
        setPx(3, 115, 114, 67, 0.13f, 0.14f, 0.38f);
        // 4: Uncalibrated 8-bit lush green rice where G slightly exceeds NIR (NDVI ~ 0.20, bright RGB green)
        setPx(4, 94, 118, 56, 0.24f, 0.15f, 0.22f);
        return out;
    };

    ProcessingConfig cfg;
    cfg.index = SpectralIndex::NDVI;
    cfg.maskSoil = true;
    cfg.maskWater = true;

    auto masked = makeCube(true);
    SpectralEngine::renderIndexOverlay(cfg, masked);
    assert(masked.validMask[0] == 1);
    assert(masked.validMask[1] == 0);
    assert(masked.validMask[2] == 0);
    assert(masked.validMask[3] == 1);
    assert(masked.validMask[4] == 1);

    cfg.maskWater = false;
    auto unmasked = makeCube(true);
    SpectralEngine::renderIndexOverlay(cfg, unmasked);
    assert(unmasked.validMask[0] == 1);
    assert(unmasked.validMask[2] == 1);
    assert(unmasked.validMask[3] == 1);
    assert(unmasked.validMask[4] == 1);

    // Without an NIR band, water masking must not false-trigger on visible green foliage.
    cfg.maskSoil = false;
    cfg.maskWater = true;
    auto rgbOnly = makeCube(false);
    SpectralEngine::renderIndexOverlay(cfg, rgbOnly);
    assert(rgbOnly.validMask[0] == 1);
    assert(rgbOnly.validMask[3] == 1);
}

void testHealthSummaryFollowsActiveIndexAndScale() {
    using namespace agrovis;

    WorkbenchOutput out;
    out.width = 2;
    out.height = 1;
    out.baseRgb.width = 2;
    out.baseRgb.height = 1;
    out.baseRgb.pixels = {
        95, 130, 50, 255,   // Vibrant green leaf (~VARI +0.20)
        95, 130, 50, 255
    };
    out.blueBand = {0.20f, 0.20f};
    out.greenBand = {0.21f, 0.21f};
    out.redBand = {0.16f, 0.16f};
    out.redEdgeBand = {0.24f, 0.24f};
    // Low uncalibrated NIR (~NDVI 0.14): at standard 0.08..0.90 it's low,
    // but at 0.00..0.20 scale or in VARI mode it must register as Healthy!
    out.nirBand = {0.21f, 0.21f};

    ProcessingConfig cfg;
    cfg.index = SpectralIndex::NDVI;
    cfg.maskSoil = false;
    cfg.maskWater = false;
    cfg.clampMin = 0.00f;
    cfg.clampMax = 0.20f;
    auto rescaled = out;
    SpectralEngine::renderIndexOverlay(cfg, rescaled);
    // Boosted visual display range stretches the overlay toward green (G > R) while
    // Crop Health Estimate stays anchored to Standard NDVI benchmarks (low NIR => Stressed).
    assert(rescaled.indexOverlay.pixels[1] > rescaled.indexOverlay.pixels[0]);
    assert(rescaled.summary.stressedPct > 90.0);

    // Switching to VARI must evaluate VARI (~+0.20 -> Healthy on Standard -0.08..0.22) even with maskSoil enabled.
    cfg.index = SpectralIndex::VARI;
    cfg.maskSoil = true;
    cfg.clampMin = -0.08f;
    cfg.clampMax = 0.22f;
    auto variOut = out;
    SpectralEngine::renderIndexOverlay(cfg, variOut);
    assert(variOut.summary.canopyCoverPct > 90.0);
    assert(variOut.summary.healthyPct > 90.0);
}

}  // namespace

int main() {
    testCaptureScannerGrouping();
    testSubfolderDatasetGrouping();
    testSensorIndexGating();
    testCustomFormulaParser();
    testSyntheticPlotAndProbe();
    testRadiometryXmpAndCalibration();
    testRegistrationSyntheticShift();
    testWaterMaskFilter();
    testHealthSummaryFollowsActiveIndexAndScale();
    std::cout << "All agrovis_core tests passed.\n";
    return 0;
}
