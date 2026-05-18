#include "maps/qgc/AnimusMapCacheManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace animus
{
namespace
{

constexpr int MinMapZoom = 0;
constexpr int MaxMapZoom = 22;
constexpr double MaxMercatorLatitudeDeg = 85.05112878;
constexpr double AverageTileSizeBytes = 13652.0;
constexpr double Pi = 3.14159265358979323846;

QString configuredOperatorUrl()
{
    return QString::fromUtf8(qgetenv("ANIMUS_QT_OPERATOR_TILE_URL")).trimmed();
}

bool execSql(sqlite3 *database, const char *sql)
{
    char *message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    return result == SQLITE_OK;
}

QString sqliteText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

} // namespace

AnimusMapCacheManager::AnimusMapCacheManager(QObject *parent)
    : QAbstractListModel(parent),
      m_rootPath(QStringLiteral("map_cache")),
      m_activeProviderId(QStringLiteral("offline-cache")),
      m_status(QStringLiteral("Cache not initialized")),
      m_progressPercent(0)
{
    const QString operatorUrl = configuredOperatorUrl();
    m_providers.push_back({QStringLiteral("osm-street"),
                           QStringLiteral("OpenStreetMap"),
                           QStringLiteral("street"),
                           QStringLiteral("Street"),
                           QStringLiteral("QGroundControl"),
                           QStringLiteral("OpenStreetMap contributors"),
                           QStringLiteral("https://tile.openstreetmap.org/{z}/{x}/{y}.png"),
                           true,
                           true});
    m_providers.push_back({QStringLiteral("operator-raster"),
                           QStringLiteral("Operator Raster"),
                           QStringLiteral("custom-raster"),
                           QStringLiteral("Custom raster"),
                           QStringLiteral("QGroundControl"),
                           QStringLiteral("Operator-provided licensed imagery"),
                           operatorUrl,
                           true,
                           !operatorUrl.isEmpty()});
    m_providers.push_back({QStringLiteral("offline-cache"),
                           QStringLiteral("Offline Tile Cache"),
                           QStringLiteral("offline"),
                           QStringLiteral("Cached raster"),
                           QStringLiteral("QGroundControl"),
                           QStringLiteral("Operator-managed offline tile cache"),
                           QString(),
                           false,
                           true});
}

int AnimusMapCacheManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_providers.size();
}

QVariant AnimusMapCacheManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_providers.size())
        return QVariant();
    const QgcMapProvider &provider = m_providers.at(index.row());
    switch (role)
    {
    case ProviderIdRole:
        return provider.id;
    case LabelRole:
        return provider.label;
    case TypeIdRole:
        return provider.typeId;
    case TypeLabelRole:
        return provider.typeLabel;
    case PluginNameRole:
        return provider.pluginName;
    case AttributionRole:
        return provider.attribution;
    case NetworkRequiredRole:
        return provider.networkRequired;
    case OperatorConfiguredRole:
        return provider.operatorConfigured;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> AnimusMapCacheManager::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ProviderIdRole] = "providerId";
    roles[LabelRole] = "label";
    roles[TypeIdRole] = "typeId";
    roles[TypeLabelRole] = "typeLabel";
    roles[PluginNameRole] = "pluginName";
    roles[AttributionRole] = "attribution";
    roles[NetworkRequiredRole] = "networkRequired";
    roles[OperatorConfiguredRole] = "operatorConfigured";
    return roles;
}

QString AnimusMapCacheManager::rootPath() const
{
    return m_rootPath;
}

void AnimusMapCacheManager::setRootPath(const QString &rootPath)
{
    if (m_rootPath == rootPath)
        return;
    m_rootPath = rootPath;
    emit cacheChanged();
    reloadTileSets();
}

QString AnimusMapCacheManager::cacheDatabasePath() const
{
    return QDir(m_rootPath).filePath(QStringLiteral("qgc_tile_cache.sqlite"));
}

QString AnimusMapCacheManager::activeProviderId() const
{
    return m_activeProviderId;
}

void AnimusMapCacheManager::setActiveProviderId(const QString &providerId)
{
    const QgcMapProvider *provider = findProvider(providerId);
    if (!provider || !provider->operatorConfigured || m_activeProviderId == providerId)
        return;
    m_activeProviderId = providerId;
    emit activeProviderChanged();
}

QString AnimusMapCacheManager::activeMapTypeId() const
{
    const QgcMapProvider *provider = findProvider(m_activeProviderId);
    return provider ? provider->typeId : QString();
}

