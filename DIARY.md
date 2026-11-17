# AGROVIS Development Diary

**Last updated:** 2026-11-17  
**Repository:** `git@github.com:IniNLP247/AGROVIS.git`  
**Commits so far:**
* `ec6060f` — Initial AGROVIS workbench: Qt6 UI, XMP radiometry, ECC alignment, Docker
* `630445d` — Added Japanese as default language and switch to english capabilities; Also added an index for method information

---

## 1. Project Overview

**AGROVIS** is a cross-platform multispectral crop scouting workstation designed to start simple for **ground spot-checks by farmers** and scale later to **Raspberry Pi 5 dual-camera capture rigs**, **UAV field surveys**, **local ML crop diagnostics**, and **SaaS/API workflows**.

### Architecture
* **`core/` (`agrovis_core`)** — Pure C++20 static library with **no Qt dependency** (portable across desktop, Docker, edge Pi 5, and future `pybind11` Python bindings).
* **`app/` (`agrovis_app`)** — Qt 6 + QML desktop workstation (`C++20`, compatible with both **Qt 6.11.1 host** and **Qt 6.4.2 Docker/Ubuntu 24.04**).
* **`tests/`** — C++ unit tests (`tests/core_tests.cpp`) via `ctest`.
* **`docker/` & `scripts/`** — Reproducible Ubuntu 24.04 build/run container (`fonts-noto-cjk` included for Japanese rendering) and host launcher scripts.
* **`ml/`, `edge/`, `api/`** — Scaffolded directories for upcoming ML diagnostics, Raspberry Pi 5 capture daemon, and FastAPI SaaS layer.

---

## 2. What Has Been Implemented (Topics, Files & Key Functions)

### A. Capture Discovery & Sensor Profiles
* **Files:** [`core/include/agrovis/types.hpp`](core/include/agrovis/types.hpp), [`core/include/agrovis/capture_scanner.hpp`](core/include/agrovis/capture_scanner.hpp), [`core/src/capture_scanner.cpp`](core/src/capture_scanner.cpp)
* **Key functions & types:**
  * `SensorProfile`: `DjiMavic3M`, `Pi5DualRgbNir850`, `GenericMultiTiff`.
  * `CaptureScanner::scanDirectory(directoryPath)`: Scans root and immediate non-excluded subdirectories (skipping `Masks/`, `Overlay/`, `Metadata/`, etc.) for `.JPG/.JPEG/.PNG/.TIF/.TIFF/.DNG` files and groups matching stems:
    * `_D`, `_MS_G`, `_MS_R`, `_MS_RE`, `_MS_NIR` → `SensorProfile::DjiMavic3M` (with timestamp extraction for `DJI_YYYYMMDDHHMMSS_NNNN`).
    * Structured dataset stems (`RGB/<stem>.JPG` + `Multispectral/<stem>_{G,R,RE,NIR}.TIF`, including `DJI_DateTime_YYYY_MM_DD_HH_MM_...` timestamps) → `SensorProfile::DjiMavic3M`.
    * `_RGB` + `_NIR` or `_850` → `SensorProfile::Pi5DualRgbNir850`.
    * `_MS_B` or unmatched stems → `SensorProfile::GenericMultiTiff`.

### B. Radiometric Calibration (XMP Metadata + Safe Defaults)
* **Files:** [`core/include/agrovis/radiometry.hpp`](core/include/agrovis/radiometry.hpp), [`core/src/radiometry.cpp`](core/src/radiometry.cpp)
* **Key functions & types:**
  * `BandRadiometryMeta`: Holds dark level (`blackLevel`), analog gain (`sensorGain`), band adjustment (`gainAdjustment`), exposure (`exposureUs`), downwelling sunlight (`irradiance`), 6-term radial vignette polynomial (`vignetteK`, `vignetteCx/Cy`), optical center, distortion, and `dewarpHMatrix`.
  * `parseBandXmpFromFile(filePath)` / `parseBandXmpBuffer(xmpXml)`: Zero-regex `<charconv>` (`std::from_chars`) parser checking the first `128 KB` and last `256 KB` for `<x:xmpmeta>...</x:xmpmeta>` tags (`drone-dji:*` and `Camera:*`).
  * `computeRadianceScale(meta, maxDn, dlsScale = 25.0f)`: Computes pixel multiplier handling dark current, `1e6 / (gain * exposureUs)`, `gainAdjustment`, and `Irradiance` (`dlsScale = 25.0f`, or `0.01f` nominal daylight fallback if exposure exists without DLS).
  * `calibrateUint16Band(...)` / `calibrateUint8Band(...)` → `calibrateBandImpl(...)`: Subtracts black pedestal, applies Horner polynomial vignetting correction, and scales pixels into a float reflectance `FloatBand`.
  * **Default fallback:** If XMP tags are missing (e.g., standard PNG/JPEG/TIFF or uncalibrated DIY images), `BandRadiometryMeta` stays at `blackLevel = 0`, `sensorGain = 1`, `gainAdjustment = 1`, no vignette correction, and scales purely by `1 / 65535.0f` (`CV_16U`) or `1 / 255.0f` (`CV_8U`).

