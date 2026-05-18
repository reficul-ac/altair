#include "maps/MapPackManager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

#include <sqlite3.h>

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
    case TileDatabasePathRole:
        return pack.tileDatabasePath;
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
    roles[TileDatabasePathRole] = "tileDatabasePath";
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

QString MapPackManager::activeTileDatabasePath() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->tileDatabasePath : QString();
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

bool MapPackManager::activeHasMbtilesImagery() const
{
    const MapPack *pack = findPack(m_activePackId);
    return pack && pack->has2dImagery && pack->imageryFormat == QStringLiteral("mbtiles") &&
           !pack->tileDatabasePath.isEmpty();
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

MbtilesPack MapPackManager::mbtilesPackInfo(const QString &packId) const
{
    const MapPack *pack = findPack(packId);
    if (!pack || !pack->has2dImagery || pack->imageryFormat != QStringLiteral("mbtiles") ||
        pack->tileDatabasePath.isEmpty())
        return MbtilesPack{false, QString(), 0, 0};
    return MbtilesPack{true, pack->tileDatabasePath, pack->minZoom, pack->maxZoom};
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
    if (imageryFormat != QStringLiteral("mbtiles"))
    {
        if (error)
            *error = QStringLiteral("%1: imagery.format must be mbtiles").arg(packDir.dirName());
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

    const QString tilePath = imagery.value(QStringLiteral("path"))
                                 .toString(QStringLiteral("2d/imagery.mbtiles"))
                                 .trimmed();
    if (tilePath.isEmpty() || QDir::isAbsolutePath(tilePath) ||
        tilePath.contains(QStringLiteral("..")))
    {
        if (error)
            *error =
                QStringLiteral("%1: imagery.path must be a relative path").arg(packDir.dirName());
        return false;
    }
    const QFileInfo tileDatabaseInfo(packDir.filePath(tilePath));
    if (!tileDatabaseInfo.exists() || !tileDatabaseInfo.isFile())
    {
        if (error)
            *error = QStringLiteral("%1: MBTiles database is missing").arg(packDir.dirName());
        return false;
    }
    const QString tileDatabasePath = tileDatabaseInfo.canonicalFilePath();
    QString databaseError;
    if (!validateMbtilesDatabase(tileDatabasePath, name, attribution, &databaseError))
    {
        if (error)
            *error = QStringLiteral("%1: %2").arg(packDir.dirName(), databaseError);
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
    parsed.tileDatabasePath = tileDatabasePath;
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

bool MapPackManager::validateMbtilesDatabase(const QString &databasePath,
                                             const QString &expectedName,
                                             const QString &expectedAttribution,
                                             QString *error)
{
    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(databasePath.toUtf8().constData(),
                        &database,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                        nullptr) != SQLITE_OK)
    {
        if (error)
            *error = QStringLiteral("MBTiles database cannot be opened read-only");
        if (database)
            sqlite3_close(database);
        return false;
    }

    auto closeWithError = [&](const QString &message)
    {
        if (error)
            *error = message;
        sqlite3_close(database);
        return false;
    };

    auto scalarText = [&](const char *sql, QString *value) -> bool
    {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
            return false;
        const int result = sqlite3_step(statement);
        if (result == SQLITE_ROW)
        {
            const unsigned char *text = sqlite3_column_text(statement, 0);
            if (text)
                *value = QString::fromUtf8(reinterpret_cast<const char *>(text)).trimmed();
        }
        sqlite3_finalize(statement);
        return result == SQLITE_ROW || result == SQLITE_DONE;
    };

    QString tableCount;
    if (!scalarText("SELECT count(*) FROM sqlite_master "
                    "WHERE type = 'table' AND name = 'tiles'",
                    &tableCount) ||
        tableCount.toInt() != 1)
    {
        return closeWithError(QStringLiteral("MBTiles tiles table is missing"));
    }

    QString tileCount;
    if (!scalarText("SELECT count(tile_data) FROM tiles", &tileCount) || tileCount.toInt() <= 0)
        return closeWithError(QStringLiteral("MBTiles tiles table has no tile_data"));

    QString metadataCount;
    if (scalarText("SELECT count(*) FROM sqlite_master "
                   "WHERE type = 'table' AND name = 'metadata'",
                   &metadataCount) &&
        metadataCount.toInt() == 1)
    {
        QString format;
        if (scalarText("SELECT value FROM metadata WHERE name = 'format' LIMIT 1", &format) &&
            !format.isEmpty())
        {
            format = format.toLower();
            if (format != QStringLiteral("png") && format != QStringLiteral("jpg") &&
                format != QStringLiteral("jpeg") && format != QStringLiteral("webp"))
            {
                return closeWithError(QStringLiteral("MBTiles metadata.format must be raster"));
            }
        }

        QString name;
        if (scalarText("SELECT value FROM metadata WHERE name = 'name' LIMIT 1", &name) &&
            !name.isEmpty() && name != expectedName)
        {
            return closeWithError(
                QStringLiteral("MBTiles metadata.name disagrees with metadata.json"));
        }

        QString attribution;
        if (scalarText("SELECT value FROM metadata WHERE name = 'attribution' LIMIT 1",
                       &attribution) &&
            !attribution.isEmpty() && attribution != expectedAttribution)
        {
            return closeWithError(
                QStringLiteral("MBTiles metadata.attribution disagrees with metadata.json"));
        }
    }

    sqlite3_close(database);
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