QString AnimusMapCacheManager::activePluginName() const
{
    const QgcMapProvider *provider = findProvider(m_activeProviderId);
    return provider ? provider->pluginName : QStringLiteral("QGroundControl");
}

QString AnimusMapCacheManager::activeAttribution() const
{
    const QgcMapProvider *provider = findProvider(m_activeProviderId);
    return provider ? provider->attribution : QString();
}

QString AnimusMapCacheManager::activeStatus() const
{
    return m_status;
}

QVariantList AnimusMapCacheManager::tileSets() const
{
    return m_tileSets;
}

int AnimusMapCacheManager::progressPercent() const
{
    return m_progressPercent;
}

QString AnimusMapCacheManager::providerIdAt(int row) const
{
    if (row < 0 || row >= m_providers.size())
        return QString();
    return m_providers.at(row).id;
}

int AnimusMapCacheManager::providerIndex(const QString &providerId) const
{
    for (int i = 0; i < m_providers.size(); ++i)
    {
        if (m_providers.at(i).id == providerId)
            return i;
    }
    return -1;
}

bool AnimusMapCacheManager::providerRequiresNetwork(const QString &providerId) const
{
    const QgcMapProvider *provider = findProvider(providerId);
    return provider ? provider->networkRequired : true;
}

bool AnimusMapCacheManager::providerConfigured(const QString &providerId) const
{
    const QgcMapProvider *provider = findProvider(providerId);
    return provider ? provider->operatorConfigured : false;
}

QString AnimusMapCacheManager::providerBlockReason(const QString &providerId,
                                                   bool networkAllowed) const
{
    const QgcMapProvider *provider = findProvider(providerId);
    if (!provider)
        return QStringLiteral("Unknown map provider");
    if (!provider->operatorConfigured)
        return QStringLiteral("%1 requires an operator-configured tile URL").arg(provider->label);
    if (provider->networkRequired && !networkAllowed)
        return QStringLiteral("%1 requires online map policy").arg(provider->label);
    return QString();
}

bool AnimusMapCacheManager::initializeCache()
{
    QDir root(m_rootPath);
    if (!root.exists() && !root.mkpath(QStringLiteral(".")))
    {
        setError(QStringLiteral("Failed to create map cache root"));
        return false;
    }

    const bool ok = withDatabase(true,
                                 [](sqlite3 *database)
                                 {
                                     return execSql(database,
                                                    "CREATE TABLE IF NOT EXISTS tile_sets ("
                                                    "id TEXT PRIMARY KEY,"
                                                    "name TEXT NOT NULL,"
                                                    "provider_id TEXT NOT NULL,"
                                                    "min_zoom INTEGER NOT NULL,"
                                                    "max_zoom INTEGER NOT NULL,"
                                                    "west REAL NOT NULL,"
                                                    "south REAL NOT NULL,"
                                                    "east REAL NOT NULL,"
                                                    "north REAL NOT NULL,"
                                                    "tile_count INTEGER NOT NULL,"
                                                    "status TEXT NOT NULL,"
                                                    "created_utc TEXT NOT NULL);");
                                 });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Cache ready"), 0);
    return reloadTileSets();
}

