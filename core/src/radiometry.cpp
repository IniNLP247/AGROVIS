#include "agrovis/radiometry.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <optional>

namespace agrovis {
    namespace {

    bool parseFloatToken(std::string_view sv, float& out) {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
            sv.remove_prefix(1);
        }
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))){
            sv.remove_suffix(1);
        }
        if (sv.empty()) return false;

        float val = 0.0f;
        const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
        if (ec != std::errc{} || !std::isfinite(val)) return false;
        out = val;
        return true;
    }

    std::optional<std::string_view> extractXmpTag(std::string_view xml, std::string_view key) {
        // 1) Attribute form: key="value"
        std::string attrNeedle; 
        attrNeedle.reserve(key.size() + 2);
        attrNeedle.append(key).append("=\"");
        if (const size_t pos = xml.find(attrNeedle); pos != std::string_view::npos) {
            const size_t start = pos + attrNeedle.size(); 
            if (const size_t end = xml.find('"', start); end != std::string_view::npos) {
                return xml.substr(start, end - start); 
            }
        }
  
        // 2) Element form: <key>value</key>
        std::string elemOpen; 
        elemOpen.reserve(key.size() + 2); 
        elemOpen.push_back('<');
        elemOpen.append(key).push_back('>');
        if (const size_t pos = xml.find(elemOpen); pos != std::string_view::npos) { 
            const size_t start = pos + elemOpen.size();
            if (const size_t end = xml.find("</", start); end != std::string_view::npos) { 
                return xml.substr(start, end - start);
            }
        }
        return std::nullopt;
    }
  
    std::optional<float> extractXmpFloat(std::string_view xml, std::string_view key) {
        const auto tag = extractXmpTag(xml, key); 
        if (!tag.has_value()) return std::nullopt;
        float v = 0.0f;
        if (!parseFloatToken(*tag, v)) return std::nullopt;
        return v;
    }
  
    std::vector<float> extractFloatList(std::string_view sv) {
        if (const size_t semi = sv.find(';'); semi != std::string_view::npos) { 
            sv.remove_prefix(semi + 1); 
        }
  
        std::vector<float> vals; 
        size_t i = 0;
        while (i < sv.size()) {
            if (sv[i] == '<') {
                const size_t close = sv.find('>', i);
                i = (close == std::string_view::npos) ? sv.size() : (close + 1);
                continue;
            }
            const char c = sv[i]; 
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
                float v = 0.0f; 
                const auto [ptr, ec] = std::from_chars(sv.data() + i, sv.data() + sv.size(), v); 
                if (ec == std::errc{} && std::isfinite(v)) {
                    vals.push_back(v);
                    i = static_cast<size_t>(ptr - sv.data());
                    continue;
                }
            }
            ++i;
        }
        return vals;
    }
  
    std::optional<std::string_view> sliceXmpPacket(std::string_view chunk) { 
        constexpr std::string_view kOpen = "<x:xmpmeta";
        constexpr std::string_view kClose = "</x:xmpmeta>"; 
        const size_t start = chunk.find(kOpen);
        if (start == std::string_view::npos) return std::nullopt;
        const size_t end = chunk.find(kClose, start); 
        if (end == std::string_view::npos) return std::nullopt; 
        return chunk.substr(start, (end + kClose.size()) - start);
    }
  
    template <typename PixelT>
    FloatBand calibrateBandImpl( 
        const PixelT* pixels,
        int width,
        int height, 
        int rowStridePixels, 
        float maxDn,
        const BandRadiometryMeta& meta,
        float dlsScale) {
        FloatBand out;
        if (pixels == nullptr || width <= 0 || height <= 0) {
            return out;
        }
  
        const int stride = (rowStridePixels >= width) ? rowStridePixels : width;
        out.width = width;
        out.height = height;
        out.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height)); 
  
        const float black = meta.hasBlackLevel ? meta.blackLevel : 0.0f;
        const float scale = computeRadianceScale(meta, maxDn, dlsScale);
        const bool applyVignette = meta.hasVignette && !meta.vignetteAlreadyApplied;
  
        const float cx = (meta.vignetteCx >= 0.0f)
            ? meta.vignetteCx
            : (meta.opticalCenterX > 0.0f ? meta.opticalCenterX : static_cast<float>(width) * 0.5f);
        const float cy = (meta.vignetteCy >= 0.0f)
            ? meta.vignetteCy
            : (meta.opticalCenterY > 0.0f ? meta.opticalCenterY : static_cast<float>(height) * 0.5f);
  
        const auto& k = meta.vignetteK;
  
        for (int y = 0; y < height; ++y) { 
            const float dy = static_cast<float>(y) - cy;
            const PixelT* src = pixels + static_cast<size_t>(y) * static_cast<size_t>(stride); 
            float* dst = out.data.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
  
            for (int x = 0; x < width; ++x) {
                const float sig = std::max(0.0f, static_cast<float>(src[x]) - black);
                float vig = 1.0f;
                if (applyVignette) {
                    const float dx = static_cast<float>(x) - cx;
                    const float r = std::sqrt(dx * dx + dy * dy);
                    vig = 1.0f + r * (k[0] + r * (k[1] + r * (k[2] + 
                                 r * (k[3] + r * (k[4] + r * k[5])))));
                } 
                dst[x] = sig * vig * scale;
            } 
        }
        return out; 
    }

    }  // namespace
  
    BandRadiometryMeta parseBandXmpFromFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return {}; 
        }
  
        const std::streamoff totalSize = file.tellg(); 
        if (totalSize <= 0) { 
            return {};
        }
  
        constexpr std::streamoff kHeadBytes = 128LL * 1024LL; 
        constexpr std::streamoff kTailBytes = 256LL * 1024LL; 
  
        const std::streamoff headLen = std::min(totalSize, kHeadBytes);
        std::string buf(static_cast<size_t>(headLen), '\0');
        file.seekg(0, std::ios::beg);
        file.read(buf.data(), headLen);
  
        if (const auto packet = sliceXmpPacket(buf); packet.has_value()) {
            return parseBandXmpBuffer(*packet); 
        }
  
        if (totalSize > kHeadBytes) {
            const std::streamoff tailLen = std::min(totalSize, kTailBytes);
            buf.assign(static_cast<size_t>(tailLen), '\0'); 
            file.seekg(totalSize - tailLen, std::ios::beg);
            file.read(buf.data(), tailLen); 
            if (const auto packet = sliceXmpPacket(buf); packet.has_value()) {
                return parseBandXmpBuffer(*packet);
            } 
        }
        return {};
    }
  
    BandRadiometryMeta parseBandXmpBuffer(std::string_view xmpXml) {
        BandRadiometryMeta meta; 
  
        if (const auto bn = extractXmpTag(xmpXml, "drone-dji:BandName")
                                .value_or(extractXmpTag(xmpXml, "Camera:BandName").value_or(""));
            !bn.empty()) {
            meta.bandName = std::string(bn);
        }
        if (const auto wl = extractXmpFloat(xmpXml, "Camera:CentralWavelength")) { 
            meta.centralWavelengthNm = *wl;
        }
  
        // Black level (pedestal)                                                                                                                                         
        if (const auto bl = extractXmpFloat(xmpXml, "Camera:BlackCurrent")) {
            meta.blackLevel = *bl;                                                                                                                                        
            meta.hasBlackLevel = true;                                                                                                                                    
        } else if (const auto blAlt = extractXmpFloat(xmpXml, "drone-dji:BlackLevel")) {                                                                                  
            meta.blackLevel = *blAlt;                                                                                                                                     
            meta.hasBlackLevel = true;                                                                                                                                    
        }                                                                                                                                                                 
                                                                                                                                                                          
        // Analog gain, band adjustment, exposure time                                                                                                                    
        if (const auto g = extractXmpFloat(xmpXml, "drone-dji:SensorGain"); g && *g > 0.0f) {                                                                             
            meta.sensorGain = *g;                                                                                                                                         
            meta.hasGain = true;                                                                                                                                          
        }                                                                                                                                                                 
        if (const auto ga = extractXmpFloat(xmpXml, "drone-dji:SensorGainAdjustment"); ga && *ga > 0.0f) {                                                                
            meta.gainAdjustment = *ga;                                                                                                                                    
            meta.hasGainAdjustment = true;                                                                                                                                
        }                                                                                                                                                                 
        if (const auto exp = extractXmpFloat(xmpXml, "drone-dji:ExposureTime"); exp && *exp > 0.0f) {                                                                     
            meta.exposureUs = *exp;                                                                                                                                       
            meta.hasExposure = true;                                                                                                                                      
        }                                                                                                                                                                 
                                                                                                                                                                          
        // Irradiance (DLS)                                                                                                                                               
        if (const auto irr = extractXmpFloat(xmpXml, "drone-dji:Irradiance"); irr && *irr > 0.0f) {                                                                       
            meta.irradiance = *irr;                                                                                                                                       
            meta.hasIrradiance = true;                                                                                                                                    
        } else if (const auto irrAlt = extractXmpFloat(xmpXml, "Camera:Irradiance");                                                                                      
                   irrAlt && *irrAlt > 0.0f) {                                                                                                                            
            meta.irradiance = *irrAlt;                                                                                                                                    
            meta.hasIrradiance = true;                                                                                                                                    
        }                                                                                                                                                                 
                                                                                                                                                                          
        // Vignette                                                                                                                                                       
        if (const auto vf = extractXmpFloat(xmpXml, "drone-dji:VignettingFlag")) {                                                                                        
            meta.vignetteAlreadyApplied = (*vf != 0.0f);                                                                                                                  
        }                                                                                                                                                                 
        using namespace std::literals::string_literals;
        auto vigTag = extractXmpTag(xmpXml, "drone-dji:VignettingData");
        if (!vigTag) {
            vigTag = extractXmpTag(xmpXml, "Camera:VignettingPolynomial");
        }
        if (vigTag) {
            const auto coeffs = extractFloatList(*vigTag);
            if (coeffs.size() >= 6) {
                for (size_t i = 0; i < 6; ++i) {
                    meta.vignetteK[i] = coeffs[i]; 
                }
                meta.hasVignette = true;
            }
        }                                                                                                                                                              
        if (const auto vcTag = extractXmpTag(xmpXml, "Camera:VignettingCenter")) {                                                                                        
            const auto vc = extractFloatList(*vcTag);                                                                                                                     
            if (vc.size() >= 2) {                                                                                                                                         
                meta.vignetteCx = vc[0];                                                                                                                                  
                meta.vignetteCy = vc[1];                                                                                                                                  
            }                                                                                                                                                             
        }                                                                                                                                                                 
                                                                                                                                                                          
        // Geometric & lens alignment tags (for Step 2B)                                                                                                                  
        if (const auto df = extractXmpFloat(xmpXml, "drone-dji:DewarpFlag")) {                                                                                            
            meta.dewarpAlreadyApplied = (*df != 0.0f);                                                                                                                    
        }                                                                                                                                                                 
        if (const auto fl = extractXmpFloat(xmpXml, "drone-dji:CalibratedFocalLength")) {                                                                                 
            meta.focalLengthPx = *fl;                                                                                                                                     
        }                                                                                                                                                                 
        if (const auto ocx = extractXmpFloat(xmpXml, "drone-dji:CalibratedOpticalCenterX")) {                                                                             
            meta.opticalCenterX = *ocx;                                                                                                                                   
        }                                                                                                                                                                 
        if (const auto ocy = extractXmpFloat(xmpXml, "drone-dji:CalibratedOpticalCenterY")) {                                                                             
            meta.opticalCenterY = *ocy;                                                                                                                                   
        }                                                                                                                                                                 
                                                                                                                                                                          
        const auto rox = extractXmpFloat(xmpXml, "drone-dji:RelativeOpticalCenterX");                                                                                     
        const auto roy = extractXmpFloat(xmpXml, "drone-dji:RelativeOpticalCenterY");                                                                                     
        if (rox && roy) {                                                                                                                                                 
            meta.relativeOpticalCenterX = *rox;                                                                                                                           
            meta.relativeOpticalCenterY = *roy;                                                                                                                           
            meta.hasRelativeOpticalOffset = true;                                                                                                                         
        }                                                                                                                                                                 
                                                                                                                                                                          
        if (const auto distTag = extractXmpTag(xmpXml, "Camera:PerspectiveDistortion")) {                                                                                 
            const auto d = extractFloatList(*distTag);                                                                                                                    
            if (d.size() >= 5) {                                                                                                                                          
                for (size_t i = 0; i < 5; ++i) {                                                                                                                          
                    meta.distortionK1K2P1P2K3[i] = d[i];                                                                                                                  
                }                                                                                                                                                         
                meta.hasPerspectiveDistortion = true;                                                                                                                     
            }                                                                                                                                                             
        }                                                                                                                                                                 
                                                                                                                                                                          
        if (const auto hTag = extractXmpTag(xmpXml, "drone-dji:DewarpHMatrix")) {                                                                                         
            const auto h = extractFloatList(*hTag);                                                                                                                       
            if (h.size() >= 9) {                                                                                                                                          
                for (size_t i = 0; i < 9; ++i) {                                                                                                                          
                    meta.dewarpHMatrix[i] = h[i];                                                                                                                         
                }                                                                                                                                                         
                meta.hasDewarpHMatrix = true;                                                                                                                             
            }                                                                                                                                                             
        }                                                                                                                                                                 
                                                                                                                                                                          
        return meta;                                                                                                                                                      
    }                                                                                                                                                                     
                                                                                                                                                                          
    float computeRadianceScale(                                                                                                                                           
        const BandRadiometryMeta& meta,                                                                                                                                   
        float maxDn,                                                                                                                                                      
        float dlsScale) {                                                                                                                                                 
        float scale = 1.0f / std::max(maxDn, 1.0f);                                                                                                                       
                                                                                                                                                                          
        if (meta.hasExposure && meta.exposureUs > 0.0f) {                                                                                                                 
            const float gain = meta.hasGain ? std::max(meta.sensorGain, 1e-4f) : 1.0f;                                                                                    
            scale *= 1.0e6f / (gain * meta.exposureUs);                                                                                                                   
        }                                                                                                                                                                 
                                                                                                                                                                          
        if (meta.hasGainAdjustment && meta.gainAdjustment > 0.0f) {                                                                                                       
            scale *= meta.gainAdjustment;                                                                                                                                 
        }                                                                                                                                                                 
                                                                                                                                                                          
        if (meta.hasIrradiance && meta.irradiance > 0.0f) {                                                                                                               
            scale /= meta.irradiance;                                                                                                                                     
            if (meta.irradiance > 100.0f) {                                                                                                                               
                scale *= dlsScale;                                                                                                                                        
            }                                                                                                                                                             
        } else if (meta.hasExposure && meta.exposureUs > 0.0f) {                                                                                                          
            // Without a sunlight sensor, normalize radiance by nominal 10ms daylight baseline                                                                            
            // so values stay near [0, 1] rather than ~100..1000.                                                                                                         
            scale *= 0.01f;                                                                                                                                               
        }                                                                                                                                                                 
                                                                                                                                                                          
        return scale;                                                                                                                                                     
    }                                                                                                                                                                     
                                                                                                                                                                          
    FloatBand calibrateUint16Band(                                                                                                                                        
        const uint16_t* pixels,                                                                                                                                           
        int width,                                                                                                                                                        
        int height,                                                                                                                                                       
        int rowStridePixels,                                                                                                                                              
        const BandRadiometryMeta& meta,                                                                                                                                   
        float dlsScale) {                                                                                                                                                 
        return calibrateBandImpl(pixels, width, height, rowStridePixels, 65535.0f, meta, dlsScale);                                                                       
    }                                                                                                                                                                     
                                                                                                                                                                          
    FloatBand calibrateUint8Band(                                                                                                                                         
        const uint8_t* pixels,                                                                                                                                            
        int width,
        int height,
        int rowStridePixels,
        const BandRadiometryMeta& meta,
        float dlsScale) {
        return calibrateBandImpl(pixels, width, height, rowStridePixels, 255.0f, meta, dlsScale);
    }
  
}  // namespace agrovis

