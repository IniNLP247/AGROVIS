#include "AppController.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <algorithm>
#include <cmath>

#include "agrovis/capture_scanner.hpp"
#include "agrovis/engine.hpp"

namespace agrovis::app {
namespace {

QImage toQImage(const agrovis::RgbaImage& src) {
    if (src.empty()) return {};
    QImage view(src.pixels.data(), src.width, src.height, src.width * 4, QImage::Format_RGBA8888);
    return view.copy();
}

}  // namespace

AppController::AppController(
    CaptureListModel* captureModel,
    ViewportImageProvider* imageProvider,
    QObject* parent)
    : QObject(parent), captureModel_(captureModel), imageProvider_(imageProvider) {
    loadingDelayTimer_.setSingleShot(true);
    connect(&loadingDelayTimer_, &QTimer::timeout, this, [this]() {
        if (!isLoading_) {
            isLoading_ = true;
            emit loadingChanged();
        }
    });

    // Populate initial synthetic plot synchronously so startup renders immediately.
    currentOutput_ = agrovis::SpectralEngine::processSyntheticSample(config_);
    imageProvider_->setImages(
        toQImage(currentOutput_.baseRgb),
        toQImage(currentOutput_.indexOverlay));
    ++imageRevision_;
}

AppController::~AppController() {
    activeJobSeq_->fetch_add(1);
    QThreadPool::globalInstance()->waitForDone();
}

void AppController::setActiveTab(int tab) {
    if (activeTab_ == tab) return;
    activeTab_ = tab;
    emit uiStateChanged();
}

void AppController::setExecutionMode(int mode) {
    if (executionMode_ == mode) return;
    executionMode_ = mode;
    emit uiStateChanged();
}

void AppController::setSaasEndpoint(const QString& url) {
    if (saasEndpoint_ == url) return;
    saasEndpoint_ = url.trimmed();
    emit uiStateChanged();
}

void AppController::setViewMode(int mode) {
    if (viewMode_ == mode) return;
    viewMode_ = mode;
    emit uiStateChanged();
}

void AppController::setOverlayOpacity(double alpha) {
    const double clamped = std::clamp(alpha, 0.0, 1.0);
    if (std::fabs(overlayOpacity_ - clamped) < 1e-4) return;
    overlayOpacity_ = clamped;
    emit uiStateChanged();
}

void AppController::setSplitRatio(double ratio) {
    const double clamped = std::clamp(ratio, 0.05, 0.95);
    if (std::fabs(splitRatio_ - clamped) < 1e-4) return;
    splitRatio_ = clamped;
    emit uiStateChanged();
}

void AppController::setSelectedCaptureIndex(int idx) {
    if (idx < 0 || idx >= captureModel_->rowCount()) return;
    if (!usingSyntheticSample_ && selectedCaptureIndex_ == idx) return;

    usingSyntheticSample_ = false;
    selectedCaptureIndex_ = idx;
    if (const auto* cap = captureModel_->captureAt(idx)) {
        config_.sensorProfile = cap->sensorProfile;
    }
    emit dataChanged();
    emit configChanged();
    reprocess(true);
}

void AppController::setSensorProfile(int v) {
    const auto next = static_cast<agrovis::SensorProfile>(std::clamp(v, 0, 2));
    if (config_.sensorProfile == next) return;
    config_.sensorProfile = next;
    emit configChanged();
    reprocess();
}

void AppController::setSpectralIndex(int v) {
    const auto& specs = agrovis::SpectralEngine::allIndexSpecs();
    if (v < 0 || v >= static_cast<int>(specs.size())) return;
    const auto next = specs[static_cast<size_t>(v)].id;
    if (config_.index == next) return;
    config_.index = next;
    const auto& spec = agrovis::SpectralEngine::specFor(next);
    config_.clampMin = spec.defaultMin;
    config_.clampMax = spec.defaultMax;
    emit configChanged();
    reprocess();
}

void AppController::setAlignmentMode(int v) {
    const auto next = static_cast<agrovis::AlignmentMode>(std::clamp(v, 0, 2));
    if (config_.alignment == next) return;
    config_.alignment = next;
    emit configChanged();
    reprocess();
}

void AppController::setRadiometryMode(int v) {
    const auto next = static_cast<agrovis::RadiometryMode>(std::clamp(v, 0, 2));
    if (config_.radiometry == next) return;
    config_.radiometry = next;
    emit configChanged();
    reprocess();
}

void AppController::setColormapKind(int v) {
    const auto next = static_cast<agrovis::ColormapKind>(std::clamp(v, 0, 3));
    if (config_.colormap == next) return;
    config_.colormap = next;
    emit configChanged();
    reprocess();
}

void AppController::setMlModelMode(int v) {
    const auto next = static_cast<agrovis::MlModelMode>(std::clamp(v, 0, 3));
    if (config_.mlModel == next) return;
    config_.mlModel = next;
    emit configChanged();
    reprocess();
}

void AppController::setMaskSoil(bool v) {
    if (config_.maskSoil == v) return;
    config_.maskSoil = v;
    emit configChanged();
    reprocess();
}

void AppController::setMaskShadow(bool v) {
    if (config_.maskShadow == v) return;
    config_.maskShadow = v;
    emit configChanged();
    reprocess();
}

void AppController::setMaskSkyGlare(bool v) {
    if (config_.maskSkyGlare == v) return;
    config_.maskSkyGlare = v;
    emit configChanged();
    reprocess();
}

void AppController::setClampMin(double v) {
    const float next = static_cast<float>(v);
    if (std::fabs(config_.clampMin - next) < 1e-4f) return;
    config_.clampMin = next;
    emit configChanged();
    reprocess();
}

void AppController::setClampMax(double v) {
    const float next = static_cast<float>(v);
    if (std::fabs(config_.clampMax - next) < 1e-4f) return;
    config_.clampMax = next;
    emit configChanged();
    reprocess();
}

void AppController::setCustomFormula(const QString& expr) {
    const std::string s = expr.toStdString();
    if (config_.customFormula == s) return;
    config_.customFormula = s;
    emit configChanged();
    if (config_.index == agrovis::SpectralIndex::Custom) {
        reprocess();
    }
}

QVariantList AppController::indexMetaList() const {
    QVariantList list;
    const auto& specs = agrovis::SpectralEngine::allIndexSpecs();
    for (int i = 0; i < static_cast<int>(specs.size()); ++i) {
        const auto& s = specs[static_cast<size_t>(i)];
        if (s.id == agrovis::SpectralIndex::Custom) continue;
        QVariantMap item;
        item[QStringLiteral("indexPos")] = i;
        item[QStringLiteral("shortName")] = QString::fromStdString(s.shortName);
        item[QStringLiteral("displayName")] = QString::fromStdString(s.displayName);
        item[QStringLiteral("formula")] = QString::fromStdString(s.formula);
        item[QStringLiteral("requiresRedEdge")] = s.requiresRedEdge;
        item[QStringLiteral("supported")] = true;
        list.append(item);
    }
    return list;
}

QString AppController::activeIndexName() const {
    return QString::fromStdString(agrovis::SpectralEngine::specFor(config_.index).shortName);
}

QString AppController::activeIndexFormula() const {
    if (config_.index == agrovis::SpectralIndex::Custom) {
        return QString::fromStdString(config_.customFormula);
    }
    return QString::fromStdString(agrovis::SpectralEngine::specFor(config_.index).formula);
}

QString AppController::sensorCapabilitySummary() const {
    switch (config_.sensorProfile) {
        case agrovis::SensorProfile::DjiMavic3M:
            return QStringLiteral("Bands: RGB + G(560) + R(650) + RE(730) + NIR(860)");
        case agrovis::SensorProfile::Pi5DualRgbNir850:
            return QStringLiteral("Bands: RGB + 850nm NoIR (RedEdge indices disabled)");
        case agrovis::SensorProfile::GenericMultiTiff:
            return QStringLiteral("Bands: Generic Multi-Band TIFF Stack");
    }
    return {};
}

QString AppController::activeCaptureSummary() const {
    if (usingSyntheticSample_) {
        return QStringLiteral("Synthetic Crop Plot · 720×480 · 5 Spectral Bands");
    }
    if (const auto* cap = captureModel_->captureAt(selectedCaptureIndex_)) {
        return QString::fromStdString(cap->groupId) + QStringLiteral(" · ") +
               QString::number(currentOutput_.width) + QStringLiteral("×") +
               QString::number(currentOutput_.height);
    }
    return QStringLiteral("No capture selected");
}

void AppController::loadSyntheticSample() {
    usingSyntheticSample_ = true;
    selectedCaptureIndex_ = -1;
    emit dataChanged();
    reprocess();
}

void AppController::loadFolder(const QUrl& folderUrl) {
    const QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    if (path.isEmpty()) return;

    auto captures = agrovis::CaptureScanner::scanDirectory(path.toStdString());
    loadedFolder_ = QDir::toNativeSeparators(path);
    captureModel_->setCaptures(std::move(captures));

    if (captureModel_->rowCount() > 0) {
        usingSyntheticSample_ = false;
        selectedCaptureIndex_ = 0;
        if (const auto* cap = captureModel_->captureAt(0)) {
            config_.sensorProfile = cap->sensorProfile;
        }
        emit dataChanged();
        emit configChanged();
        reprocess(true);
    } else {
        usingSyntheticSample_ = true;
        selectedCaptureIndex_ = -1;
        emit dataChanged();
        reprocess();
    }
}

void AppController::resetIndexClampDefaults() {
    const auto& spec = agrovis::SpectralEngine::specFor(config_.index);
    config_.clampMin = spec.defaultMin;
    config_.clampMax = spec.defaultMax;
    emit configChanged();
    reprocess();
}

QVariantMap AppController::probeAtPixel(int x, int y) const {
    const auto p = agrovis::SpectralEngine::probePixel(currentOutput_, x, y);
    QVariantMap out;
    out[QStringLiteral("valid")] = p.valid;
    out[QStringLiteral("masked")] = p.masked;
    out[QStringLiteral("x")] = p.x;
    out[QStringLiteral("y")] = p.y;
    out[QStringLiteral("blue")] = p.blue;
    out[QStringLiteral("green")] = p.green;
    out[QStringLiteral("red")] = p.red;
    out[QStringLiteral("redEdge")] = p.redEdge;
    out[QStringLiteral("nir")] = p.nir;
    out[QStringLiteral("indexValue")] = p.indexValue;
    return out;
}

bool AppController::exportOverlayPng(const QUrl& fileUrl) const {
    QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }
    const QImage overlay = toQImage(currentOutput_.indexOverlay);
    if (overlay.isNull()) return false;
    return overlay.save(path, "PNG");
}