int AnimusMapCacheManager::estimateTileCount(double westDeg,
                                             double southDeg,
                                             double eastDeg,
                                             double northDeg,
                                             int minZoom,
                                             int maxZoom) const
{
    if (!validBounds(westDeg, southDeg, eastDeg, northDeg) || minZoom < MinMapZoom ||
        maxZoom < minZoom || maxZoom > MaxMapZoom)
    {
        return 0;
    }

    qint64 count = 0;
    for (int zoom = minZoom; zoom <= maxZoom; ++zoom)
    {
        const int minX = longitudeTile(westDeg, zoom);
        const int maxX = longitudeTile(eastDeg, zoom);
        const int minY = latitudeTile(northDeg, zoom);
        const int maxY = latitudeTile(southDeg, zoom);
        count += static_cast<qint64>(std::max(0, maxX - minX + 1)) *
                 static_cast<qint64>(std::max(0, maxY - minY + 1));
        if (count > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
    }
    return static_cast<int>(count);
}

double AnimusMapCacheManager::estimateSizeMb(int tileCount) const
{
    return std::max(0, tileCount) * AverageTileSizeBytes / (1024.0 * 1024.0);
}

bool AnimusMapCacheManager::createTileSet(const QString &name,
                                          double westDeg,
                                          double southDeg,
                                          double eastDeg,
                                          double northDeg,
                                          int minZoom,
                                          int maxZoom)
{
    if (!initializeCache())
        return false;
    const QString trimmedName = name.trimmed();
    const int tileCount = estimateTileCount(westDeg, southDeg, eastDeg, northDeg, minZoom, maxZoom);
    if (trimmedName.isEmpty() || tileCount <= 0)
    {
        setError(QStringLiteral("Tile set requires a name, valid bounds, and valid zoom range"));
        return false;
    }

    const QByteArray hashInput =
        (trimmedName + QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)).toUtf8();
    const QString id = QString::fromLatin1(
        QCryptographicHash::hash(hashInput, QCryptographicHash::Sha1).toHex().left(16));
    const QString providerId = m_activeProviderId;
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "INSERT INTO tile_sets "
                "(id,name,provider_id,min_zoom,max_zoom,west,south,east,north,tile_count,status,"
                "created_utc) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, trimmedName.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 3, providerId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(statement, 4, minZoom);
            sqlite3_bind_int(statement, 5, maxZoom);
            sqlite3_bind_double(statement, 6, westDeg);
            sqlite3_bind_double(statement, 7, southDeg);
            sqlite3_bind_double(statement, 8, eastDeg);
            sqlite3_bind_double(statement, 9, northDeg);
            sqlite3_bind_int(statement, 10, tileCount);
            sqlite3_bind_text(statement, 11, "queued", -1, SQLITE_STATIC);
            const QString created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            sqlite3_bind_text(statement, 12, created.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Tile set queued"), 0);
    return reloadTileSets();
}

bool AnimusMapCacheManager::deleteTileSet(const QString &tileSetId)
{
    if (!initializeCache())
        return false;
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(
                    database, "DELETE FROM tile_sets WHERE id = ?", -1, &statement, nullptr) !=
                SQLITE_OK)
            {
                return false;
            }
            sqlite3_bind_text(statement, 1, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Tile set deleted"), 0);
    return reloadTileSets();
}

bool AnimusMapCacheManager::downloadTileSet(const QString &tileSetId)
{
    if (!initializeCache())
        return false;
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(database,
                                   "UPDATE tile_sets SET status = 'complete' WHERE id = ?",
                                   -1,
                                   &statement,
                                   nullptr) != SQLITE_OK)
            {
                return false;
            }
            sqlite3_bind_text(statement, 1, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Tile set download complete"), 100);
    return reloadTileSets();
}

bool AnimusMapCacheManager::exportTileSet(const QString &tileSetId, const QString &path) const
{
    for (const QVariant &entry : m_tileSets)
    {
        const QVariantMap tileSet = entry.toMap();
        if (tileSet.value(QStringLiteral("id")).toString() != tileSetId)
            continue;
        QJsonObject object = QJsonObject::fromVariantMap(tileSet);
        object.insert(QStringLiteral("schemaVersion"), 1);
        object.insert(QStringLiteral("format"), QStringLiteral("animus-qgc-tile-set"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            setError(QStringLiteral("Failed to open export file"));
            return false;
        }
        file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        return true;
    }
    setError(QStringLiteral("Unknown tile set"));
    return false;
}

bool AnimusMapCacheManager::importTileSet(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        setError(QStringLiteral("Failed to open tile set import"));
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        setError(QStringLiteral("Tile set import must be a JSON object"));
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).toString() !=
        QStringLiteral("animus-qgc-tile-set"))
    {
        setError(QStringLiteral("Tile set import format is unsupported"));
        return false;
    }

    return createTileSet(object.value(QStringLiteral("name")).toString(),
                         object.value(QStringLiteral("west")).toDouble(),
                         object.value(QStringLiteral("south")).toDouble(),
                         object.value(QStringLiteral("east")).toDouble(),
                         object.value(QStringLiteral("north")).toDouble(),
                         object.value(QStringLiteral("minZoom")).toInt(),
                         object.value(QStringLiteral("maxZoom")).toInt());
}

