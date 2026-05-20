#include "maps/NavigationOverlayModels.h"

#include <QtGlobal>

namespace animus
{
namespace
{

bool validFiniteCoordinate(double latitudeDeg, double longitudeDeg, double altitudeM)
{
    const QGeoCoordinate coordinate(latitudeDeg, longitudeDeg, altitudeM);
    return coordinate.isValid() && qIsFinite(altitudeM);
}

QVariantMap vertexMap(double latitudeDeg, double longitudeDeg)
{
    return {{QStringLiteral("latitudeDeg"), latitudeDeg},
            {QStringLiteral("longitudeDeg"), longitudeDeg}};
}

bool validVertexList(const QVariantList &vertices)
{
    if (vertices.size() < 3)
        return false;
    for (const QVariant &vertexValue : vertices)
    {
        const QVariantMap vertex = vertexValue.toMap();
        const double latitudeDeg = vertex.value(QStringLiteral("latitudeDeg")).toDouble();
        const double longitudeDeg = vertex.value(QStringLiteral("longitudeDeg")).toDouble();
        if (!QGeoCoordinate(latitudeDeg, longitudeDeg).isValid())
            return false;
    }
    return true;
}

} // namespace

MissionItemModel::MissionItemModel(QObject *parent) : QAbstractListModel(parent)
{
}

int MissionItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MissionItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    const MissionOverlayItem &item = m_items.at(index.row());
    switch (role)
    {
    case SequenceRole:
        return item.sequence;
    case LabelRole:
        return item.label;
    case LatitudeRole:
        return item.coordinate.latitude();
    case LongitudeRole:
        return item.coordinate.longitude();
    case AltitudeRole:
        return item.coordinate.altitude();
    case CommandRole:
        return item.command;
    case ActiveRole:
        return item.active;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> MissionItemModel::roleNames() const
{
    return {{SequenceRole, "sequence"},
            {LabelRole, "label"},
            {LatitudeRole, "latitudeDeg"},
            {LongitudeRole, "longitudeDeg"},
            {AltitudeRole, "altitudeM"},
            {CommandRole, "command"},
            {ActiveRole, "active"}};
}

void MissionItemModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

bool MissionItemModel::append(int sequence,
                              const QString &label,
                              double latitudeDeg,
                              double longitudeDeg,
                              double altitudeM,
                              const QString &command,
                              bool active)
{
    if (!validFiniteCoordinate(latitudeDeg, longitudeDeg, altitudeM))
        return false;
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.push_back(
        {sequence, label, QGeoCoordinate(latitudeDeg, longitudeDeg, altitudeM), command, active});
    endInsertRows();
    return true;
}

QVariantList MissionItemModel::toVariantList() const
{
    QVariantList list;
    list.reserve(m_items.size());
    for (const MissionOverlayItem &item : m_items)
        list.push_back(QVariantMap{{QStringLiteral("sequence"), item.sequence},
                                   {QStringLiteral("label"), item.label},
                                   {QStringLiteral("latitudeDeg"), item.coordinate.latitude()},
                                   {QStringLiteral("longitudeDeg"), item.coordinate.longitude()},
                                   {QStringLiteral("altitudeM"), item.coordinate.altitude()},
                                   {QStringLiteral("command"), item.command},
                                   {QStringLiteral("active"), item.active}});
    return list;
}

GeofenceOverlayModel::GeofenceOverlayModel(QObject *parent) : QAbstractListModel(parent)
{
}

int GeofenceOverlayModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant GeofenceOverlayModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    const GeofenceOverlay &item = m_items.at(index.row());
    switch (role)
    {
    case IdRole:
        return item.id;
    case LabelRole:
        return item.label;
    case TypeRole:
        return item.type;
    case VerticesRole:
        return item.vertices;
    case CenterLatitudeRole:
        return item.center.latitude();
    case CenterLongitudeRole:
        return item.center.longitude();
    case RadiusRole:
        return item.radiusM;
    case EnabledRole:
        return item.enabled;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> GeofenceOverlayModel::roleNames() const
{
    return {{IdRole, "id"},
            {LabelRole, "label"},
            {TypeRole, "type"},
            {VerticesRole, "vertices"},
            {CenterLatitudeRole, "centerLatitudeDeg"},
            {CenterLongitudeRole, "centerLongitudeDeg"},
            {RadiusRole, "radiusM"},
            {EnabledRole, "enabled"}};
}

void GeofenceOverlayModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

bool GeofenceOverlayModel::appendPolygon(int id,
                                         const QString &label,
                                         const QVariantList &vertices,
                                         bool enabled)
{
    if (!validVertexList(vertices))
        return false;
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.push_back(
        {id, label, QStringLiteral("polygon"), vertices, QGeoCoordinate(), 0.0, enabled});
    endInsertRows();
    return true;
}

bool GeofenceOverlayModel::appendCircle(int id,
                                        const QString &label,
                                        double centerLatitudeDeg,
                                        double centerLongitudeDeg,
                                        double radiusM,
                                        bool enabled)
{
    if (!QGeoCoordinate(centerLatitudeDeg, centerLongitudeDeg).isValid() || !qIsFinite(radiusM) ||
        radiusM <= 0.0)
        return false;
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.push_back({id,
                       label,
                       QStringLiteral("circle"),
                       QVariantList(),
                       QGeoCoordinate(centerLatitudeDeg, centerLongitudeDeg),
                       radiusM,
                       enabled});
    endInsertRows();
    return true;
}

QVariantList GeofenceOverlayModel::toVariantList() const
{
    QVariantList list;
    list.reserve(m_items.size());
    for (const GeofenceOverlay &item : m_items)
        list.push_back(QVariantMap{{QStringLiteral("id"), item.id},
                                   {QStringLiteral("label"), item.label},
                                   {QStringLiteral("type"), item.type},
                                   {QStringLiteral("vertices"), item.vertices},
                                   {QStringLiteral("centerLatitudeDeg"), item.center.latitude()},
                                   {QStringLiteral("centerLongitudeDeg"), item.center.longitude()},
                                   {QStringLiteral("radiusM"), item.radiusM},
                                   {QStringLiteral("enabled"), item.enabled}});
    return list;
}

RallyPointModel::RallyPointModel(QObject *parent) : QAbstractListModel(parent)
{
}

int RallyPointModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant RallyPointModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    const RallyPointOverlay &item = m_items.at(index.row());
    switch (role)
    {
    case IdRole:
        return item.id;
    case LabelRole:
        return item.label;
    case LatitudeRole:
        return item.coordinate.latitude();
    case LongitudeRole:
        return item.coordinate.longitude();
    case AltitudeRole:
        return item.coordinate.altitude();
    case ValidRole:
        return item.valid;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> RallyPointModel::roleNames() const
{
    return {{IdRole, "id"},
            {LabelRole, "label"},
            {LatitudeRole, "latitudeDeg"},
            {LongitudeRole, "longitudeDeg"},
            {AltitudeRole, "altitudeM"},
            {ValidRole, "valid"}};
}

void RallyPointModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

bool RallyPointModel::append(int id,
                             const QString &label,
                             double latitudeDeg,
                             double longitudeDeg,
                             double altitudeM,
                             bool valid)
{
    if (!validFiniteCoordinate(latitudeDeg, longitudeDeg, altitudeM))
        return false;
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.push_back({id, label, QGeoCoordinate(latitudeDeg, longitudeDeg, altitudeM), valid});
    endInsertRows();
    return true;
}

QVariantList RallyPointModel::toVariantList() const
{
    QVariantList list;
    list.reserve(m_items.size());
    for (const RallyPointOverlay &item : m_items)
        list.push_back(QVariantMap{{QStringLiteral("id"), item.id},
                                   {QStringLiteral("label"), item.label},
                                   {QStringLiteral("latitudeDeg"), item.coordinate.latitude()},
                                   {QStringLiteral("longitudeDeg"), item.coordinate.longitude()},
                                   {QStringLiteral("altitudeM"), item.coordinate.altitude()},
                                   {QStringLiteral("valid"), item.valid}});
    return list;
}

EventMarkerModel::EventMarkerModel(QObject *parent) : QAbstractListModel(parent)
{
}

int EventMarkerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant EventMarkerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();
    const EventMarkerOverlay &item = m_items.at(index.row());
    switch (role)
    {
    case IdRole:
        return item.id;
    case LabelRole:
        return item.label;
    case CategoryRole:
        return item.category;
    case SeverityRole:
        return item.severity;
    case TimestampRole:
        return item.timestampS;
    case LatitudeRole:
        return item.coordinate.latitude();
    case LongitudeRole:
        return item.coordinate.longitude();
    case AltitudeRole:
        return item.coordinate.altitude();
    case PositionValidRole:
        return item.positionValid;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> EventMarkerModel::roleNames() const
{
    return {{IdRole, "id"},
            {LabelRole, "label"},
            {CategoryRole, "category"},
            {SeverityRole, "severity"},
            {TimestampRole, "timestampS"},
            {LatitudeRole, "latitudeDeg"},
            {LongitudeRole, "longitudeDeg"},
            {AltitudeRole, "altitudeM"},
            {PositionValidRole, "positionValid"}};
}

void EventMarkerModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
}

bool EventMarkerModel::append(int id,
                              const QString &label,
                              const QString &category,
                              const QString &severity,
                              double timestampS,
                              double latitudeDeg,
                              double longitudeDeg,
                              double altitudeM,
                              bool positionValid)
{
    if (positionValid && !validFiniteCoordinate(latitudeDeg, longitudeDeg, altitudeM))
        return false;
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.push_back({id,
                       label,
                       category,
                       severity,
                       timestampS,
                       QGeoCoordinate(latitudeDeg, longitudeDeg, altitudeM),
                       positionValid});
    endInsertRows();
    return true;
}

QVariantList EventMarkerModel::toVariantList() const
{
    QVariantList list;
    list.reserve(m_items.size());
    for (const EventMarkerOverlay &item : m_items)
        list.push_back(QVariantMap{{QStringLiteral("id"), item.id},
                                   {QStringLiteral("label"), item.label},
                                   {QStringLiteral("category"), item.category},
                                   {QStringLiteral("severity"), item.severity},
                                   {QStringLiteral("timestampS"), item.timestampS},
                                   {QStringLiteral("latitudeDeg"), item.coordinate.latitude()},
                                   {QStringLiteral("longitudeDeg"), item.coordinate.longitude()},
                                   {QStringLiteral("altitudeM"), item.coordinate.altitude()},
                                   {QStringLiteral("positionValid"), item.positionValid}});
    return list;
}

NavigationOverlayModels::NavigationOverlayModels(QObject *parent)
    : QObject(parent), m_missionItems(this), m_geofences(this), m_rallyPoints(this),
      m_eventMarkers(this)
{
    setObjectName(QStringLiteral("navigationOverlays"));
}

MissionItemModel *NavigationOverlayModels::missionItems()
{
    return &m_missionItems;
}

GeofenceOverlayModel *NavigationOverlayModels::geofences()
{
    return &m_geofences;
}

RallyPointModel *NavigationOverlayModels::rallyPoints()
{
    return &m_rallyPoints;
}

EventMarkerModel *NavigationOverlayModels::eventMarkers()
{
    return &m_eventMarkers;
}

void NavigationOverlayModels::clear()
{
    m_missionItems.clear();
    m_geofences.clear();
    m_rallyPoints.clear();
    m_eventMarkers.clear();
}

void NavigationOverlayModels::seedCruise6DofFixture()
{
    clear();

    m_missionItems.append(0,
                          QStringLiteral("Launch"),
                          37.4275,
                          -122.1697,
                          35.0,
                          QStringLiteral("NAV_TAKEOFF"),
                          false);
    m_missionItems.append(1,
                          QStringLiteral("Cruise east"),
                          37.4310,
                          -122.1552,
                          92.0,
                          QStringLiteral("NAV_WAYPOINT"),
                          true);
    m_missionItems.append(2,
                          QStringLiteral("Recover"),
                          37.4202,
                          -122.1468,
                          70.0,
                          QStringLiteral("NAV_WAYPOINT"),
                          false);

    m_geofences.appendPolygon(10,
                              QStringLiteral("Stanford ops box"),
                              QVariantList{vertexMap(37.4350, -122.1810),
                                           vertexMap(37.4385, -122.1510),
                                           vertexMap(37.4180, -122.1390),
                                           vertexMap(37.4095, -122.1710)},
                              true);
    m_geofences.appendCircle(11, QStringLiteral("Launch keep-in"), 37.4275, -122.1697, 720.0, true);

    m_rallyPoints.append(20, QStringLiteral("Rally north"), 37.4382, -122.1645, 60.0, true);
    m_rallyPoints.append(21, QStringLiteral("Rally south"), 37.4148, -122.1585, 54.0, true);

    m_eventMarkers.append(30,
                          QStringLiteral("Sim start"),
                          QStringLiteral("system"),
                          QStringLiteral("info"),
                          0.0,
                          37.4275,
                          -122.1697,
                          35.0,
                          true);
    m_eventMarkers.append(31,
                          QStringLiteral("Guardrail sample"),
                          QStringLiteral("terrain"),
                          QStringLiteral("caution"),
                          18.0,
                          37.4288,
                          -122.1565,
                          75.0,
                          true);
}

QVariantList NavigationOverlayModels::missionItemList() const
{
    return m_missionItems.toVariantList();
}

QVariantList NavigationOverlayModels::geofenceList() const
{
    return m_geofences.toVariantList();
}

QVariantList NavigationOverlayModels::rallyPointList() const
{
    return m_rallyPoints.toVariantList();
}

QVariantList NavigationOverlayModels::eventMarkerList() const
{
    return m_eventMarkers.toVariantList();
}

QVariantMap NavigationOverlayModels::toVariantMap(int activeMissionSeq, bool missionValid) const
{
    return {{QStringLiteral("missionItems"), m_missionItems.toVariantList()},
            {QStringLiteral("geofences"), m_geofences.toVariantList()},
            {QStringLiteral("rallyPoints"), m_rallyPoints.toVariantList()},
            {QStringLiteral("eventMarkers"), m_eventMarkers.toVariantList()},
            {QStringLiteral("activeMissionSeq"), activeMissionSeq},
            {QStringLiteral("missionValid"), missionValid}};
}

} // namespace animus
