#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <cstdint>
#include <memory>

#include "CaptureListModel.h"
#include "ViewportImageProvider.h"
#include "agrovis/types.hpp"

namespace agrovis::app {

/// Main QML-exposed workstation controller bridging UI actions and agrovis_core.
class AppController : public QObject {
    Q_OBJECT

    Q_PROPERTY(int activeTab READ activeTab WRITE setActiveTab NOTIFY uiStateChanged)
    Q_PROPERTY(int executionMode READ executionMode WRITE setExecutionMode NOTIFY uiStateChanged)
    Q_PROPERTY(QString saasEndpoint READ saasEndpoint WRITE setSaasEndpoint NOTIFY uiStateChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY uiStateChanged)
    Q_PROPERTY(double overlayOpacity READ overlayOpacity WRITE setOverlayOpacity NOTIFY uiStateChanged)
    Q_PROPERTY(double splitRatio READ splitRatio WRITE setSplitRatio NOTIFY uiStateChanged)

    Q_PROPERTY(bool usingSyntheticSample READ usingSyntheticSample NOTIFY dataChanged)
    Q_PROPERTY(QString loadedFolder READ loadedFolder NOTIFY dataChanged)
    Q_PROPERTY(int selectedCaptureIndex READ selectedCaptureIndex WRITE setSelectedCaptureIndex NOTIFY dataChanged)

    Q_PROPERTY(int sensorProfile READ sensorProfile WRITE setSensorProfile NOTIFY configChanged)
    Q_PROPERTY(int spectralIndex READ spectralIndex WRITE setSpectralIndex NOTIFY configChanged)
    Q_PROPERTY(int alignmentMode READ alignmentMode WRITE setAlignmentMode NOTIFY configChanged)
    Q_PROPERTY(int radiometryMode READ radiometryMode WRITE setRadiometryMode NOTIFY configChanged)
    Q_PROPERTY(int colormapKind READ colormapKind WRITE setColormapKind NOTIFY configChanged)
    Q_PROPERTY(int mlModelMode READ mlModelMode WRITE setMlModelMode NOTIFY configChanged)
    Q_PROPERTY(bool maskSoil READ maskSoil WRITE setMaskSoil NOTIFY configChanged)
    Q_PROPERTY(bool maskShadow READ maskShadow WRITE setMaskShadow NOTIFY configChanged)
    Q_PROPERTY(bool maskSkyGlare READ maskSkyGlare WRITE setMaskSkyGlare NOTIFY configChanged)
    Q_PROPERTY(double clampMin READ clampMin WRITE setClampMin NOTIFY configChanged)
    Q_PROPERTY(double clampMax READ clampMax WRITE setClampMax NOTIFY configChanged)
    Q_PROPERTY(QString customFormula READ customFormula WRITE setCustomFormula NOTIFY configChanged)

    Q_PROPERTY(QVariantList indexMetaList READ indexMetaList NOTIFY configChanged)
    Q_PROPERTY(QString activeIndexName READ activeIndexName NOTIFY configChanged)
    Q_PROPERTY(QString activeIndexFormula READ activeIndexFormula NOTIFY configChanged)
    Q_PROPERTY(bool activeIndexSupported READ activeIndexSupported NOTIFY outputUpdated)
    Q_PROPERTY(QString sensorCapabilitySummary READ sensorCapabilitySummary NOTIFY configChanged)

    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY outputUpdated)
    Q_PROPERTY(int imageWidth READ imageWidth NOTIFY outputUpdated)
    Q_PROPERTY(int imageHeight READ imageHeight NOTIFY outputUpdated)
    Q_PROPERTY(double soilPct READ soilPct NOTIFY outputUpdated)
    Q_PROPERTY(double stressedPct READ stressedPct NOTIFY outputUpdated)
    Q_PROPERTY(double moderatePct READ moderatePct NOTIFY outputUpdated)
    Q_PROPERTY(double healthyPct READ healthyPct NOTIFY outputUpdated)
    Q_PROPERTY(double canopyCoverPct READ canopyCoverPct NOTIFY outputUpdated)
    Q_PROPERTY(double meanIndex READ meanIndex NOTIFY outputUpdated)
    Q_PROPERTY(double stdDevIndex READ stdDevIndex NOTIFY outputUpdated)
    Q_PROPERTY(double processingMs READ processingMs NOTIFY outputUpdated)
    Q_PROPERTY(QString statusNote READ statusNote NOTIFY outputUpdated)
    Q_PROPERTY(QString activeCaptureSummary READ activeCaptureSummary NOTIFY outputUpdated)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)