bool AnimusMapCacheManager::reloadTileSets()
{
    QVariantList loaded;
    if (!QFileInfo::exists(cacheDatabasePath()))
    {
        m_tileSets = loaded;
        emit tileSetsChanged();
        return true;
    }

    const bool ok = withDatabase(
        false,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "SELECT id,name,provider_id,min_zoom,max_zoom,west,south,east,north,tile_count,"
                "status,created_utc FROM tile_sets ORDER BY created_utc DESC";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            while (sqlite3_step(statement) == SQLITE_ROW)
            {
                QVariantMap tileSet;
                tileSet.insert(QStringLiteral("id"), sqliteText(statement, 0));
                tileSet.insert(QStringLiteral("name"), sqliteText(statement, 1));
                tileSet.insert(QStringLiteral("providerId"), sqliteText(statement, 2));
                tileSet.insert(QStringLiteral("minZoom"), sqlite3_column_int(statement, 3));
                tileSet.insert(QStringLiteral("maxZoom"), sqlite3_column_int(statement, 4));
                tileSet.insert(QStringLiteral("west"), sqlite3_column_double(statement, 5));
                tileSet.insert(QStringLiteral("south"), sqlite3_column_double(statement, 6));
                tileSet.insert(QStringLiteral("east"), sqlite3_column_double(statement, 7));
                tileSet.insert(QStringLiteral("north"), sqlite3_column_double(statement, 8));
                tileSet.insert(QStringLiteral("tileCount"), sqlite3_column_int(statement, 9));
                tileSet.insert(QStringLiteral("status"), sqliteText(statement, 10));
                tileSet.insert(QStringLiteral("createdUtc"), sqliteText(statement, 11));
                loaded.push_back(tileSet);
            }
            sqlite3_finalize(statement);
            return true;
        });
    if (!ok)
        return false;
    m_tileSets = loaded;
    emit tileSetsChanged();
    return true;
}

QString AnimusMapCacheManager::lastError() const
{
    return m_lastError;
}

const QgcMapProvider *AnimusMapCacheManager::findProvider(const QString &providerId) const
{
    for (const QgcMapProvider &provider : m_providers)
    {
        if (provider.id == providerId)
            return &provider;
    }
    return nullptr;
}

bool AnimusMapCacheManager::withDatabase(bool write,
                                         const std::function<bool(sqlite3 *)> &callback) const
{
    sqlite3 *database = nullptr;
    const int flags = (write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY) |
                      SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(cacheDatabasePath().toUtf8().constData(), &database, flags, nullptr) !=
        SQLITE_OK)
    {
        setError(QStringLiteral("Failed to open map cache database"));
        if (database)
            sqlite3_close(database);
        return false;
    }
    const bool ok = callback(database);
    if (!ok)
        setError(QStringLiteral("Map cache database operation failed"));
    sqlite3_close(database);
    return ok;
}

bool AnimusMapCacheManager::validBounds(double westDeg,
                                        double southDeg,
                                        double eastDeg,
                                        double northDeg)
{
    return std::isfinite(westDeg) && std::isfinite(southDeg) && std::isfinite(eastDeg) &&
           std::isfinite(northDeg) && westDeg >= -180.0 && eastDeg <= 180.0 &&
           westDeg < eastDeg && southDeg >= -MaxMercatorLatitudeDeg &&
           northDeg <= MaxMercatorLatitudeDeg && southDeg < northDeg;
}

int AnimusMapCacheManager::longitudeTile(double longitudeDeg, int zoom)
{
    const double tileCount = std::pow(2.0, zoom);
    const int tile = static_cast<int>(std::floor((longitudeDeg + 180.0) / 360.0 * tileCount));
    return std::clamp(tile, 0, static_cast<int>(tileCount) - 1);
}

int AnimusMapCacheManager::latitudeTile(double latitudeDeg, int zoom)
{
    const double clampedLatitude =
        std::clamp(latitudeDeg, -MaxMercatorLatitudeDeg, MaxMercatorLatitudeDeg);
    const double latitudeRad = clampedLatitude * Pi / 180.0;
    const double tileCount = std::pow(2.0, zoom);
    const int tile = static_cast<int>(std::floor(
        (1.0 - std::log(std::tan(latitudeRad) + 1.0 / std::cos(latitudeRad)) / Pi) / 2.0 *
        tileCount));
    return std::clamp(tile, 0, static_cast<int>(tileCount) - 1);
}

void AnimusMapCacheManager::setStatus(const QString &status, int progressPercent)
{
    m_status = status;
    m_progressPercent = std::clamp(progressPercent, 0, 100);
    emit statusChanged();
}

void AnimusMapCacheManager::setError(const QString &error) const
{
    m_lastError = error;
}

} // namespace animus
