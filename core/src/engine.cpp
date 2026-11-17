#include "agrovis/engine.hpp"
#include "agrovis/radiometry.hpp"
#include "agrovis/registration.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <sstream>

#if AGROVIS_HAS_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace agrovis {
namespace {

class FormulaParser {
public:
    FormulaParser(const std::string& expr, float b, float g, float r, float re, float nir)
        : expr_(expr), b_(b), g_(g), r_(r), re_(re), nir_(nir) {}

    std::optional<float> parse(std::string* errorOut) {
        pos_ = 0;
        error_.clear();
        const float val = parseExpression();
        skipSpaces();
        if (error_.empty() && pos_ != expr_.size()) {
            error_ = "Unexpected character near '" + expr_.substr(pos_, 8) + "'";
        }
        if (!error_.empty() || !std::isfinite(val)) {
            if (errorOut) *errorOut = error_.empty() ? "Non-finite expression value" : error_;
            return std::nullopt;
        }
        return val;
    }

private:
    void skipSpaces() {
        while (pos_ < expr_.size() && std::isspace(static_cast<unsigned char>(expr_[pos_]))) {
            ++pos_;
        }
    }

    float parseExpression() {
        float lhs = parseTerm();
        while (error_.empty()) {
            skipSpaces();
            if (pos_ < expr_.size() && (expr_[pos_] == '+' || expr_[pos_] == '-')) {
                const char op = expr_[pos_++];
                const float rhs = parseTerm();
                lhs = (op == '+') ? (lhs + rhs) : (lhs - rhs);
            } else {
                break;
            }
        }
        return lhs;
    }

    float parseTerm() {
        float lhs = parseFactor();
        while (error_.empty()) {
            skipSpaces();
            if (pos_ < expr_.size() && (expr_[pos_] == '*' || expr_[pos_] == '/')) {
                const char op = expr_[pos_++];
                const float rhs = parseFactor();
                if (op == '*') {
                    lhs *= rhs;
                } else {
                    if (std::fabs(rhs) < 1e-6f) {
                        lhs = 0.0f;
                    } else {
                        lhs /= rhs;
                    }
                }
            } else {
                break;
            }
        }
        return lhs;
    }

