#include "ViewportImageProvider.h"

#include <utility>

namespace agrovis::app {

ViewportImageProvider::ViewportImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {
    baseRgb_ = QImage(4, 4, QImage::Format_RGBA8888);
    baseRgb_.fill(QColor(18, 24, 22));
    indexOverlay_ = baseRgb_;
}

void ViewportImageProvider::setImages(QImage baseRgb, QImage indexOverlay) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!baseRgb.isNull()) {
        baseRgb_ = std::move(baseRgb);
    }
    if (!indexOverlay.isNull()) {
        indexOverlay_ = std::move(indexOverlay);
    }
}

QImage ViewportImageProvider::requestImage(
    const QString& id, QSize* size, const QSize& requestedSize) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Strip cache-busting query string (?rev=...) appended by QML Image.source.
    const qsizetype qPos = id.indexOf('?');
    const QString key = (qPos >= 0) ? id.left(qPos) : id;

    QImage img = (key == QLatin1String("overlay")) ? indexOverlay_ : baseRgb_;
    if (size) {
        *size = img.size();
    }
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0) {
        return img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return img;
}

}  // namespace agrovis::app
