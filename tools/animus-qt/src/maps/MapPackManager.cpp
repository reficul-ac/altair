#include "maps/MapPackManager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

#include <cmath>
#include <limits>

namespace animus
{

MapPackManager::MapPackManager(QObject *parent)
    : QAbstractListModel(parent), m_rootPath(QStringLiteral("map_packs"))
{
}

int MapPackManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_packs.size();
}

QVariant MapPackManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_packs.size())
        return QVariant();
    const MapPack &pack = m_packs.at(index.row());
    switch (role)
    {
    case IdRole:
        return pack.id;
    case NameRole:
        return pack.name;
    case DescriptionRole:
        return pack.description;
    case PathRole:
        return pack.path;
    case LicenseRole:
        return pack.license;
    case AttributionRole:
        return pack.attribution;
    case ImageryFormatRole:
        return pack.imageryFormat;
    case TerrainFormatRole:
        return pack.terrainFormat;
    case TileRootPathRole:
        return pack.tileRootPath;
    case MinZoomRole:
        return pack.minZoom;
    case MaxZoomRole:
        return pack.maxZoom;
    case HasBoundsRole:
        return pack.hasBounds;
    case WestDegRole:
        return pack.westDeg;
    case SouthDegRole:
        return pack.southDeg;
    case EastDegRole:
        return pack.eastDeg;
    case NorthDegRole:
        return pack.northDeg;
    case Has2dImageryRole:
        return pack.has2dImagery;
    case Has3dTerrainRole:
        return pack.has3dTerrain;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> MapPackManager::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "packId";
    roles[NameRole] = "name";
    roles[DescriptionRole] = "description";
    roles[PathRole] = "path";
    roles[LicenseRole] = "license";
    roles[AttributionRole] = "attribution";
    roles[ImageryFormatRole] = "imageryFormat";
    roles[TerrainFormatRole] = "terrainFormat";
    roles[TileRootPathRole] = "tileRootPath";
    roles[MinZoomRole] = "minZoom";
    roles[MaxZoomRole] = "maxZoom";
    roles[HasBoundsRole] = "hasBounds";
    roles[WestDegRole] = "westDeg";
    roles[SouthDegRole] = "southDeg";
    roles[EastDegRole] = "eastDeg";
    roles[NorthDegRole] = "northDeg";
    roles[Has2dImageryRole] = "has2dImagery";
    roles[Has3dTerrainRole] = "has3dTerrain";
    return roles;
}

QString MapPackManager::rootPath() const
{
    return m_rootPath;
}

void MapPackManager::setRootPath(const QString &rootPath)
{
    if (m_rootPath == rootPath)
        return;
    m_rootPath = rootPath;
    reload();
    emit packsChanged();
}

QString MapPackManager::activePackId() const
{
    return m_activePackId;
}

void MapPackManager::setActivePackId(const QString &activePackId)
{
    if (!activePackId.isEmpty() && !findPack(activePackId))
        return;
    if (m_activePackId == activePackId)
        return;
    m_activePackId = activePackId;
    emit activePackChanged();
}

QString MapPackManager::activePackPath() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->path : QString();
}

QString MapPackManager::activeTileRootPath() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->tileRootPath : QString();
}

int MapPackManager::activeMinZoom() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->minZoom : 0;
}

int MapPackManager::activeMaxZoom() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->maxZoom : 0;
}

bool MapPackManager::activeHasLocalXyzImagery() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack && pack->has2dImagery && pack->imageryFormat == QStringLiteral("xyz") &&
           !pack->tileRootPath.isEmpty();
}

bool MapPackManager::activeHasBounds() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->hasBounds : false;
}

double MapPackManager::activeWestDeg() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->westDeg : 0.0;
}

double MapPackManager::activeSouthDeg() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->southDeg : 0.0;
}

double MapPackManager::activeEastDeg() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->eastDeg : 0.0;
}

double MapPackManager::activeNorthDeg() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->northDeg : 0.0;
}

