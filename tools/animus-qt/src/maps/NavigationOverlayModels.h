#pragma once

#include <QAbstractListModel>
#include <QGeoCoordinate>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace animus
{

struct MissionOverlayItem
{
    int sequence;
    QString label;
    QGeoCoordinate coordinate;
    QString command;
    bool active;
};

struct GeofenceOverlay
{
    int id;
    QString label;
    QString type;
    QVariantList vertices;
    QGeoCoordinate center;
    double radiusM;
    bool enabled;
};

struct RallyPointOverlay
{
    int id;
    QString label;
    QGeoCoordinate coordinate;
    bool valid;
};

struct EventMarkerOverlay
{
    int id;
    QString label;
    QString category;
    QString severity;
    double timestampS;
    QGeoCoordinate coordinate;
    bool positionValid;
};

class MissionItemModel final : public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Roles
    {
        SequenceRole = Qt::UserRole + 1,
        LabelRole,
        LatitudeRole,
        LongitudeRole,
        AltitudeRole,
        CommandRole,
        ActiveRole
    };
    Q_ENUM(Roles)

    explicit MissionItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool append(int sequence,
                            const QString &label,
                            double latitudeDeg,
                            double longitudeDeg,
                            double altitudeM,
                            const QString &command,
                            bool active);

    QVariantList toVariantList() const;

  private:
    QVector<MissionOverlayItem> m_items;
};

class GeofenceOverlayModel final : public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        TypeRole,
        VerticesRole,
        CenterLatitudeRole,
        CenterLongitudeRole,
        RadiusRole,
        EnabledRole
    };
    Q_ENUM(Roles)

    explicit GeofenceOverlayModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool appendPolygon(int id,
                                   const QString &label,
                                   const QVariantList &vertices,
                                   bool enabled);
    Q_INVOKABLE bool appendCircle(int id,
                                  const QString &label,
                                  double centerLatitudeDeg,
                                  double centerLongitudeDeg,
                                  double radiusM,
                                  bool enabled);

    QVariantList toVariantList() const;

  private:
    QVector<GeofenceOverlay> m_items;
};

class RallyPointModel final : public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        LatitudeRole,
        LongitudeRole,
        AltitudeRole,
        ValidRole
    };
    Q_ENUM(Roles)

    explicit RallyPointModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool append(int id,
                            const QString &label,
                            double latitudeDeg,
                            double longitudeDeg,
                            double altitudeM,
                            bool valid);

    QVariantList toVariantList() const;

  private:
    QVector<RallyPointOverlay> m_items;
};

class EventMarkerModel final : public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        CategoryRole,
        SeverityRole,
        TimestampRole,
        LatitudeRole,
        LongitudeRole,
        AltitudeRole,
        PositionValidRole
    };
    Q_ENUM(Roles)

    explicit EventMarkerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    Q_INVOKABLE bool append(int id,
                            const QString &label,
                            const QString &category,
                            const QString &severity,
                            double timestampS,
                            double latitudeDeg,
                            double longitudeDeg,
                            double altitudeM,
                            bool positionValid);

    QVariantList toVariantList() const;

  private:
    QVector<EventMarkerOverlay> m_items;
};

class NavigationOverlayModels final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MissionItemModel *missionItems READ missionItems CONSTANT)
    Q_PROPERTY(GeofenceOverlayModel *geofences READ geofences CONSTANT)
    Q_PROPERTY(RallyPointModel *rallyPoints READ rallyPoints CONSTANT)
    Q_PROPERTY(EventMarkerModel *eventMarkers READ eventMarkers CONSTANT)

  public:
    explicit NavigationOverlayModels(QObject *parent = nullptr);

    MissionItemModel *missionItems();
    GeofenceOverlayModel *geofences();
    RallyPointModel *rallyPoints();
    EventMarkerModel *eventMarkers();

    Q_INVOKABLE void clear();
    Q_INVOKABLE void seedCruise6DofFixture();
    Q_INVOKABLE QVariantList missionItemList() const;
    Q_INVOKABLE QVariantList geofenceList() const;
    Q_INVOKABLE QVariantList rallyPointList() const;
    Q_INVOKABLE QVariantList eventMarkerList() const;

    QVariantMap toVariantMap(int activeMissionSeq, bool missionValid) const;

  private:
    MissionItemModel m_missionItems;
    GeofenceOverlayModel m_geofences;
    RallyPointModel m_rallyPoints;
    EventMarkerModel m_eventMarkers;
};

} // namespace animus
