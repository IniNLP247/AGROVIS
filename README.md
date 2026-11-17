# AGROVIS

Cross-platform multispectral crop scouting & precision agriculture analysis suite (Ground Spot-Check -> UAV Field Survey -> SaaS).

## Architecture

* **`core/` (`agrovis_core`)** — Pure C++20 spectral engine (no Qt dependency): sensor profiles, multi-band capture grouping, radiometric calibration, close-range band registration, canopy/soil masking, spectral indices (`NDVI`, `NDRE`, `GNDVI`, `SAVI`, `OSAVI`, `MSAVI2`, `EVI`, `CIgreen`, `CIrededge`, `LCI`, `VARI`, custom math), and ONNX inference hooks.
* **`app/` (`agrovis_app`)** — Qt 6 (C++20 + QML) desktop workstation for Ubuntu and Windows (and mobile expansion), supporting local offline processing or remote SaaS job dispatch.
* **`api/`** — FastAPI SaaS control plane & worker orchestration (upcoming).
* **`ml/`** — PyTorch training & ONNX export pipeline for canopy segmentation, stress detection, and anomaly mapping (upcoming).
* **`edge/`** — Raspberry Pi 5 dual-CSI synchronized raw capture daemon (Pi AI RGB + Pi Cam 3 NoIR 850 nm) (upcoming).

## Building Locally (Ubuntu 24.04)

### Option A: Using System Qt 6 (6.4+)
```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-dialogs qml6-module-qtquick-window \
  libopencv-dev libtiff-dev libgdal-dev

cmake --preset linux-system
cmake --build --preset linux-system
ctest --preset linux-system
./build/linux-system/app/agrovis_app
```

### Option B: Using Qt 6.11.1 in `~/Qt/6.11.1`
```bash
cmake --preset linux-qt611
cmake --build --preset linux-qt611
ctest --preset linux-qt611
LD_LIBRARY_PATH=/home/inigo/Qt/6.11.1/gcc_64/lib ./build/linux-qt611/app/agrovis_app
```

## Running in Docker (Portable Across Laptops)

```bash
# Allow local X11 connections for the GUI container and build+launch
./scripts/run_docker.sh

# Or run tests headlessly inside the container
docker compose run --rm agrovis-dev bash -c \
  "cmake --preset linux-docker && cmake --build --preset linux-docker && ctest --preset linux-docker --output-on-failure"
```

For laptops with an NVIDIA GPU + `nvidia-container-toolkit`:
```bash
docker compose -f docker-compose.yml -f docker/docker-compose.gpu.yml up --build
```

## Windows Build (MSVC + Qt 6)
Configure with `cmake --preset windows-msvc -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"` and run `cmake --build --preset windows-msvc --config Release`.
