#pragma once

#include <QAbstractListModel>
#include <QGeoCoordinate>
#include <QHash>
#include <QVariant>
#include <QVector>

namespace animus
{

struct BreadcrumbPoint
{
    QGeoCoordinate coordinate;
    double timestampS;
};

class BreadcrumbPathModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int maxPoints READ maxPoints WRITE setMaxPoints NOTIFY limitsChanged)
    Q_PROPERTY(double minDistanceM READ minDistanceM WRITE setMinDistanceM NOTIFY limitsChanged)

  public:
    enum Roles
    {
        LatitudeRole = Qt::UserRole + 1,
        LongitudeRole,
        AltitudeRole,
        TimestampRole
    };
    Q_ENUM(Roles)

    explicit BreadcrumbPathModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int maxPoints() const;
    void setMaxPoints(int maxPoints);

    double minDistanceM() const;
    void setMinDistanceM(double minDistanceM);

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool
    append(double latitudeDeg, double longitudeDeg, double altitudeM, double timestampS);

  signals:
    void limitsChanged();
    void countChanged();

  private:
    QVector<BreadcrumbPoint> m_points;
    int m_maxPoints;
    double m_minDistanceM;
};

} // namespace animus