### C. Multi-Band Geometric Registration (OpenCV)
* **Files:** [`core/include/agrovis/registration.hpp`](core/include/agrovis/registration.hpp), [`core/src/registration.cpp`](core/src/registration.cpp)
* **Key functions & types:**
  * `alignedCaptureCube(input, enableAligned)`:
    * Selects anchor frame by priority: `NIR -> RedEdge -> Green -> Red -> Blue -> RGB`.
    * `estimatePairwiseTransform(refFull, movFull, motionType, useLogEdge)`: Downscales to 640 px width, runs log-Sobel FFT `cv::phaseCorrelate` for coarse shift initialization, then refines with `cv::findTransformECC` (with sanity bounds on determinant `(0.5, 2.0)`, correlation `> 0.10`, and translation `< 30%`, falling back to `phaseCorrelate` if `>= 0.12` or identity).
    * Chains MS transforms rigidly (`MOTION_EUCLIDEAN`) to avoid affine scaling drift on repetitive crop canopies: `RE -> NIR`, `G -> RE` (log-edge cross-spectral), `R -> G`, `B -> G`.
    * `buildInitialRgbToAnchorHomography(...)`: Uses Mavic `DewarpHMatrix` if present; otherwise falls back to centered aspect-preserving scaling (`Generic / DIY dual-camera fallback`) and refines `RGB -> G` (or `R`/`RE`/`NIR`) with `MOTION_AFFINE`.
    * Composes $3 \times 3$ matrices so every band is warped **at most once** via `cv::warpPerspective(..., WARP_INVERSE_MAP)`, while intersecting non-border coverage into `validMask`.

