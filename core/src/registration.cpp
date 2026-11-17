#include "agrovis/registration.hpp"
  
    #include <algorithm>
    #include <cmath>
    #include <cstdint>
  
    #if AGROVIS_HAS_OPENCV
    #include <opencv2/core.hpp>
    #include <opencv2/imgproc.hpp>
    #include <opencv2/video/tracking.hpp>
    #endif
  
    namespace agrovis {
    namespace {
  
    #if AGROVIS_HAS_OPENCV
  
    cv::Mat floatBandToMatView(const FloatBand& band) {
        if (band.empty()) return {};
        return cv::Mat(band.height, band.width, CV_32F, const_cast<float*>(band.data.data()));
    }
  
    FloatBand matToFloatBand(const cv::Mat& m) {
        FloatBand out;
        if (m.empty()) return out;
        cv::Mat f32;
        if (m.type() != CV_32F) {
            m.convertTo(f32, CV_32F);
        } else {
            f32 = m.isContinuous() ? m : m.clone();
        }
        out.width = f32.cols;
        out.height = f32.rows; 
        out.data.assign(f32.ptr<float>(), f32.ptr<float>() + static_cast<size_t>(f32.cols) * f32.rows);
        return out;
    }
  
    RgbaImage matToRgbaImage(const cv::Mat& m) {
        RgbaImage out;
        if (m.empty()) return out;
        cv::Mat rgba = (m.type() == CV_8UC4 && m.isContinuous()) ? m : m.clone(); 
        if (rgba.type() != CV_8UC4) {
            cv::cvtColor(rgba, rgba, cv::COLOR_BGR2RGBA); 
        }
        if (!rgba.isContinuous()) { 
            rgba = rgba.clone();
        }
        out.width = rgba.cols;
        out.height = rgba.rows;
        const size_t byteCount = static_cast<size_t>(rgba.cols) * rgba.rows * 4; 
        out.pixels.assign(rgba.data, rgba.data + byteCount);
        return out;
    }
  
    cv::Mat buildPrepFeature(const cv::Mat& img, cv::Size workSize, bool useLogEdge) {
        cv::Mat sm;
        cv::resize(img, sm, workSize, 0.0, 0.0, cv::INTER_AREA);
        if (sm.type() != CV_32F) {
            sm.convertTo(sm, CV_32F); 
        }
        cv::GaussianBlur(sm, sm, cv::Size(5, 5), 1.0);
  
        cv::Mat feat;
        if (useLogEdge) {
            cv::Mat gx, gy, mag;
            cv::Sobel(sm, gx, CV_32F, 1, 0, 3);
            cv::Sobel(sm, gy, CV_32F, 0, 1, 3);
            cv::magnitude(gx, gy, mag);
            cv::log(mag + 1.0f, feat);
        } else {
            feat = sm;
        }
  
        cv::Scalar mean, stddev;
        cv::meanStdDev(feat, mean, stddev);
        feat = (feat - mean[0]) / std::max(stddev[0], 1e-5);
        return feat;
    }
  
    struct PairwiseResult { 
        cv::Mat H;  ///< 3x3 CV_64F inverse warp matrix: p_mov = H * p_ref
        double correlation{0.0};
    };
  
    PairwiseResult estimatePairwiseTransform(
        const cv::Mat& refFull,
        const cv::Mat& movFull, 
        int motionType,
        bool useLogEdge) {
        cv::Mat identity = cv::Mat::eye(3, 3, CV_64F);
        if (refFull.empty() || movFull.empty()) {
            return {identity, 0.0};
        }
  
        const int workW = 640; 
        const int workH = std::max(120, static_cast<int>(std::lround(
            static_cast<double>(workW) * refFull.rows / std::max(1, refFull.cols))));
        const cv::Size workSize(workW, workH); 
  
        // Phase correlation on log-edges handles large shifts across inverted bands cleanly.
        const cv::Mat edgeRef = buildPrepFeature(refFull, workSize, true);
        const cv::Mat edgeMov = buildPrepFeature(movFull, workSize, true);
        cv::Mat hann; 
        cv::createHanningWindow(hann, workSize, CV_32F);
  
        double pcResp = 0.0;
        const cv::Point2d shift = cv::phaseCorrelate(edgeMov, edgeRef, hann, &pcResp);
        const bool pcReasonable = 
            std::isfinite(shift.x) && std::isfinite(shift.y) && 
            std::abs(shift.x) < workW * 0.25 && std::abs(shift.y) < workH * 0.25;
  
        cv::Mat W = cv::Mat::eye(2, 3, CV_32F); 
        if (pcReasonable && pcResp >= 0.08) {
            W.at<float>(0, 2) = static_cast<float>(-shift.x);
            W.at<float>(1, 2) = static_cast<float>(-shift.y);
        }
  
        const cv::Mat eccRef = useLogEdge ? edgeRef : buildPrepFeature(refFull, workSize, false);
        const cv::Mat eccMov = useLogEdge ? edgeMov : buildPrepFeature(movFull, workSize, false);
  
        double cc = pcResp;
        cv::Mat bestW = W.clone();
        try {
            const cv::TermCriteria criteria(
                cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 100, 1e-4);
            cc = cv::findTransformECC(eccRef, eccMov, W, motionType, criteria); 
  
            const double det =
                static_cast<double>(W.at<float>(0, 0)) * W.at<float>(1, 1) -
                static_cast<double>(W.at<float>(0, 1)) * W.at<float>(1, 0);
            const bool validAffine = 
                std::isfinite(cc) && cc > 0.10 && det > 0.5 && det < 2.0 && 
                std::abs(W.at<float>(0, 2)) < workW * 0.30 && 
                std::abs(W.at<float>(1, 2)) < workH * 0.30;
            if (validAffine) {
                bestW = W;
            }
        } catch (const cv::Exception&) {
            // Keep phaseCorrelate translation fallback if ECC fails to converge.                                                                           
            cc = (pcReasonable && pcResp >= 0.12) ? pcResp : 0.0;
            if (cc <= 0.0) {
                return {identity, 0.0};
            }
        }
  
        cv::Mat W3 = cv::Mat::eye(3, 3, CV_64F);
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 3; ++c) {
                W3.at<double>(r, c) = static_cast<double>(bestW.at<float>(r, c));
            }
        }
  
        // Lift work-resolution transform cleanly to full anchor resolution: H_full = S * W3 * S^-1
        const double sx = static_cast<double>(refFull.cols) / static_cast<double>(workW);
        const double sy = static_cast<double>(refFull.rows) / static_cast<double>(workH);
        cv::Mat S = (cv::Mat_<double>(3, 3) << sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 1.0);
        cv::Mat Sinv = (cv::Mat_<double>(3, 3) << 1.0 / sx, 0.0, 0.0, 0.0, 1.0 / sy, 0.0, 0.0, 0.0, 1.0);
        return {S * W3 * Sinv, cc};
    }
  
    cv::Mat buildInitialRgbToAnchorHomography( 
        const BandRadiometryMeta& meta, 
        int rgbW,
        int rgbH,
        int anchorW,
        int anchorH) {
        if (meta.hasDewarpHMatrix) { 
            cv::Mat H(3, 3, CV_64F); 
            for (int i = 0; i < 9; ++i) { 
                H.at<double>(i / 3, i % 3) = static_cast<double>(meta.dewarpHMatrix[static_cast<size_t>(i)]); 
            }
            if (std::abs(cv::determinant(H)) > 1e-8) { 
                return H;
            }
        }
  
        // Generic / DIY dual-camera fallback: aspect-preserving centered scale p_rgb = H * p_anchor
        const double scale = std::min(
            static_cast<double>(rgbW) / std::max(1, anchorW), 
            static_cast<double>(rgbH) / std::max(1, anchorH)); 
        const double tx = 0.5 * (static_cast<double>(rgbW) - scale * anchorW);
        const double ty = 0.5 * (static_cast<double>(rgbH) - scale * anchorH);
        return (cv::Mat_<double>(3, 3) << scale, 0.0, tx, 0.0, scale, ty, 0.0, 0.0, 1.0);
    }
  
    #endif  // AGROVIS_HAS_OPENCV
  
    }  // namespace                                                                                                                                         
  
    AlignedCaptureCube alignedCaptureCube(
        const RawCaptureInput& input,
        bool enableAligned) {
        AlignedCaptureCube out;                                                                                                                             
                                                                                                                                                            
        // Select anchor resolution: NIR -> RedEdge -> Green -> Red -> Blue -> RGB                                                                          
        int anchorW = 0;
        int anchorH = 0;
        enum class AnchorKind { Nir, RedEdge, Green, Red, Blue, Rgb, None } anchorKind = AnchorKind::None;
  
        if (!input.nir.empty()) {
            anchorW = input.nir.width; anchorH = input.nir.height; anchorKind = AnchorKind::Nir;
        } else if (!input.redEdge.empty()) {
            anchorW = input.redEdge.width; anchorH = input.redEdge.height; anchorKind = AnchorKind::RedEdge;
        } else if (!input.green.empty()) {
            anchorW = input.green.width; anchorH = input.green.height; anchorKind = AnchorKind::Green;
        } else if (!input.red.empty()) {
            anchorW = input.red.width; anchorH = input.red.height; anchorKind = AnchorKind::Red;
        } else if (!input.blue.empty()) {
            anchorW = input.blue.width; anchorH = input.blue.height; anchorKind = AnchorKind::Blue;
        } else if (!input.rgb.empty()) {
            anchorW = input.rgb.width; anchorH = input.rgb.height; anchorKind = AnchorKind::Rgb;
        }
  
        if (anchorW <= 0 || anchorH <= 0 || anchorKind == AnchorKind::None) {
            out.statusNote = "No valid image bands available for registration"; 
            return out;
        }
  
        out.width = anchorW;
        out.height = anchorH;
        out.validMask.assign(static_cast<size_t>(anchorW) * anchorH, 1);
  
    #if !AGROVIS_HAS_OPENCV
        (void)enableAligned;
        out.baseRgb = input.rgb; 
        out.blue = input.blue;
        out.green = input.green;
        out.red = input.red;
        out.redEdge = input.redEdge;
        out.nir = input.nir;
        out.statusNote = "OpenCV unavailable — skipped geometric alignment"; 
        return out;
    #else 
        const cv::Size anchorSize(anchorW, anchorH); 
        const cv::Mat nirMat = floatBandToMatView(input.nir);
        const cv::Mat reMat = floatBandToMatView(input.redEdge); 
        const cv::Mat gMat = floatBandToMatView(input.green);                                                                                               
        const cv::Mat rMat = floatBandToMatView(input.red);                                                                                                 
        const cv::Mat bMat = floatBandToMatView(input.blue);                                                                                                
                                                                                                                                                            
        cv::Mat rgbMat;                                                                                                                                     
        cv::Mat rgbGreenF32;                                                                                                                                
        if (!input.rgb.empty()) {                                                                                                                           
            rgbMat = cv::Mat(                                                                                                                               
                input.rgb.height, input.rgb.width, CV_8UC4,                                                                                                 
                const_cast<uint8_t*>(input.rgb.pixels.data()));                                                                                             
            cv::Mat chG;                                                                                                                                    
            cv::extractChannel(rgbMat, chG, 1);                                                                                                             
            chG.convertTo(rgbGreenF32, CV_32F, 1.0 / 255.0);                                                                                                
        }                                                                                                                                                   
                                                                                                                                                            
        if (!enableAligned) {                                                                                                                               
            auto resizeOnly = [&](const cv::Mat& m) -> FloatBand {                                                                                          
                if (m.empty()) return {};                                                                                                                   
                if (m.cols == anchorW && m.rows == anchorH) return matToFloatBand(m);                                                                       
                cv::Mat r;                                                                                                                                  
                cv::resize(m, r, anchorSize, 0.0, 0.0, cv::INTER_AREA);                                                                                     
                return matToFloatBand(r);                                                                                                                   
            };                                                                                                                                              
            out.blue = resizeOnly(bMat);                                                                                                                    
            out.green = resizeOnly(gMat);                                                                                                                   
            out.red = resizeOnly(rMat);                                                                                                                     
            out.redEdge = resizeOnly(reMat);                                                                                                                
            out.nir = resizeOnly(nirMat);                                                                                                                   
            if (!rgbMat.empty()) {                                                                                                                          
                cv::Mat r;                                                                                                                                  
                cv::resize(rgbMat, r, anchorSize, 0.0, 0.0, cv::INTER_AREA);                                                                                
                out.baseRgb = matToRgbaImage(r);                                                                                                            
            }                                                                                                                                               
            out.statusNote = "Band alignment disabled";                                                                                                     
            return out;                                                                                                                                     
        }                                                                                                                                                   
                                                                                                                                                            
        cv::Mat anchorMat;                                                                                                                                  
        switch (anchorKind) {                                                                                                                               
            case AnchorKind::Nir: anchorMat = nirMat; break;                                                                                                
            case AnchorKind::RedEdge: anchorMat = reMat; break;                                                                                             
            case AnchorKind::Green: anchorMat = gMat; break;                                                                                                
            case AnchorKind::Red: anchorMat = rMat; break;                                                                                                  
            case AnchorKind::Blue: anchorMat = bMat; break;                                                                                                 
            case AnchorKind::Rgb: anchorMat = rgbGreenF32; break;                                                                                           
            case AnchorKind::None: break;                                                                                                                   
        }                                                                                                                                                   
                                                                                                                                                            
        double ccSum = 0.0;                                                                                                                                 
        int ccCount = 0;                                                                                                                                    
        auto recordCc = [&](double cc) {                                                                                                                    
            if (cc > 0.0) {                                                                                                                                 
                ccSum += cc;                                                                                                                                
                ++ccCount;                                                                                                                                  
            }                                                                                                                                               
        };                                                                                                                                                  
                                                                                                                                                            
        // Composed inverse warps: p_band = H_anchor_to_band * p_anchor                                                                                     
        cv::Mat H_nir = cv::Mat::eye(3, 3, CV_64F);                                                                                                         
        cv::Mat H_re = cv::Mat::eye(3, 3, CV_64F);                                                                                                          
        cv::Mat H_g = cv::Mat::eye(3, 3, CV_64F);                                                                                                           
        cv::Mat H_r = cv::Mat::eye(3, 3, CV_64F);                                                                                                           
        cv::Mat H_b = cv::Mat::eye(3, 3, CV_64F);                                                                                                           
                                                                                                                                                            
        if (anchorKind == AnchorKind::Nir) {                                                                                                                
            if (!reMat.empty()) {                                                                                                                           
                const auto res = estimatePairwiseTransform(nirMat, reMat, cv::MOTION_EUCLIDEAN, false);                                                     
                H_re = res.H;                                                                                                                               
                recordCc(res.correlation);                                                                                                                  
            }                                                                                                                                               
            if (!gMat.empty()) {                                                                                                                            
                if (!reMat.empty()) {                                                                                                                       
                    const auto res = estimatePairwiseTransform(reMat, gMat, cv::MOTION_EUCLIDEAN, true);                                                    
                    H_g = res.H * H_re;                                                                                                                     
                    recordCc(res.correlation);                                                                                                              
                } else {                                                                                                                                    
                    const auto res = estimatePairwiseTransform(nirMat, gMat, cv::MOTION_EUCLIDEAN, true);                                                   
                    H_g = res.H;                                                                                                                            
                    recordCc(res.correlation);                                                                                                              
                }                                                                                                                                           
            }                                                                                                                                               
            if (!rMat.empty()) {                                                                                                                            
                if (!gMat.empty()) {                                                                                                                        
                    const auto res = estimatePairwiseTransform(gMat, rMat, cv::MOTION_EUCLIDEAN, false);                                                    
                    H_r = res.H * H_g;                                                                                                                      
                    recordCc(res.correlation);                                                                                                              
                } else if (!reMat.empty()) {                                                                                                                
                    const auto res = estimatePairwiseTransform(reMat, rMat, cv::MOTION_EUCLIDEAN, true);                                                    
                    H_r = res.H * H_re;                                                                                                                     
                    recordCc(res.correlation);                                                                                                              
                } else {                                                                                                                                    
                    const auto res = estimatePairwiseTransform(nirMat, rMat, cv::MOTION_EUCLIDEAN, true);                                                   
                    H_r = res.H;                                                                                                                            
                    recordCc(res.correlation);                                                                                                              
                }                                                                                                                                           
            }                                                                                                                                               
            if (!bMat.empty()) {                                                                                                                            
                const cv::Mat& ref = !gMat.empty() ? gMat : (!rMat.empty() ? rMat : nirMat);                                                                
                const cv::Mat& H_ref = !gMat.empty() ? H_g : (!rMat.empty() ? H_r : H_nir);                                                                 
                const bool cross = (gMat.empty() && rMat.empty());                                                                                          
                const auto res = estimatePairwiseTransform(ref, bMat, cv::MOTION_EUCLIDEAN, cross);                                                         
                H_b = res.H * H_ref;                                                                                                                        
                recordCc(res.correlation);                                                                                                                  
            }                                                                                                                                               
        } else {                                                                                                                                            
            if (!reMat.empty() && anchorKind != AnchorKind::RedEdge) {                                                                                      
                const auto res = estimatePairwiseTransform(anchorMat, reMat, cv::MOTION_EUCLIDEAN, true);                                                   
                H_re = res.H; recordCc(res.correlation);                                                                                                    
            }                                                                                                                                               
            if (!gMat.empty() && anchorKind != AnchorKind::Green) {                                                                                         
                const auto res = estimatePairwiseTransform(anchorMat, gMat, cv::MOTION_EUCLIDEAN, false);                                                   
                H_g = res.H; recordCc(res.correlation);                                                                                                     
            }                                                                                                                                               
            if (!rMat.empty() && anchorKind != AnchorKind::Red) {                                                                                           
                const auto res = estimatePairwiseTransform(anchorMat, rMat, cv::MOTION_EUCLIDEAN, false);                                                   
                H_r = res.H; recordCc(res.correlation);                                                                                                     
            }                                                                                                                                               
            if (!bMat.empty() && anchorKind != AnchorKind::Blue) {                                                                                          
                const auto res = estimatePairwiseTransform(anchorMat, bMat, cv::MOTION_EUCLIDEAN, false);                                                   
                H_b = res.H; recordCc(res.correlation);                                                                                                     
            }                                                                                                                                               
        }                                                                                                                                                   
                                                                                                                                                            
        // Track intersection of valid non-border coverage across all warped MS bands.                                                                      
        cv::Mat validMask(anchorSize, CV_8U, cv::Scalar(255));                                                                                              
        auto warpBandAndMask = [&](const cv::Mat& src, const cv::Mat& H, bool isAnchor) -> FloatBand {                                                      
            if (src.empty()) return {};                                                                                                                     
            if (isAnchor && src.cols == anchorW && src.rows == anchorH) {                                                                                   
                return matToFloatBand(src);                                                                                                                 
            }                                                                                                                                               
            cv::Mat warped;                                                                                                                                 
            cv::warpPerspective(                                                                                                                            
                src, warped, H, anchorSize,                                                                                                                 
                cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,                                                                                                    
                cv::BORDER_CONSTANT, cv::Scalar(0.0));                                                                                                      
                                                                                                                                                            
            cv::Mat ones(src.size(), CV_8U, cv::Scalar(255));                                                                                               
            cv::Mat warpedValid;                                                                                                                            
            cv::warpPerspective(                                                                                                                            
                ones, warpedValid, H, anchorSize,                                                                                                           
                cv::INTER_NEAREST | cv::WARP_INVERSE_MAP,                                                                                                   
                cv::BORDER_CONSTANT, cv::Scalar(0));                                                                                                        
            cv::bitwise_and(validMask, warpedValid, validMask);                                                                                             
                                                                                                                                                            
            return matToFloatBand(warped);                                                                                                                  
        };                                                                                                                                                  
                                                                                                                                                            
        out.nir = warpBandAndMask(nirMat, H_nir, anchorKind == AnchorKind::Nir);                                                                            
        out.redEdge = warpBandAndMask(reMat, H_re, anchorKind == AnchorKind::RedEdge);                                                                      
        out.green = warpBandAndMask(gMat, H_g, anchorKind == AnchorKind::Green);                                                                            
        out.red = warpBandAndMask(rMat, H_r, anchorKind == AnchorKind::Red);                                                                                
        out.blue = warpBandAndMask(bMat, H_b, anchorKind == AnchorKind::Blue);                                                                              
                                                                                                                                                            
        // Align RGB into the anchor frame using DewarpHMatrix (if present) + Affine refinement.                                                            
        if (!rgbMat.empty()) {                                                                                                                              
            if (anchorKind == AnchorKind::Rgb) {                                                                                                            
                out.baseRgb = matToRgbaImage(rgbMat);                                                                                                       
            } else {                                                                                                                                        
                const BandRadiometryMeta& hMeta = input.nirMeta.hasDewarpHMatrix                                                                            
                    ? input.nirMeta                                                                                                                         
                    : (input.greenMeta.hasDewarpHMatrix ? input.greenMeta : input.redMeta);                                                                 
                                                                                                                                                            
                const cv::Mat H_init = buildInitialRgbToAnchorHomography(                                                                                   
                    hMeta, rgbMat.cols, rgbMat.rows, anchorW, anchorH);                                                                                     
                                                                                                                                                            
                // Pre-warp RGB green channel into approximate anchor geometry via H_init^-1.                                                               
                cv::Mat rgbGreenApprox;                                                                                                                     
                cv::warpPerspective(rgbGreenF32, rgbGreenApprox, H_init.inv(), anchorSize, cv::INTER_LINEAR);                                               
                                                                                                                                                            
                // Prefer matching RGB Green against MS Green (same spectral band), else Red/RE/NIR.                                                        
                const cv::Mat* refPtr = &anchorMat;                                                                                                         
                const cv::Mat* H_refPtr = &H_nir;                                                                                                           
                bool crossSpectral = (anchorKind == AnchorKind::Nir || anchorKind == AnchorKind::RedEdge);                                                  
                if (!gMat.empty()) {                                                                                                                        
                    refPtr = &gMat; H_refPtr = &H_g; crossSpectral = false;                                                                                 
                } else if (!rMat.empty()) {                                                                                                                 
                    refPtr = &rMat; H_refPtr = &H_r; crossSpectral = false;                                                                                 
                } else if (!reMat.empty()) {                                                                                                                
                    refPtr = &reMat; H_refPtr = &H_re; crossSpectral = true;                                                                                
                }                                                                                                                                           
                                                                                                                                                            
                const auto rgbRes = estimatePairwiseTransform(                                                                                              
                    *refPtr, rgbGreenApprox, cv::MOTION_AFFINE, crossSpectral);                                                                             
                recordCc(rgbRes.correlation);                                                                                                               
                                                                                                                                                            
                const cv::Mat H_rgb = H_init * rgbRes.H * (*H_refPtr);                                                                                      
                cv::Mat rgbWarped;                                                                                                                          
                cv::warpPerspective(                                                                                                                        
                    rgbMat, rgbWarped, H_rgb, anchorSize,                                                                                                   
                    cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,                                                                                                
                    cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 255));                                                                                         
                out.baseRgb = matToRgbaImage(rgbWarped);                                                                                                    
                                                                                                                                                            
                // Only intersect RGB coverage if visible bands are sourced from RGB (e.g., Pi 5 Dual).                                                     
                if (gMat.empty() || rMat.empty()) {                                                                                                         
                    cv::Mat ones(rgbMat.size(), CV_8U, cv::Scalar(255));                                                                                    
                    cv::Mat rgbValid;                                                                                                                       
                    cv::warpPerspective(                                                                                                                    
                        ones, rgbValid, H_rgb, anchorSize,                                                                                                  
                        cv::INTER_NEAREST | cv::WARP_INVERSE_MAP,                                                                                           
                        cv::BORDER_CONSTANT, cv::Scalar(0));                                                                                                
                    cv::bitwise_and(validMask, rgbValid, validMask);                                                                                        
                }
            }
        }
  
        const size_t total = static_cast<size_t>(anchorW) * anchorH;
        for (size_t i = 0; i < total; ++i) {
            out.validMask[i] = (validMask.data[i] > 0) ? 1 : 0;
        }
  
        out.meanCorrelation = (ccCount > 0) ? (ccSum / ccCount) : 0.0;
        out.statusNote = "Radiometry + multi-band alignment active (mean ECC: " +
                         std::to_string(out.meanCorrelation).substr(0, 5) + ")";
        return out;
    #endif
    }
  
}  // namespace agrovis