    float parseFactor() {
        skipSpaces();
        if (pos_ >= expr_.size()) {
            error_ = "Unexpected end of formula";
            return 0.0f;
        }
        if (expr_[pos_] == '+') {
            ++pos_;
            return parseFactor();
        }
        if (expr_[pos_] == '-') {
            ++pos_;
            return -parseFactor();
        }
        if (expr_[pos_] == '(') {
            ++pos_;
            const float inner = parseExpression();
            skipSpaces();
            if (pos_ >= expr_.size() || expr_[pos_] != ')') {
                error_ = "Missing closing ')'";
                return 0.0f;
            }
            ++pos_;
            return inner;
        }
        if (std::isalpha(static_cast<unsigned char>(expr_[pos_]))) {
            const size_t start = pos_;
            while (pos_ < expr_.size() && std::isalnum(static_cast<unsigned char>(expr_[pos_]))) {
                ++pos_;
            }
            std::string ident = expr_.substr(start, pos_ - start);
            std::transform(ident.begin(), ident.end(), ident.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            if (ident == "B" || ident == "BLUE") return b_;
            if (ident == "G" || ident == "GREEN") return g_;
            if (ident == "R" || ident == "RED") return r_;
            if (ident == "RE" || ident == "REDEDGE") return re_;
            if (ident == "NIR") return nir_;
            error_ = "Unknown band '" + ident + "' (use B, G, R, RE, NIR)";
            return 0.0f;
        }
        if (std::isdigit(static_cast<unsigned char>(expr_[pos_])) || expr_[pos_] == '.') {
            const char* beginPtr = expr_.c_str() + pos_;
            char* endPtr = nullptr;
            const float val = std::strtof(beginPtr, &endPtr);
            if (endPtr == beginPtr) {
                error_ = "Invalid numeric literal";
                return 0.0f;
            }
            pos_ = static_cast<size_t>(endPtr - expr_.c_str());
            return val;
        }
        error_ = std::string("Invalid token '") + expr_[pos_] + "'";
        return 0.0f;
    }

    const std::string& expr_;
    size_t pos_{0};
    std::string error_;
    float b_, g_, r_, re_, nir_;
};

float safeDiv(float num, float den) {
    return (std::fabs(den) < 1e-5f) ? 0.0f : (num / den);
}

float computeSinglePixelIndex(
    const ProcessingConfig& cfg,
    float b, float g, float r, float re, float nir,
    bool* customOk = nullptr) {
    switch (cfg.index) {
        case SpectralIndex::RGB:
        case SpectralIndex::NDVI:
            return safeDiv(nir - r, nir + r);
        case SpectralIndex::NDRE:
            return safeDiv(nir - re, nir + re);
        case SpectralIndex::GNDVI:
            return safeDiv(nir - g, nir + g);
        case SpectralIndex::SAVI:
            return safeDiv(1.5f * (nir - r), nir + r + 0.5f);
        case SpectralIndex::OSAVI:
            return safeDiv(1.16f * (nir - r), nir + r + 0.16f);
        case SpectralIndex::MSAVI2: {
            const float term = 2.0f * nir + 1.0f;
            const float rad = std::max(0.0f, term * term - 8.0f * (nir - r));
            return 0.5f * (term - std::sqrt(rad));
        }
        case SpectralIndex::EVI:
            return safeDiv(2.5f * (nir - r), nir + 6.0f * r - 7.5f * b + 1.0f);
        case SpectralIndex::CIgreen:
            return safeDiv(nir, std::max(g, 1e-4f)) - 1.0f;
        case SpectralIndex::CIrededge:
            return safeDiv(nir, std::max(re, 1e-4f)) - 1.0f;
        case SpectralIndex::LCI:
            return safeDiv(nir - re, nir + r);
        case SpectralIndex::VARI:
            return safeDiv(g - r, g + r - b);
        case SpectralIndex::Custom: {
            auto res = SpectralEngine::evaluateCustomFormula(cfg.customFormula, b, g, r, re, nir);
            if (!res.has_value()) {
                if (customOk) *customOk = false;
                return 0.0f;
            }
            return *res;
        }
    }
    return 0.0f;
}

uint8_t toByte(float v) {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

struct RgbStop {
    float pos;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

template <size_t N>
void sampleRamp(const std::array<RgbStop, N>& stops, float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float tc = std::clamp(t, 0.0f, 1.0f);
    for (size_t i = 0; i + 1 < N; ++i) {
        if (tc <= stops[i + 1].pos) {
            const float span = std::max(1e-5f, stops[i + 1].pos - stops[i].pos);
            const float alpha = (tc - stops[i].pos) / span;
            r = static_cast<uint8_t>(std::lround(stops[i].r + alpha * (stops[i + 1].r - stops[i].r)));
            g = static_cast<uint8_t>(std::lround(stops[i].g + alpha * (stops[i + 1].g - stops[i].g)));
            b = static_cast<uint8_t>(std::lround(stops[i].b + alpha * (stops[i + 1].b - stops[i].b)));
            return;
        }
    }
    r = stops.back().r;
    g = stops.back().g;
    b = stops.back().b;
}

void computeHealthSummaryAndOverlay(
    const ProcessingConfig& cfg,
    WorkbenchOutput& out,
    const std::string& statusNote) {
    const int total = out.width * out.height;
    out.indexValues.resize(total);
    const bool hasBorderMask = (out.validMask.size() == static_cast<size_t>(total));
    if (!hasBorderMask) {
        out.validMask.assign(total, 1);
    }
    out.indexOverlay.width = out.width;
    out.indexOverlay.height = out.height;
    out.indexOverlay.pixels.resize(static_cast<size_t>(total) * 4);

    bool customValid = true;
    const float invSpan = 1.0f / std::max(1e-4f, cfg.clampMax - cfg.clampMin);

    int domainCount = 0;
    int soilCount = 0;
    int stressedCount = 0;
    int moderateCount = 0;
    int healthyCount = 0;
    int activeCount = 0;
    double sum = 0.0;
    double sumSq = 0.0;

    for (int i = 0; i < total; ++i) {
        const size_t px = static_cast<size_t>(i) * 4;
        if (hasBorderMask && out.validMask[i] == 0) {
            out.indexValues[i] = 0.0f;
            out.indexOverlay.pixels[px + 0] = 11;
            out.indexOverlay.pixels[px + 1] = 15;
            out.indexOverlay.pixels[px + 2] = 14;
            out.indexOverlay.pixels[px + 3] = 255;
            continue;
        }
        ++domainCount;

        const float b = out.blueBand[i];
        const float g = out.greenBand[i];
        const float r = out.redBand[i];
        const float re = out.redEdgeBand[i];
        const float nir = out.nirBand[i];

        const float ndviProxy = safeDiv(nir - r, nir + r);
        const float brightness = (b + g + r) / 3.0f;

        bool keep = true;
        if (cfg.maskSoil && ndviProxy < 0.18f) keep = false;
        if (cfg.maskShadow && brightness < 0.045f) keep = false;
        if (cfg.maskSkyGlare && brightness > 0.72f && ndviProxy < 0.2f) keep = false;

        out.validMask[i] = keep ? 1 : 0;
        const float idxVal = computeSinglePixelIndex(cfg, b, g, r, re, nir, &customValid);
        out.indexValues[i] = idxVal;

        if (!keep) {
            ++soilCount;
        } else {
            ++activeCount;
            sum += idxVal;
            sumSq += static_cast<double>(idxVal) * idxVal;
            if (ndviProxy < 0.42f) {
                ++stressedCount;
            } else if (ndviProxy < 0.68f) {
                ++moderateCount;
            } else {
                ++healthyCount;
            }
        }

        if (cfg.index == SpectralIndex::RGB) {
            out.indexOverlay.pixels[px + 0] = out.baseRgb.pixels[px + 0];
            out.indexOverlay.pixels[px + 1] = out.baseRgb.pixels[px + 1];
            out.indexOverlay.pixels[px + 2] = out.baseRgb.pixels[px + 2];
            out.indexOverlay.pixels[px + 3] = 255;
        } else if (!keep) {
            // Masked non-canopy pixels render muted neutral slate so bare soil/shadows stand out clearly.
            const uint8_t luma = static_cast<uint8_t>(std::clamp(brightness * 170.0f, 18.0f, 70.0f));
            out.indexOverlay.pixels[px + 0] = luma;
            out.indexOverlay.pixels[px + 1] = luma;
            out.indexOverlay.pixels[px + 2] = static_cast<uint8_t>(luma + 4);
            out.indexOverlay.pixels[px + 3] = 255;
        } else {
            const float norm = (idxVal - cfg.clampMin) * invSpan;
            uint8_t cr = 0, cg = 0, cb = 0;
            SpectralEngine::mapColor(cfg.colormap, norm, cr, cg, cb);
            out.indexOverlay.pixels[px + 0] = cr;
            out.indexOverlay.pixels[px + 1] = cg;
            out.indexOverlay.pixels[px + 2] = cb;
            out.indexOverlay.pixels[px + 3] = 255;
        }
    }

    const double invTotal = 100.0 / std::max(1, domainCount);
    out.summary.soilPct = soilCount * invTotal;
    out.summary.stressedPct = stressedCount * invTotal;
    out.summary.moderatePct = moderateCount * invTotal;
    out.summary.healthyPct = healthyCount * invTotal;
    out.summary.canopyCoverPct = activeCount * invTotal;
    if (activeCount > 0) {
        const double mean = sum / activeCount;
        const double var = std::max(0.0, (sumSq / activeCount) - mean * mean);
        out.summary.meanIndex = mean;
        out.summary.stdDevIndex = std::sqrt(var);
    }
    out.summary.indexSupported = SpectralEngine::isIndexSupported(cfg.sensorProfile, cfg.index);
    if (!out.summary.indexSupported) {
        out.summary.statusNote =
            "Unavailable on " + SpectralEngine::sensorDisplayName(cfg.sensorProfile) +
            " (no RedEdge band)";
    } else if (cfg.index == SpectralIndex::Custom && !customValid) {
        out.summary.statusNote = "Invalid custom formula — check band tokens (B, G, R, RE, NIR)";
    } else {
        out.summary.statusNote = statusNote;
    }
}

}  // namespace

const std::vector<SpectralIndexSpec>& SpectralEngine::allIndexSpecs() {
    static const std::vector<SpectralIndexSpec> specs = {
        {SpectralIndex::RGB, "RGB", "True Color RGB", "R, G, B composite", false, false, true, 0.0f, 1.0f},
        {SpectralIndex::NDVI, "NDVI", "Normalized Difference Veg. Index", "(NIR - R) / (NIR + R)", true, false, false, -0.2f, 0.9f},
        {SpectralIndex::NDRE, "NDRE", "Normalized Difference Red Edge", "(NIR - RE) / (NIR + RE)", true, true, false, -0.1f, 0.7f},
        {SpectralIndex::GNDVI, "GNDVI", "Green NDVI", "(NIR - G) / (NIR + G)", true, false, false, -0.2f, 0.9f},
        {SpectralIndex::SAVI, "SAVI", "Soil-Adjusted Veg. Index", "1.5 * (NIR - R) / (NIR + R + 0.5)", true, false, false, -0.2f, 0.85f},
        {SpectralIndex::OSAVI, "OSAVI", "Optimized SAVI", "1.16 * (NIR - R) / (NIR + R + 0.16)", true, false, false, -0.2f, 0.9f},
        {SpectralIndex::MSAVI2, "MSAVI2", "Modified SAVI 2", "(2*NIR + 1 - sqrt((2*NIR+1)^2 - 8*(NIR-R))) / 2", true, false, false, -0.2f, 0.9f},
        {SpectralIndex::EVI, "EVI", "Enhanced Vegetation Index", "2.5*(NIR - R) / (NIR + 6*R - 7.5*B + 1)", true, false, true, -0.2f, 1.0f},
        {SpectralIndex::CIgreen, "CIgreen", "Chlorophyll Index (Green)", "(NIR / G) - 1", true, false, false, 0.0f, 8.0f},
        {SpectralIndex::CIrededge, "CIre", "Chlorophyll Index (Red Edge)", "(NIR / RE) - 1", true, true, false, 0.0f, 5.0f},
        {SpectralIndex::LCI, "LCI", "Leaf Chlorophyll Index", "(NIR - RE) / (NIR + R)", true, true, false, -0.1f, 0.7f},
        {SpectralIndex::VARI, "VARI", "Visible Atmospherically Resistant", "(G - R) / (G + R - B)", false, false, true, -0.4f, 0.6f},
        {SpectralIndex::Custom, "Custom", "Custom Band Expression", "User-defined formula", false, false, false, -1.0f, 1.0f},
    };
    return specs;
}

const SpectralIndexSpec& SpectralEngine::specFor(SpectralIndex idx) {
    for (const auto& s : allIndexSpecs()) {
        if (s.id == idx) return s;
    }
    return allIndexSpecs().front();
}

bool SpectralEngine::isIndexSupported(SensorProfile sensor, SpectralIndex idx) {
    const auto& spec = specFor(idx);
    if (sensor == SensorProfile::Pi5DualRgbNir850 && spec.requiresRedEdge) {
        return false;
    }
    return true;
}

std::string SpectralEngine::sensorDisplayName(SensorProfile sensor) {
    switch (sensor) {
        case SensorProfile::DjiMavic3M: return "DJI Mavic 3M (RGB + G/R/RE/NIR)";
        case SensorProfile::Pi5DualRgbNir850: return "Pi 5 Dual (RGB + 850nm NoIR)";
        case SensorProfile::GenericMultiTiff: return "Generic Multi-Band TIFF";
    }
    return "Unknown";
}

std::optional<float> SpectralEngine::evaluateCustomFormula(
    const std::string& formula,
    float b, float g, float r, float re, float nir,
    std::string* errorOut) {
    FormulaParser parser(formula, b, g, r, re, nir);
    return parser.parse(errorOut);
}

void SpectralEngine::mapColor(ColormapKind kind, float t, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (kind) {
        case ColormapKind::RdYlGn: {
            static constexpr std::array<RgbStop, 5> kRamp{{
                {0.00f, 165, 25, 38},
                {0.28f, 244, 125, 67},
                {0.52f, 248, 224, 110},
                {0.76f, 134, 203, 101},
                {1.00f, 18, 124, 58},
            }};
            sampleRamp(kRamp, t, r, g, b);
            return;
        }
        case ColormapKind::Viridis: {
            static constexpr std::array<RgbStop, 5> kRamp{{
                {0.00f, 68, 1, 84},
                {0.25f, 59, 82, 139},
                {0.50f, 33, 145, 140},
                {0.75f, 94, 201, 98},
                {1.00f, 253, 231, 37},
            }};
            sampleRamp(kRamp, t, r, g, b);
            return;
        }
        case ColormapKind::Turbo: {
            static constexpr std::array<RgbStop, 5> kRamp{{
                {0.00f, 48, 18, 59},
                {0.25f, 40, 188, 235},
                {0.50f, 164, 252, 60},
                {0.75f, 251, 126, 33},
                {1.00f, 122, 4, 3},
            }};
            sampleRamp(kRamp, t, r, g, b);
            return;
        }
        case ColormapKind::Grayscale: {
            const uint8_t v = toByte(t);
            r = g = b = v;
            return;
        }
    }
}

WorkbenchOutput SpectralEngine::processSyntheticSample(const ProcessingConfig& config) {
    const auto t0 = std::chrono::steady_clock::now();

    constexpr int kW = 720;
    constexpr int kH = 480;
    constexpr int kN = kW * kH;

    WorkbenchOutput out;
    out.width = kW;
    out.height = kH;
    out.baseRgb.width = kW;
    out.baseRgb.height = kH;
    out.baseRgb.pixels.resize(static_cast<size_t>(kN) * 4);
    out.blueBand.resize(kN);
    out.greenBand.resize(kN);
    out.redBand.resize(kN);
    out.redEdgeBand.resize(kN);
    out.nirBand.resize(kN);

    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const int idx = y * kW + x;
            const float u = static_cast<float>(x) / kW;
            const float v = static_cast<float>(y) / kH;

            // Default bare soil furrow baseline
            float b = 0.08f, g = 0.11f, r = 0.15f, re = 0.19f, nir = 0.21f;

            // Crop planting rows running diagonally with realistic leaf spacing
            const float rowWave = std::sin((u * 14.0f + v * 2.2f) * 3.14159f);
            const float leafTex = 0.03f * std::sin(u * 95.0f) * std::cos(v * 85.0f);
            const bool isCanopy = (rowWave > -0.35f);

            // Localized drought / nitrogen stress pocket centered around (0.64, 0.48)
            const float dx = (u - 0.64f) / 0.17f;
            const float dy = (v - 0.48f) / 0.21f;
            const float stressFactor = std::exp(-(dx * dx + dy * dy));

            // Mild diagonal cloud/operator shadow band across upper right
            const float shadowDiag = (u * 0.7f - v + 0.05f);
            const float shadowScale = (shadowDiag > 0.15f && shadowDiag < 0.30f) ? 0.35f : 1.0f;

            if (isCanopy) {
                // Healthy vegetation vs stressed vegetation transition
                b = (0.025f + 0.035f * stressFactor) * shadowScale;
                g = (0.095f + 0.045f * stressFactor + leafTex * 0.4f) * shadowScale;
                r = (0.038f + 0.095f * stressFactor + leafTex * 0.3f) * shadowScale;
                re = (0.240f - 0.050f * stressFactor + leafTex * 0.5f) * shadowScale;
                nir = (0.520f - 0.210f * stressFactor + leafTex) * shadowScale;
            } else {
                b = (0.075f + leafTex * 0.2f) * shadowScale;
                g = (0.105f + leafTex * 0.2f) * shadowScale;
                r = (0.145f + leafTex * 0.2f) * shadowScale;
                re = (0.175f + leafTex * 0.2f) * shadowScale;
                nir = (0.195f + leafTex * 0.2f) * shadowScale;
            }

            // 50% calibrated gray reflectance reference card in bottom-left corner
            if (x >= 28 && x <= 118 && y >= kH - 102 && y <= kH - 28) {
                b = g = r = re = nir = 0.50f;
            }

            // If sensor is Pi 5 Dual (RGB + 850nm), RedEdge channel is unpopulated (0)
            if (config.sensorProfile == SensorProfile::Pi5DualRgbNir850) {
                re = 0.0f;
            }

            out.blueBand[idx] = std::clamp(b, 0.0f, 1.0f);
            out.greenBand[idx] = std::clamp(g, 0.0f, 1.0f);
            out.redBand[idx] = std::clamp(r, 0.0f, 1.0f);
            out.redEdgeBand[idx] = std::clamp(re, 0.0f, 1.0f);
            out.nirBand[idx] = std::clamp(nir, 0.0f, 1.0f);

            // Gamma-like stretch for readable natural RGB display
            const size_t px = static_cast<size_t>(idx) * 4;
            out.baseRgb.pixels[px + 0] = toByte(std::pow(r * 2.1f, 0.72f));
            out.baseRgb.pixels[px + 1] = toByte(std::pow(g * 2.1f, 0.72f));
            out.baseRgb.pixels[px + 2] = toByte(std::pow(b * 2.1f, 0.72f));
            out.baseRgb.pixels[px + 3] = 255;
        }
    }