void AppController::reprocess(bool showLoadingImmediately) {
    const uint64_t seq = activeJobSeq_->fetch_add(1) + 1;
    const auto seqRef = activeJobSeq_;

    if (showLoadingImmediately) {
        loadingDelayTimer_.stop();
        if (!isLoading_) {
            isLoading_ = true;
            emit loadingChanged();
        }
    } else if (!isLoading_) {
        // Avoid flickering "Loading..." on <75 ms cache-hit index switches.
        loadingDelayTimer_.start(75);
    }

    const bool useSynthetic = usingSyntheticSample_ || selectedCaptureIndex_ < 0 ||
                              selectedCaptureIndex_ >= captureModel_->rowCount();
    agrovis::CaptureGroup capCopy;
    if (!useSynthetic) {
        if (const auto* cap = captureModel_->captureAt(selectedCaptureIndex_)) {
            capCopy = *cap;
        }
    }
    const agrovis::ProcessingConfig cfgCopy = config_;
    QPointer<AppController> self(this);

    QThreadPool::globalInstance()->start(
        [self, seq, seqRef, useSynthetic, capCopy = std::move(capCopy), cfgCopy]() {
            if (seq != seqRef->load()) {
                return;
            }

            agrovis::WorkbenchOutput out = useSynthetic
                ? agrovis::SpectralEngine::processSyntheticSample(cfgCopy)
                : agrovis::SpectralEngine::processCaptureGroup(capCopy, cfgCopy);

            if (seq != seqRef->load()) {
                return;
            }

            QImage baseImg = toQImage(out.baseRgb);
            QImage overlayImg = toQImage(out.indexOverlay);

            if (self) {
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, seq, out = std::move(out), baseImg = std::move(baseImg),
                     overlayImg = std::move(overlayImg)]() mutable {
                        if (!self || seq != self->activeJobSeq_->load()) {
                            return;
                        }
                        self->loadingDelayTimer_.stop();
                        self->currentOutput_ = std::move(out);
                        self->imageProvider_->setImages(std::move(baseImg), std::move(overlayImg));
                        ++self->imageRevision_;
                        if (self->isLoading_) {
                            self->isLoading_ = false;
                            emit self->loadingChanged();
                        }
                        emit self->outputUpdated();
                    },
                    Qt::QueuedConnection);
            }
        });
}

}  // namespace agrovis::app