bool MapPackManager::reload()
{
    QDir root(m_rootPath);
    QVector<MapPack> loaded;
    QString firstError;

    if (root.exists())
    {
        const QFileInfoList entries =
            root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries)
        {
            MapPack pack;
            QString error;
            if (loadPack(QDir(entry.absoluteFilePath()), &pack, &error))
            {
                loaded.push_back(pack);
            }
            else if (firstError.isEmpty())
            {
                firstError = error;
            }
        }
    }

    beginResetModel();
    m_packs = loaded;
    m_validationError = firstError;
    if (!m_activePackId.isEmpty() && !findPack(m_activePackId))
        m_activePackId.clear();
    if (m_activePackId.isEmpty() && !m_packs.isEmpty())
        m_activePackId = m_packs.constFirst().id;
    endResetModel();
    emit packsChanged();
    emit activePackChanged();
    return firstError.isEmpty();
}

QString MapPackManager::validationError() const
{
    return m_validationError;
}

QString MapPackManager::activeAttribution() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->attribution : QString();
}

QString MapPackManager::localXyzTilePath(const QString &packId, int zoom, int x, int y) const
{
    const LocalXyzPack pack = localXyzPackInfo(packId);
    if (!pack.valid)
        return QString();
    if (zoom < pack.minZoom || zoom > pack.maxZoom || x < 0 || y < 0)
        return QString();

    if (zoom < 0 || zoom > 30)
        return QString();
    const int maxIndex = (1 << zoom) - 1;
    if (x > maxIndex || y > maxIndex)
        return QString();

    const QDir tileRoot(pack.tileRootPath);
    const QString relativePath = QStringLiteral("%1/%2/%3.png").arg(zoom).arg(x).arg(y);
    const QString absolutePath = QFileInfo(tileRoot.filePath(relativePath)).canonicalFilePath();
    const QString canonicalRoot = QFileInfo(pack.tileRootPath).canonicalFilePath();
    if (absolutePath.isEmpty() || canonicalRoot.isEmpty())
        return QString();
    if (absolutePath != canonicalRoot &&
        !absolutePath.startsWith(canonicalRoot + QDir::separator()))
        return QString();
    if (!absolutePath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        return QString();
    return absolutePath;
}

LocalXyzPack MapPackManager::localXyzPackInfo(const QString &packId) const
{
    const MapPack *pack = findPack(packId);
    if (!pack || !pack->has2dImagery || pack->imageryFormat != QStringLiteral("xyz") ||
        pack->tileRootPath.isEmpty())
        return LocalXyzPack{false, QString(), 0, 0};
    return LocalXyzPack{true, pack->tileRootPath, pack->minZoom, pack->maxZoom};
}

bool MapPackManager::loadPack(const QDir &packDir, MapPack *pack, QString *error) const
{
    QFile metadata(packDir.filePath(QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("%1: metadata.json is missing").arg(packDir.dirName());
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
    if (!document.isObject())
    {
        if (error)
            *error = QStringLiteral("%1: metadata.json must be an object").arg(packDir.dirName());
        return false;
    }

    const QJsonObject object = document.object();
    const int schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(0);
    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    const QString license = object.value(QStringLiteral("license")).toString().trimmed();
    const QString attribution = object.value(QStringLiteral("attribution")).toString().trimmed();
    if (schemaVersion != 1 || name.isEmpty() || license.isEmpty() || attribution.isEmpty())
    {
        if (error)
            *error =
                QStringLiteral("%1: schemaVersion 1, name, license, and attribution are required")
                    .arg(packDir.dirName());
        return false;
    }

    const QJsonObject imagery = object.value(QStringLiteral("imagery")).toObject();
    const QJsonObject terrain = object.value(QStringLiteral("terrain")).toObject();
    const QString imageryFormat = imagery.value(QStringLiteral("format")).toString().trimmed();
    const QString terrainFormat =
        terrain.value(QStringLiteral("format")).toString(QStringLiteral("none")).trimmed();
    if (imageryFormat != QStringLiteral("xyz"))
    {
        if (error)
            *error = QStringLiteral("%1: imagery.format must be xyz").arg(packDir.dirName());
        return false;
    }
    if (terrainFormat != QStringLiteral("none") &&
        terrainFormat != QStringLiteral("quantized-mesh"))
    {
        if (error)
            *error = QStringLiteral("%1: terrain.format must be none or quantized-mesh")
                         .arg(packDir.dirName());
        return false;
    }

    const int minZoom = object.value(QStringLiteral("minZoom")).toInt(-1);
    const int maxZoom = object.value(QStringLiteral("maxZoom")).toInt(-1);
    if (minZoom < 0 || maxZoom < minZoom || maxZoom > 30)
    {
        if (error)
            *error = QStringLiteral("%1: minZoom and maxZoom must be a valid 0..30 range")
                         .arg(packDir.dirName());
        return false;
    }

    const QString tileRoot =
        imagery.value(QStringLiteral("tileRoot")).toString(QStringLiteral("2d/xyz")).trimmed();
    if (tileRoot.isEmpty() || QDir::isAbsolutePath(tileRoot) ||
        tileRoot.contains(QStringLiteral("..")))
    {
        if (error)
            *error = QStringLiteral("%1: imagery.tileRoot must be a relative path")
                         .arg(packDir.dirName());
        return false;
    }
    const QFileInfo tileRootInfo(packDir.filePath(tileRoot));
    if (!tileRootInfo.exists() || !tileRootInfo.isDir())
    {
        if (error)
            *error = QStringLiteral("%1: local XYZ tile root is missing").arg(packDir.dirName());
        return false;
    }

    MapPack parsed;
    parsed.id = packDir.dirName();
    parsed.name = name;
    parsed.description = object.value(QStringLiteral("description")).toString();
    parsed.path = packDir.absolutePath();
    parsed.license = license;
    parsed.attribution = attribution;
    parsed.imageryFormat = imageryFormat;
    parsed.terrainFormat = terrainFormat;
    parsed.tileRootPath = tileRootInfo.canonicalFilePath();
    parsed.minZoom = minZoom;
    parsed.maxZoom = maxZoom;
    parsed.hasBounds = false;
    parsed.westDeg = 0.0;
    parsed.southDeg = 0.0;
    parsed.eastDeg = 0.0;
    parsed.northDeg = 0.0;
    if (!parseBounds(object, &parsed, error))
    {
        if (error && !error->isEmpty())
            *error = QStringLiteral("%1: %2").arg(packDir.dirName(), *error);
        return false;
    }
    parsed.has2dImagery = true;
    parsed.has3dTerrain =
        terrainFormat == QStringLiteral("quantized-mesh") &&
        QFileInfo::exists(packDir.filePath(QStringLiteral("3d/terrain/layer.json")));
    *pack = parsed;
    return true;
}

const MapPack *MapPackManager::findPack(const QString &packId) const
{
    for (const MapPack &pack : m_packs)
    {
        if (pack.id == packId)
            return &pack;
    }
    return nullptr;
}

bool MapPackManager::parseBounds(const QJsonObject &object, MapPack *pack, QString *error)
{
    const QJsonValue value = object.value(QStringLiteral("bounds"));
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isObject())
    {
        if (error)
            *error = QStringLiteral("bounds must be an object");
        return false;
    }

    const QJsonObject bounds = value.toObject();
    bool ok = true;
    const double unset = std::numeric_limits<double>::quiet_NaN();
    const double west = bounds.value(QStringLiteral("west")).toDouble(unset);
    const double south = bounds.value(QStringLiteral("south")).toDouble(unset);
    const double east = bounds.value(QStringLiteral("east")).toDouble(unset);
    const double north = bounds.value(QStringLiteral("north")).toDouble(unset);
    ok = std::isfinite(west) && std::isfinite(south) && std::isfinite(east) &&
         std::isfinite(north) && west >= -180.0 && east <= 180.0 && west < east &&
         south >= -85.05112878 && north <= 85.05112878 && south < north;
    if (!ok)
    {
        if (error)
            *error = QStringLiteral("bounds must be west/south/east/north degrees");
        return false;
    }

    pack->hasBounds = true;
    pack->westDeg = west;
    pack->southDeg = south;
    pack->eastDeg = east;
    pack->northDeg = north;
    return true;
}

} // namespace animus
