#include "CaptureListModel.h"

#include "agrovis/engine.hpp"

namespace agrovis::app {
namespace {

QString formatByteSize(uint64_t bytes) {
    if (bytes == 0) return QStringLiteral("--");
    if (bytes < 1024ULL * 1024ULL) {
        return QString::number(static_cast<double>(bytes) / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
}

QString buildBandSummary(const agrovis::CaptureGroup& g) {
    QStringList tags;
    if (g.hasRgb()) tags << QStringLiteral("RGB");
    if (g.hasBlue()) tags << QStringLiteral("B");
    if (g.hasGreen()) tags << QStringLiteral("G");
    if (g.hasRed()) tags << QStringLiteral("R");
    if (g.hasRedEdge()) tags << QStringLiteral("RE");
    if (g.hasNir()) tags << QStringLiteral("NIR");
    return tags.isEmpty() ? QStringLiteral("None") : tags.join(QStringLiteral(" · "));
}

}  // namespace

CaptureListModel::CaptureListModel(QObject* parent) : QAbstractListModel(parent) {}

int CaptureListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(captures_.size());
}

QVariant CaptureListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(captures_.size())) {
        return {};
    }
    const auto& c = captures_[static_cast<size_t>(index.row())];
    switch (role) {
        case GroupIdRole: return QString::fromStdString(c.groupId);
        case TimestampRole: return QString::fromStdString(c.timestampLabel);
        case SensorNameRole: return QString::fromStdString(SpectralEngine::sensorDisplayName(c.sensorProfile));
        case BandSummaryRole: return buildBandSummary(c);
        case SizeLabelRole: return formatByteSize(c.totalBytes);
        case BandCountRole: return c.spectralBandCount() + (c.hasRgb() ? 1 : 0);
        case HasRgbRole: return c.hasRgb();
        case HasGreenRole: return c.hasGreen();
        case HasRedRole: return c.hasRed();
        case HasRedEdgeRole: return c.hasRedEdge();
        case HasNirRole: return c.hasNir();
    }
    return {};
}

QHash<int, QByteArray> CaptureListModel::roleNames() const {
    return {
        {GroupIdRole, "groupId"},
        {TimestampRole, "timestamp"},
        {SensorNameRole, "sensorName"},
        {BandSummaryRole, "bandSummary"},
        {SizeLabelRole, "sizeLabel"},
        {BandCountRole, "bandCount"},
        {HasRgbRole, "hasRgb"},
        {HasGreenRole, "hasGreen"},
        {HasRedRole, "hasRed"},
        {HasRedEdgeRole, "hasRedEdge"},
        {HasNirRole, "hasNir"},
    };
}

void CaptureListModel::setCaptures(std::vector<agrovis::CaptureGroup> captures) {
    beginResetModel();
    captures_ = std::move(captures);
    endResetModel();
}

const agrovis::CaptureGroup* CaptureListModel::captureAt(int row) const {
    if (row < 0 || row >= static_cast<int>(captures_.size())) return nullptr;
    return &captures_[static_cast<size_t>(row)];
}

}  // namespace agrovis::app