public:
    AppController(
        CaptureListModel* captureModel,
        ViewportImageProvider* imageProvider,
        QObject* parent = nullptr);
    ~AppController() override;

    // UI navigation & view settings
    int activeTab() const { return activeTab_; }
    void setActiveTab(int tab);
    int executionMode() const { return executionMode_; }
    void setExecutionMode(int mode);
    QString saasEndpoint() const { return saasEndpoint_; }
    void setSaasEndpoint(const QString& url);
    int viewMode() const { return viewMode_; }
    void setViewMode(int mode);
    double overlayOpacity() const { return overlayOpacity_; }
    void setOverlayOpacity(double alpha);
    double splitRatio() const { return splitRatio_; }
    void setSplitRatio(double ratio);

    // Data state
    bool usingSyntheticSample() const { return usingSyntheticSample_; }
    QString loadedFolder() const { return loadedFolder_; }
    int selectedCaptureIndex() const { return selectedCaptureIndex_; }
    void setSelectedCaptureIndex(int idx);

    // Pipeline configuration
    int sensorProfile() const { return static_cast<int>(config_.sensorProfile); }
    void setSensorProfile(int v);
    int spectralIndex() const { return static_cast<int>(config_.index); }
    void setSpectralIndex(int v);
    int alignmentMode() const { return static_cast<int>(config_.alignment); }
    void setAlignmentMode(int v);
    int radiometryMode() const { return static_cast<int>(config_.radiometry); }
    void setRadiometryMode(int v);
    int colormapKind() const { return static_cast<int>(config_.colormap); }
    void setColormapKind(int v);
    int mlModelMode() const { return static_cast<int>(config_.mlModel); }
    void setMlModelMode(int v);
    bool maskSoil() const { return config_.maskSoil; }
    void setMaskSoil(bool v);
    bool maskShadow() const { return config_.maskShadow; }
    void setMaskShadow(bool v);
    bool maskSkyGlare() const { return config_.maskSkyGlare; }
    void setMaskSkyGlare(bool v);
    double clampMin() const { return config_.clampMin; }
    void setClampMin(double v);
    double clampMax() const { return config_.clampMax; }
    void setClampMax(double v);
    QString customFormula() const { return QString::fromStdString(config_.customFormula); }
    void setCustomFormula(const QString& expr);

    // Derived metadata & analytics
    QVariantList indexMetaList() const;
    QString activeIndexName() const;
    QString activeIndexFormula() const;
    bool activeIndexSupported() const { return currentOutput_.summary.indexSupported; }
    QString sensorCapabilitySummary() const;

    int imageRevision() const { return imageRevision_; }
    int imageWidth() const { return currentOutput_.width; }
    int imageHeight() const { return currentOutput_.height; }
    double soilPct() const { return currentOutput_.summary.soilPct; }
    double stressedPct() const { return currentOutput_.summary.stressedPct; }
    double moderatePct() const { return currentOutput_.summary.moderatePct; }
    double healthyPct() const { return currentOutput_.summary.healthyPct; }
    double canopyCoverPct() const { return currentOutput_.summary.canopyCoverPct; }
    double meanIndex() const { return currentOutput_.summary.meanIndex; }
    double stdDevIndex() const { return currentOutput_.summary.stdDevIndex; }
    double processingMs() const { return currentOutput_.summary.processingMs; }
    QString statusNote() const { return QString::fromStdString(currentOutput_.summary.statusNote); }
    QString activeCaptureSummary() const;
    bool isLoading() const { return isLoading_; }

    // Actions invoked from QML
    Q_INVOKABLE void loadSyntheticSample();
    Q_INVOKABLE void loadFolder(const QUrl& folderUrl);
    Q_INVOKABLE void resetIndexClampDefaults();
    Q_INVOKABLE QVariantMap probeAtPixel(int x, int y) const;
    Q_INVOKABLE bool exportOverlayPng(const QUrl& fileUrl) const;

signals:
    void uiStateChanged();
    void dataChanged();
    void configChanged();
    void outputUpdated();
    void loadingChanged();

private:
    void reprocess(bool showLoadingImmediately = false);

    CaptureListModel* captureModel_;
    ViewportImageProvider* imageProvider_;

    int activeTab_{0};
    int executionMode_{0};
    QString saasEndpoint_{QStringLiteral("http://127.0.0.1:8000")};
    int viewMode_{0};
    double overlayOpacity_{0.80};
    double splitRatio_{0.50};

    bool usingSyntheticSample_{true};
    QString loadedFolder_;
    int selectedCaptureIndex_{-1};

    agrovis::ProcessingConfig config_;
    agrovis::WorkbenchOutput currentOutput_;
    int imageRevision_{0};

    bool isLoading_{false};
    QTimer loadingDelayTimer_;
    std::shared_ptr<std::atomic<uint64_t>> activeJobSeq_{
        std::make_shared<std::atomic<uint64_t>>(0)};
};

}  // namespace agrovis::app
