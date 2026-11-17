#pragma once

#include <QAbstractListModel>
#include <vector>

#include "agrovis/types.hpp"

namespace agrovis::app {

/// Exposes discovered multi-band CaptureGroups to the QML capture queue.
class CaptureListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        GroupIdRole = Qt::UserRole + 1,
        TimestampRole,
        SensorNameRole,
        BandSummaryRole,
        SizeLabelRole,
        BandCountRole,
        HasRgbRole,
        HasGreenRole,
        HasRedRole,
        HasRedEdgeRole,
        HasNirRole
    };

    explicit CaptureListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCaptures(std::vector<agrovis::CaptureGroup> captures);
    [[nodiscard]] const agrovis::CaptureGroup* captureAt(int row) const;
    [[nodiscard]] const std::vector<agrovis::CaptureGroup>& captures() const { return captures_; }

private:
    std::vector<agrovis::CaptureGroup> captures_;
};

}  // namespace agrovis::app
