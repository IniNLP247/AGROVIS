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

}  // namespace

int main() {
    testCaptureScannerGrouping();
    testSensorIndexGating();
    testCustomFormulaParser();
    testSyntheticPlotAndProbe();
    testRadiometryXmpAndCalibration();
    testRegistrationSyntheticShift();
    std::cout << "All agrovis_core tests passed.\n";
    return 0;
}