### D. Spectral Index Engine & Zonal Health Analysis
* **Files:** [`core/include/agrovis/engine.hpp`](core/include/agrovis/engine.hpp), [`core/src/engine.cpp`](core/src/engine.cpp)
* **Key functions & types:**
  * `SpectralEngine::allIndexSpecs()` / `specFor(idx)`: Defines 12 farmer-facing modes (`RGB`, `NDVI`, `NDRE`, `GNDVI`, `SAVI`, `OSAVI`, `MSAVI2`, `EVI`, `CIgreen`, `CIrededge`, `LCI`, `VARI`) plus internal `Custom` formula parser (`FormulaParser`, `evaluateCustomFormula`).
  * `SpectralEngine::processSyntheticSample(config)`: Generates a 720×480 synthetic plot with crop rows, localized stress pocket, shadow band, and a 50% reflectance gray card.
  * `SpectralEngine::processCaptureGroup(group, config)`:
    * Uses mutex-guarded `CapturePreviewCache` (`key = groupId + paths + alignmentFlag`) so switching indices or colormaps on the same capture is instantaneous.
    * Downscales previews to max `1280 px` width, fills missing visible bands (`blue`, `green`, `red`) from `baseRgb`, and leaves missing uncaptured bands (such as `redEdge` on a 4-band camera) as `0.0f`.
  * `SpectralEngine::renderIndexOverlay(...)` / `computeHealthSummaryAndOverlay(...)`: Applies border `validMask` plus optional background filters (`maskSoil`: `NDVI < 0.18` or `VARI < 0.02` when using `VARI`/RGB-only, `maskWater`: open water `NDWI(G, NIR) > 0` w/ `!looksLeafyGreen` guard + shallow sky-reflecting paddy water `B/R > 0.83`, `G-B < 0.095`, `NIR < 0.30`, `maskShadow`: brightness `< 0.045`, `maskSkyGlare`: brightness `> 0.72` & `NDVI < 0.2`), classifies health zones (`stressedPct` `< 0.42`, `moderatePct` `< 0.68`, `healthyPct` `>= 0.68` along each index's fixed Standard benchmark range `[spec.defaultMin, spec.defaultMax]`) so visual contrast boost never distorts health percentages, computes `soilPct`, `canopyCoverPct`, `meanIndex`, `stdDevIndex`, and renders `RdYlGn` / `Viridis` / `Turbo` / `Grayscale` colormaps (`mapColor`).
  * `SpectralEngine::probePixel(output, x, y)`: Returns per-band reflectance and index value under the cursor.

### E. Qt 6 / QML Workstation UI (`app/`)
* **C++ Layer:** [`app/src/main.cpp`](app/src/main.cpp), [`app/src/AppController.h`](app/src/AppController.h), [`app/src/AppController.cpp`](app/src/AppController.cpp), [`app/src/CaptureListModel.h`](app/src/CaptureListModel.h), [`app/src/CaptureListModel.cpp`](app/src/CaptureListModel.cpp), [`app/src/ViewportImageProvider.h`](app/src/ViewportImageProvider.h), [`app/src/ViewportImageProvider.cpp`](app/src/ViewportImageProvider.cpp)
  * `AppController::reprocess(showLoadingImmediately)`: Runs `SpectralEngine` asynchronously on `QThreadPool::globalInstance()` with atomic `activeJobSeq_` cancellation for rapid clicks, `autoRange_` `[p2, p98]` percentile fitting when enabled, and a `75 ms` delayed `isLoading` overlay so fast cache hits don't flicker.
  * `AppController::language()` / `setLanguage()` / `tr()`: Defaults to plain farmer-friendly **Japanese (`"ja"`)** and switches dynamically between **`日本語` and `EN`** across headers, viewport labels, sidebar, health KPIs, and index names (`localizedInfoFor`).
  * `AppController::indexMetaList()`: Exposes the ordered 12-item catalog (excluding `Custom`) with plain names, use-case explanations, and color meanings.
  * `AppController::probeAtPixel(x, y)` and `exportOverlayPng(fileUrl)`.
* **QML Views:**
  * [`app/qml/Main.qml`](app/qml/Main.qml): Dark-slate + emerald theme, top navigation (`解析ビュー / Analysis` | `指標ガイド / Spectral Index Information`), `[ 日本語 | EN ]` toggle pill, Open Folder, Export PNG.
  * [`app/qml/CaptureSidebar.qml`](app/qml/CaptureSidebar.qml): Capture queue with sample plot shortcut, band badges, and folder selector.
  * [`app/qml/SpectralViewport.qml`](app/qml/SpectralViewport.qml): 4 comparison views (`Split Curtain`, `Side-by-Side`, `Overlay` with opacity slider, `Index Only`), palette selector, zoom/pan, centered `Weak ↔ Good` color scale with `[Standard | Boost Contrast]` (`標準表示` / `差を強調`) buttons, and green `"読み込み中..." / "Loading..."` overlay.
  * [`app/qml/AnalysisInspector.qml`](app/qml/AnalysisInspector.qml): Simplified farmer inspector with locked **3 columns × 4 rows** spectral index grid, 2×2 background filters (`Mask Soil`, `Water`, `Shadow`, `Glare`), health breakdown bar & KPIs (`CANOPY`, `MEAN`, `STD DEV`), and `ROUGH ISSUE ESTIMATE` (`推定される症状`) currently set to `開発中 (WIP)` / `WIP`.
  * [`app/qml/IndexGuideView.qml`](app/qml/IndexGuideView.qml): Full explanation page ("Spectral Index Information") ordered identically to the 3×4 grid with full names, "When to use", "What colors mean", color & scale legend, and one-click `"この方法で表示 / Use This Method"`.

### F. Verification Status
* Verified locally against Sri Lanka Mavic 3M (`samples_raw/`, gitignored) and Vietnam Mekong Delta `WeedyRice-RGBMS-DB` (`734/734` paired 5-band captures).
* C++ unit tests in [`tests/core_tests.cpp`](tests/core_tests.cpp) (`agrovis_core_tests`) cover capture grouping (flat + subfolder layouts), sensor gating, formula parsing, synthetic probe + gray card check, XMP parsing + calibration, synthetic multi-band shift registration, deep/shallow paddy water masking, and index/scale-synchronized health summaries.
* Configured for Host Qt 6.11.1 (`linux-qt611`), Docker Ubuntu 24.04 + Qt 6.4.2 (`linux-docker`), System Qt 6 (`linux-system`), and Windows (`windows-msvc`).

---

## 3. Technical Notes & Design Decisions to Remember

1. **Qt 6.4 vs 6.11 Portability:** Keep QML compatible with Qt 6.4.2 (Docker/Ubuntu apt): avoid Qt 6.5+ QML features like `pragma ComponentBehavior: Bound` or `loadFromModule`; keep `RESOURCE_PREFIX "/qt/qml"` and `QQuickStyle::setStyle("Basic")`. On host Ubuntu, run via [`scripts/run_host.sh`](scripts/run_host.sh) so `LD_LIBRARY_PATH` prioritizes Qt 6.11.1 over system Qt libraries.
2. **Dataset Hygiene:** Keep raw imagery, `.zip` archives, `.onnx/.pt` weights, `.vscode/`, and `build/` untracked via [`.gitignore`](.gitignore).
3. **Why Defaults Alone Aren't Enough for DIY Dual Cameras:**
   * Missing XMP falls back to DN scaling (`1/65535` or `1/255`) and centered aspect-fit `ECC` alignment.
   * Two independent cameras (`Pi AI RGB` + `Pi Cam 3 NoIR 850 nm`) differ in quantum efficiency, lens transmission, solar spectral energy (650 nm vs 850 nm), and ISP gamma/WB. Even on a cloudless sunny day, locking manual exposure/gain/WB/focus and applying **one empirical gray-card calibration ratio per band** (`gainAdjustment`) is needed so neutral gray yields equal B/G/R/NIR reflectance (`NDVI = 0`).
   * Keep an eye on 10/12-bit RAW scaled vs unscaled into `uint16` TIFFs (`calibrateUint16Band` assumes full `65535.0f` range).

---

## 4. Next Steps Roadmap

### Step 1: Folder Triage ("Worst Spots First")
* Scan large folders of captures in background worker threads (`QThreadPool`) with progress reporting.
* Rank and filter captures by `stressedPct`, low canopy `meanIndex`, or stress hotspots so farmers immediately see the worst locations first, plus exportable summary table/report.

### Step 2: Local ML Rough Issue Estimator + Reporting (`ml/`, `core/`, `app/`)
* Build training/evaluation pipeline under `ml/` using multi-date Sri Lanka Mavic 3M paddy captures.
* Export a lightweight local model (e.g., ONNX) or rule+ML classifier callable from C++ to replace `"開発中 (WIP)" / "WIP"` in `ROUGH ISSUE ESTIMATE` (`推定される症状`) in [`AppController.cpp`](app/src/AppController.cpp) and generate farmer-readable scouting reports.

### Step 3: Raspberry Pi 5 Dual-Camera Capture & Calibration (`edge/`, `core/`) — Target: ~Dec 5, 2026
* Synchronized `libcamera` capture script/daemon for **Pi AI Camera (RGB)** + **Pi Camera Module 3 NoIR + 850 nm filter**:
  * Lock `AfMode=Manual`, fixed shutter (`ExposureTime`), fixed `AnalogueGain`, fixed daylight `ColourGains`, and linear output (16-bit TIFF / DNG unpack).
* Add a Pi 5 calibration profile in `agrovis_core`: black level, bit-depth `maxDn`, empirical gray-card `gainAdjustment` multipliers (sunny preset + optional field gray-card quick recalibration), and saved dual-camera alignment homography to eliminate close-range FOV/parallax issues.

### Step 4: Robustness — Auto Gray-Card Calibration & Multi-Sensor Adapters (`core/`, `app/`)
* **One-Shot Gray Reference Card Calibration (`RadiometryMode::ReferencePanel`):**
  * Allow the farmer to take one photo of a known reflectance gray card (e.g., 18% or 50% card — either as a dedicated reference capture before scanning a field, or in-frame with an AprilTag/printed border for automatic detection) followed by field captures under the same lighting/exposure.
  * Automatically sample the card region across all bands (`B`, `G`, `R`, `RE`, `NIR`), derive exact per-band scaling multipliers (`gainAdjustment = rho_card / median_card`), and apply them across the whole session — compensating for any camera brand, filter transmission, or weather/sunlight color temperature automatically.
* **Additional Commercial Camera Adapters:**
  * Extend [`capture_scanner.cpp`](core/src/capture_scanner.cpp) and [`radiometry.cpp`](core/src/radiometry.cpp) with pluggable metadata/filename adapters for other multispectral sensors (e.g., MicaSense RedEdge/Altum `_1.._5.tif`, Parrot Sequoia, Sentera), mapping each sensor into standard `[0, 1]` reflectance before entering `SpectralEngine`.

### Step 5: Cloud SaaS & UAV Survey Expansion (`api/`)
* Implement FastAPI + PostGIS + `pybind11` bridge wrapping `agrovis_core` for batch UAV mosaic/orthomosaic jobs and remote dashboard reporting.
