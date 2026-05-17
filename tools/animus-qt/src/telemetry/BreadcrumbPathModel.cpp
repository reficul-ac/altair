#include "telemetry/BreadcrumbPathModel.h"

#include <QtGlobal>

namespace animus
{

BreadcrumbPathModel::BreadcrumbPathModel(QObject *parent)
    : QAbstractListModel(parent), m_maxPoints(1200), m_minDistanceM(2.0)
{
}

int BreadcrumbPathModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_points.size();
}

QVariant BreadcrumbPathModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size())
        return QVariant();
    const BreadcrumbPoint &point = m_points.at(index.row());
    switch (role)
    {
    case LatitudeRole:
        return point.coordinate.latitude();
    case LongitudeRole:
        return point.coordinate.longitude();
    case AltitudeRole:
        return point.coordinate.altitude();
    case TimestampRole:
        return point.timestampS;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> BreadcrumbPathModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[LatitudeRole] = "latitude";
    roles[LongitudeRole] = "longitude";
    roles[AltitudeRole] = "altitude";
    roles[TimestampRole] = "timestampS";
    return roles;
}

int BreadcrumbPathModel::maxPoints() const
{
    return m_maxPoints;
}

void BreadcrumbPathModel::setMaxPoints(int maxPoints)
{
    const int bounded = qBound(1, maxPoints, 100000);
    if (m_maxPoints == bounded)
        return;
    m_maxPoints = bounded;
    while (m_points.size() > m_maxPoints)
    {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_points.removeFirst();
        endRemoveRows();
    }
    emit limitsChanged();
}

double BreadcrumbPathModel::minDistanceM() const
{
    return m_minDistanceM;
}

void BreadcrumbPathModel::setMinDistanceM(double minDistanceM)
{
    const double bounded = qMax(0.0, minDistanceM);
    if (qFuzzyCompare(m_minDistanceM, bounded))
        return;
    m_minDistanceM = bounded;
    emit limitsChanged();
}

void BreadcrumbPathModel::clear()
{
    beginResetModel();
    m_points.clear();
    endResetModel();
}

bool BreadcrumbPathModel::append(double latitudeDeg,
                                 double longitudeDeg,
                                 double altitudeM,
                                 double timestampS)
{
    const QGeoCoordinate coordinate(latitudeDeg, longitudeDeg, altitudeM);
    if (!coordinate.isValid())
        return false;
    if (!m_points.isEmpty() && m_minDistanceM > 0.0)
    {
        const double distanceM = m_points.constLast().coordinate.distanceTo(coordinate);
        if (distanceM < m_minDistanceM)
            return false;
    }

    if (m_points.size() >= m_maxPoints)
    {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_points.removeFirst();
        endRemoveRows();
    }

    const int row = m_points.size();
    beginInsertRows(QModelIndex(), row, row);
    m_points.push_back({coordinate, timestampS});
    endInsertRows();
    return true;
}

} // namespace animus