    computeHealthSummaryAndOverlay(
        config,
        out,
        "Synthetic test plot active (with gray reference card + stress patch)");

    const auto t1 = std::chrono::steady_clock::now();
    out.summary.processingMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return out;
}

WorkbenchOutput SpectralEngine::processCaptureGroup(
    const CaptureGroup& group,
    const ProcessingConfig& config) {
    const auto t0 = std::chrono::steady_clock::now();

#if AGROVIS_HAS_OPENCV
    struct CapturePreviewCache {
        std::mutex mutex;
        std::string key;
        WorkbenchOutput cube;
        std::string statusNote;
    };
    static CapturePreviewCache sCache;

    const bool alignEnabled = (config.alignment != AlignmentMode::Off);
    const std::string cacheKey =
        group.groupId + "|" + group.rgbPath + "|" + group.nirPath + "|" +
        group.redPath + "|" + (alignEnabled ? "1" : "0");

    {
        std::lock_guard<std::mutex> lock(sCache.mutex);
        if (!sCache.key.empty() && sCache.key == cacheKey && !sCache.cube.baseRgb.empty()) {
            WorkbenchOutput out = sCache.cube;
            computeHealthSummaryAndOverlay(config, out, sCache.statusNote);
            const auto t1 = std::chrono::steady_clock::now();
            out.summary.processingMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            return out;
        }
    }

    RawCaptureInput rawIn;
    if (group.hasRgb()) {
        cv::Mat bgr = cv::imread(group.rgbPath, cv::IMREAD_COLOR);
        if (!bgr.empty()) {
            cv::Mat rgba;
            cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
            rawIn.rgb.width = rgba.cols;
            rawIn.rgb.height = rgba.rows;
            rawIn.rgb.pixels.assign(
                rgba.data, rgba.data + static_cast<size_t>(rgba.cols) * rgba.rows * 4);
            rawIn.rgbMeta = parseBandXmpFromFile(group.rgbPath);
        }
    }

    auto loadCalibrated = [](const std::string& path, FloatBand& bandOut, BandRadiometryMeta& metaOut) {
        if (path.empty()) return;
        cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (raw.empty()) return;
        if (raw.channels() > 1) {
            cv::cvtColor(raw, raw, cv::COLOR_BGR2GRAY);
        }
        metaOut = parseBandXmpFromFile(path);
        if (raw.depth() == CV_16U) {
            bandOut = calibrateUint16Band(
                raw.ptr<uint16_t>(), raw.cols, raw.rows,
                static_cast<int>(raw.step1()), metaOut);
        } else if (raw.depth() == CV_8U) {
            bandOut = calibrateUint8Band(
                raw.ptr<uint8_t>(), raw.cols, raw.rows,
                static_cast<int>(raw.step1()), metaOut);
        } else {
            cv::Mat f32;
            raw.convertTo(f32, CV_32F);
            bandOut.width = f32.cols;
            bandOut.height = f32.rows;
            bandOut.data.assign(
                f32.ptr<float>(),
                f32.ptr<float>() + static_cast<size_t>(f32.cols) * f32.rows);
        }
    };

    loadCalibrated(group.greenPath, rawIn.green, rawIn.greenMeta);
    loadCalibrated(group.redPath, rawIn.red, rawIn.redMeta);
    loadCalibrated(group.redEdgePath, rawIn.redEdge, rawIn.redEdgeMeta);
    loadCalibrated(group.nirPath, rawIn.nir, rawIn.nirMeta);
    loadCalibrated(group.bluePath, rawIn.blue, rawIn.blueMeta);

    const AlignedCaptureCube cube = alignedCaptureCube(rawIn, alignEnabled);
    if (cube.width > 0 && cube.height > 0) {
        constexpr int kMaxPreviewDim = 1280;
        const double scale = (cube.width > kMaxPreviewDim)
            ? static_cast<double>(kMaxPreviewDim) / cube.width
            : 1.0;
        const int dstW = std::max(1, static_cast<int>(std::lround(cube.width * scale)));
        const int dstH = std::max(1, static_cast<int>(std::lround(cube.height * scale)));
        const cv::Size dstSize(dstW, dstH);
        const int total = dstW * dstH;

        auto resizeFloatBand = [&](const FloatBand& fb) -> std::vector<float> {
            if (fb.empty()) {
                return std::vector<float>(static_cast<size_t>(total), 0.0f);
            }
            if (fb.width == dstW && fb.height == dstH) {
                return fb.data;
            }
            cv::Mat src(fb.height, fb.width, CV_32F, const_cast<float*>(fb.data.data()));
            cv::Mat dst;
            cv::resize(src, dst, dstSize, 0.0, 0.0, cv::INTER_AREA);
            return std::vector<float>(
                dst.ptr<float>(), dst.ptr<float>() + static_cast<size_t>(total));
        };

        WorkbenchOutput templateOut;
        templateOut.width = dstW;
        templateOut.height = dstH;
        templateOut.baseRgb.width = dstW;
        templateOut.baseRgb.height = dstH;
        templateOut.baseRgb.pixels.resize(static_cast<size_t>(total) * 4);

        if (!cube.baseRgb.empty()) {
            cv::Mat rgbaSrc(
                cube.baseRgb.height, cube.baseRgb.width, CV_8UC4,
                const_cast<uint8_t*>(cube.baseRgb.pixels.data()));
            cv::Mat rgbaDst;
            if (rgbaSrc.cols == dstW && rgbaSrc.rows == dstH) {
                rgbaDst = rgbaSrc;
            } else {
                cv::resize(rgbaSrc, rgbaDst, dstSize, 0.0, 0.0, cv::INTER_AREA);
            }
            templateOut.baseRgb.pixels.assign(
                rgbaDst.data, rgbaDst.data + static_cast<size_t>(total) * 4);
        } else {
            const auto& monoSrc = !cube.nir.empty() ? cube.nir : cube.red;
            const std::vector<float> mono = resizeFloatBand(monoSrc);
            for (int i = 0; i < total; ++i) {
                const uint8_t v = toByte(mono[static_cast<size_t>(i)] * 1.8f);
                const size_t px = static_cast<size_t>(i) * 4;
                templateOut.baseRgb.pixels[px + 0] = v;
                templateOut.baseRgb.pixels[px + 1] = v;
                templateOut.baseRgb.pixels[px + 2] = v;
                templateOut.baseRgb.pixels[px + 3] = 255;
            }
        }

        templateOut.blueBand = resizeFloatBand(cube.blue);
        templateOut.greenBand = resizeFloatBand(cube.green);
        templateOut.redBand = resizeFloatBand(cube.red);
        templateOut.redEdgeBand = resizeFloatBand(cube.redEdge);
        templateOut.nirBand = resizeFloatBand(cube.nir);

        // Fallback for missing visible bands (e.g., Blue on Mavic 3M or G/R on Pi 5 Dual).
        const bool fillB = cube.blue.empty();
        const bool fillG = cube.green.empty();
        const bool fillR = cube.red.empty();
        if (fillB || fillG || fillR) {
            for (int i = 0; i < total; ++i) {
                const size_t px = static_cast<size_t>(i) * 4;
                if (fillR) templateOut.redBand[static_cast<size_t>(i)] = templateOut.baseRgb.pixels[px + 0] / 255.0f;
                if (fillG) templateOut.greenBand[static_cast<size_t>(i)] = templateOut.baseRgb.pixels[px + 1] / 255.0f;
                if (fillB) templateOut.blueBand[static_cast<size_t>(i)] = templateOut.baseRgb.pixels[px + 2] / 255.0f;
            }
        }

        if (cube.validMask.size() == static_cast<size_t>(cube.width) * cube.height) {
            if (cube.width == dstW && cube.height == dstH) {
                templateOut.validMask = cube.validMask;
            } else {
                cv::Mat mSrc(
                    cube.height, cube.width, CV_8U,
                    const_cast<uint8_t*>(cube.validMask.data()));
                cv::Mat mDst;
                cv::resize(mSrc, mDst, dstSize, 0.0, 0.0, cv::INTER_NEAREST);
                templateOut.validMask.assign(mDst.data, mDst.data + static_cast<size_t>(total));
            }
        }

        {
            std::lock_guard<std::mutex> lock(sCache.mutex);
            sCache.key = cacheKey;
            sCache.cube = templateOut;
            sCache.statusNote = cube.statusNote;
        }

        WorkbenchOutput out = std::move(templateOut);
        computeHealthSummaryAndOverlay(config, out, cube.statusNote);
        const auto t1 = std::chrono::steady_clock::now();
        out.summary.processingMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return out;
    }
#else
    (void)group;
#endif

    WorkbenchOutput fallback = processSyntheticSample(config);
    fallback.summary.statusNote = "Fallback synthetic view (capture files unreadable or OpenCV unavailable)";
    const auto t1 = std::chrono::steady_clock::now();
    fallback.summary.processingMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return fallback;
}

PixelProbe SpectralEngine::probePixel(const WorkbenchOutput& output, int x, int y) {
    PixelProbe probe;
    if (x < 0 || y < 0 || x >= output.width || y >= output.height) {
        return probe;
    }
    const int idx = y * output.width + x;
    if (static_cast<size_t>(idx) >= output.indexValues.size()) {
        return probe;
    }
    probe.valid = true;
    probe.masked = (output.validMask[idx] == 0);
    probe.x = x;
    probe.y = y;
    probe.blue = output.blueBand[idx];
    probe.green = output.greenBand[idx];
    probe.red = output.redBand[idx];
    probe.redEdge = output.redEdgeBand[idx];
    probe.nir = output.nirBand[idx];
    probe.indexValue = output.indexValues[idx];
    return probe;
}

}  // namespace agrovis
