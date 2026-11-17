#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QString>
#include <mutex>

namespace agrovis::app {

/// Serves dynamically rendered base RGB and spectral index overlays to QML Image items.
class ViewportImageProvider : public QQuickImageProvider {
public:
    ViewportImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    void setImages(QImage baseRgb, QImage indexOverlay);

private:
    std::mutex mutex_;
    QImage baseRgb_;
    QImage indexOverlay_;
};

}  // namespace agrovis::app
