#include "maps/qgc/AnimusMapCacheManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
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
constexpr int MaxConcurrentDownloads = 6;
constexpr int MaxTileRetries = 1;
constexpr double MaxMercatorLatitudeDeg = 85.05112878;
constexpr double AverageTileSizeBytes = 13652.0;
constexpr double Pi = 3.14159265358979323846;
constexpr double Cruise6DofWestDeg = -122.2607248;
constexpr double Cruise6DofSouthDeg = 37.3552151;
constexpr double Cruise6DofEastDeg = -122.0786752;
constexpr double Cruise6DofNorthDeg = 37.4997849;
constexpr int Cruise6DofMinZoom = 12;
constexpr int Cruise6DofMaxZoom = 15;
constexpr int TilePixelSize = 256;

const char *DefaultCruise6DofTileSetId = "cruise6dof-5mi-origin";
const char *DefaultCruise6DofTileSetName = "Cruise 6DOF 5mi Origin";
const char *DefaultCruise6DofProviderId = "osm-street";

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

bool execSqlOrDuplicateColumn(sqlite3 *database, const char *sql)
{
    char *message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    const QString text = message ? QString::fromUtf8(message).toLower() : QString();
    sqlite3_free(message);
    return result == SQLITE_OK || text.contains(QStringLiteral("duplicate column"));
}

QString sqliteText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QImage fixtureTileImage(int zoom, int x, int y)
{
    QImage image(TilePixelSize, TilePixelSize, QImage::Format_RGB32);
    const int redBase = 72 + ((x * 29 + zoom * 17) % 112);
    const int greenBase = 96 + ((y * 23 + zoom * 11) % 104);
    const int blueBase = 112 + ((x * 7 + y * 13) % 88);
    for (int row = 0; row < TilePixelSize; ++row)
    {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(row));
        for (int column = 0; column < TilePixelSize; ++column)
        {
            const bool grid = (row % 64) < 3 || (column % 64) < 3;
            const bool diagonal = ((row + column + x + y) % 53) < 4;
            const int shade = grid ? 42 : (diagonal ? 24 : ((row ^ column) & 15));
            line[column] = qRgb(qBound(0, redBase + shade, 255),
                                qBound(0, greenBase + shade / 2, 255),
                                qBound(0, blueBase - shade / 3, 255));
        }
    }
    return image;
}

} // namespace

AnimusMapCacheManager::AnimusMapCacheManager(QObject *parent)
    : QAbstractListModel(parent), m_rootPath(QStringLiteral("map_cache")),
      m_activeProviderId(QStringLiteral("offline-cache")),
      m_status(QStringLiteral("Cache not initialized")), m_progressPercent(0)
{
    const QString operatorUrl = configuredOperatorUrl();
    m_providers.push_back(
        {QStringLiteral("osm-street"),
         QStringLiteral("OpenStreetMap"),
         QStringLiteral("street"),
         QStringLiteral("Street"),
         QStringLiteral("QGroundControl"),
         QStringLiteral("OpenStreetMap contributors, CARTO"),
         QStringLiteral("https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png"),
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
    return parent.isValid() ? 0 : m_providers.size();
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
    return {{ProviderIdRole, "providerId"},
            {LabelRole, "label"},
            {TypeIdRole, "typeId"},
            {TypeLabelRole, "typeLabel"},
            {PluginNameRole, "pluginName"},
            {AttributionRole, "attribution"},
            {NetworkRequiredRole, "networkRequired"},
            {OperatorConfiguredRole, "operatorConfigured"}};
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

int AnimusMapCacheManager::cachedTileCount() const
{
    return m_cachedTileCount;
}

int AnimusMapCacheManager::missingTileCount() const
{
    return m_missingTileCount;
}

int AnimusMapCacheManager::failedTileCount() const
{
    return m_failedTileCount;
}

int AnimusMapCacheManager::inFlightTileCount() const
{
    return m_inFlightTileCount;
}

int AnimusMapCacheManager::totalTileCount() const
{
    return m_totalTileCount;
}

QString AnimusMapCacheManager::providerIdAt(int row) const
{
    return row >= 0 && row < m_providers.size() ? m_providers.at(row).id : QString();
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
    if (!root.mkpath(QStringLiteral("tiles")))
    {
        setError(QStringLiteral("Failed to create map tile cache root"));
        return false;
    }

    const bool ok = withDatabase(
        true,
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
                           "last_error TEXT NOT NULL DEFAULT '',"
                           "created_utc TEXT NOT NULL,"
                           "updated_utc TEXT NOT NULL);") &&
                   execSql(database,
                           "CREATE TABLE IF NOT EXISTS tile_set_tiles ("
                           "tile_set_id TEXT NOT NULL,"
                           "provider_id TEXT NOT NULL,"
                           "zoom INTEGER NOT NULL,"
                           "x INTEGER NOT NULL,"
                           "y INTEGER NOT NULL,"
                           "state TEXT NOT NULL,"
                           "path TEXT NOT NULL,"
                           "last_error TEXT NOT NULL DEFAULT '',"
                           "retry_count INTEGER NOT NULL DEFAULT 0,"
                           "updated_utc TEXT NOT NULL,"
                           "PRIMARY KEY(tile_set_id,provider_id,zoom,x,y));") &&
                   execSql(database,
                           "CREATE INDEX IF NOT EXISTS idx_tile_set_tiles_lookup "
                           "ON tile_set_tiles(provider_id,zoom,x,y,state);") &&
                   execSqlOrDuplicateColumn(
                       database,
                       "ALTER TABLE tile_sets ADD COLUMN last_error TEXT NOT NULL DEFAULT '';") &&
                   execSqlOrDuplicateColumn(
                       database,
                       "ALTER TABLE tile_sets ADD COLUMN updated_utc TEXT NOT NULL DEFAULT '';") &&
                   execSqlOrDuplicateColumn(database,
                                            "ALTER TABLE tile_set_tiles ADD COLUMN state TEXT NOT "
                                            "NULL DEFAULT 'missing';") &&
                   execSqlOrDuplicateColumn(
                       database,
                       "ALTER TABLE tile_set_tiles ADD COLUMN last_error TEXT NOT NULL DEFAULT "
                       "'';") &&
                   execSqlOrDuplicateColumn(database,
                                            "ALTER TABLE tile_set_tiles ADD COLUMN retry_count "
                                            "INTEGER NOT NULL DEFAULT 0;");
        });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Cache ready"), 0);
    return reloadTileSets();
}

bool AnimusMapCacheManager::ensureDefaultCruise6DofTileSet()
{
    if (!initializeCache())
        return false;

    const int tileCount = estimateTileCount(Cruise6DofWestDeg,
                                            Cruise6DofSouthDeg,
                                            Cruise6DofEastDeg,
                                            Cruise6DofNorthDeg,
                                            Cruise6DofMinZoom,
                                            Cruise6DofMaxZoom);
    const QString id = QString::fromLatin1(DefaultCruise6DofTileSetId);
    const QString name = QString::fromLatin1(DefaultCruise6DofTileSetName);
    const QString providerId = QString::fromLatin1(DefaultCruise6DofProviderId);
    const QString now = utcNow();
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "INSERT INTO tile_sets "
                "(id,name,provider_id,min_zoom,max_zoom,west,south,east,north,tile_count,status,"
                "last_error,created_utc,updated_utc) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET "
                "name=excluded.name,provider_id=excluded.provider_id,min_zoom=excluded.min_zoom,"
                "max_zoom=excluded.max_zoom,west=excluded.west,south=excluded.south,"
                "east=excluded.east,north=excluded.north,tile_count=excluded.tile_count,"
                "updated_utc=excluded.updated_utc";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, id.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 3, providerId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(statement, 4, Cruise6DofMinZoom);
            sqlite3_bind_int(statement, 5, Cruise6DofMaxZoom);
            sqlite3_bind_double(statement, 6, Cruise6DofWestDeg);
            sqlite3_bind_double(statement, 7, Cruise6DofSouthDeg);
            sqlite3_bind_double(statement, 8, Cruise6DofEastDeg);
            sqlite3_bind_double(statement, 9, Cruise6DofNorthDeg);
            sqlite3_bind_int(statement, 10, tileCount);
            sqlite3_bind_text(statement, 11, "empty", -1, SQLITE_STATIC);
            sqlite3_bind_text(statement, 12, "", -1, SQLITE_STATIC);
            sqlite3_bind_text(statement, 13, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 14, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    return ok && seedTileInventory(id) && reloadTileSets();
}

bool AnimusMapCacheManager::seedDefaultCruise6DofFixtureTiles()
{
    if (!ensureDefaultCruise6DofTileSet())
        return false;

    const QString providerId = QString::fromLatin1(DefaultCruise6DofProviderId);
    bool ok = true;
    for (int zoom = Cruise6DofMinZoom; zoom <= Cruise6DofMaxZoom && ok; ++zoom)
    {
        const int minX = longitudeTile(Cruise6DofWestDeg, zoom);
        const int maxX = longitudeTile(Cruise6DofEastDeg, zoom);
        const int minY = latitudeTile(Cruise6DofNorthDeg, zoom);
        const int maxY = latitudeTile(Cruise6DofSouthDeg, zoom);
        for (int x = minX; x <= maxX && ok; ++x)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                const QString path = providerTilePath(providerId, zoom, x, y);
                if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath()))
                {
                    ok = false;
                    break;
                }
                if (!fixtureTileImage(zoom, x, y).save(path, "PNG"))
                {
                    ok = false;
                    break;
                }
            }
        }
    }

    if (!ok)
    {
        setError(QStringLiteral("Failed to seed deterministic capture tile fixture"));
        return false;
    }
    const QString id = QString::fromLatin1(DefaultCruise6DofTileSetId);
    return seedTileInventory(id) && reloadTileSets();
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
        return 0;

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

QString AnimusMapCacheManager::tileUrlFor(
    const QString &providerId, int zoom, int x, int y, bool networkAllowed) const
{
    Q_UNUSED(networkAllowed)
    const QString cachedPath = providerId == QStringLiteral("offline-cache")
                                   ? offlineCachedTilePath(zoom, x, y)
                                   : providerTilePath(providerId, zoom, x, y);
    if (!cachedPath.isEmpty() && QFileInfo::exists(cachedPath))
        return QUrl::fromLocalFile(cachedPath).toString();
    return QString();
}

QString
AnimusMapCacheManager::cachedTilePathFor(const QString &providerId, int zoom, int x, int y) const
{
    return providerId == QStringLiteral("offline-cache") ? offlineCachedTilePath(zoom, x, y)
                                                         : providerTilePath(providerId, zoom, x, y);
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
        (trimmedName + QString::number(QDateTime::currentMSecsSinceEpoch()) +
         QString::number(reinterpret_cast<quintptr>(this)))
            .toUtf8();
    const QString id = QString::fromLatin1(
        QCryptographicHash::hash(hashInput, QCryptographicHash::Sha1).toHex().left(16));
    const QString providerId = m_activeProviderId == QStringLiteral("offline-cache")
                                   ? QStringLiteral("osm-street")
                                   : m_activeProviderId;
    const QString now = utcNow();
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "INSERT INTO tile_sets "
                "(id,name,provider_id,min_zoom,max_zoom,west,south,east,north,tile_count,status,"
                "last_error,created_utc,updated_utc) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
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
            sqlite3_bind_text(statement, 12, "", -1, SQLITE_STATIC);
            sqlite3_bind_text(statement, 13, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 14, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Tile set queued"), 0);
    return seedTileInventory(id) && reloadTileSets();
}

bool AnimusMapCacheManager::downloadTileSet(const QString &tileSetId)
{
    if (!initializeCache())
        return false;
    const QVariantMap tileSet = tileSetById(tileSetId);
    if (tileSet.isEmpty())
    {
        setError(QStringLiteral("Unknown tile set"));
        return false;
    }
    const QString providerId = tileSet.value(QStringLiteral("providerId")).toString();
    const QgcMapProvider *provider = findProvider(providerId);
    if (!provider || provider->urlTemplate.isEmpty() || !provider->operatorConfigured)
    {
        setError(QStringLiteral("Tile set provider is not configured for downloads"));
        return false;
    }

    m_canceledDownloads.remove(tileSetId);
    QVector<TileCoord> missing = missingTilesForDownload(tileSetId);
    if (missing.isEmpty())
    {
        updateTileSetStatus(tileSetId, QStringLiteral("complete"));
        setStatus(QStringLiteral("No missing tiles"), 100);
        return reloadTileSets();
    }

    for (const TileCoord &tile : missing)
        m_downloadQueue.enqueue(tile);
    updateTileSetStatus(tileSetId, QStringLiteral("downloading"));
    setStatus(QStringLiteral("Queued %1 tile downloads").arg(missing.size()), 0);
    startQueuedDownloads();
    return reloadTileSets();
}

bool AnimusMapCacheManager::cancelTileSetDownload(const QString &tileSetId)
{
    m_canceledDownloads.insert(tileSetId);
    QQueue<TileCoord> retained;
    while (!m_downloadQueue.isEmpty())
    {
        const TileCoord tile = m_downloadQueue.dequeue();
        if (tile.tileSetId != tileSetId)
            retained.enqueue(tile);
    }
    m_downloadQueue = retained;
    for (auto it = m_activeDownloads.cbegin(); it != m_activeDownloads.cend(); ++it)
    {
        if (it.value().tileSetId == tileSetId)
            it.key()->abort();
    }
    updateTileSetStatus(tileSetId, QStringLiteral("canceled"));
    setStatus(QStringLiteral("Tile set download canceled"), 0);
    return reloadTileSets();
}

bool AnimusMapCacheManager::deleteTileSet(const QString &tileSetId)
{
    if (!initializeCache())
        return false;
    cancelTileSetDownload(tileSetId);
    removeTileSetFiles(tileSetId);
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(database,
                                   "DELETE FROM tile_set_tiles WHERE tile_set_id = ?",
                                   -1,
                                   &statement,
                                   nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            if (!stepOk)
                return false;
            if (sqlite3_prepare_v2(
                    database, "DELETE FROM tile_sets WHERE id = ?", -1, &statement, nullptr) !=
                SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    if (!ok)
        return false;
    setStatus(QStringLiteral("Tile set deleted"), 0);
    return reloadTileSets();
}

bool AnimusMapCacheManager::exportTileSet(const QString &tileSetId, const QString &path) const
{
    const QVariantMap tileSet = tileSetById(tileSetId);
    if (tileSet.isEmpty())
    {
        setError(QStringLiteral("Unknown tile set"));
        return false;
    }
    QDir exportRoot(path);
    if (!exportRoot.exists() && !exportRoot.mkpath(QStringLiteral(".")))
    {
        setError(QStringLiteral("Failed to create tile-set export directory"));
        return false;
    }

    QJsonObject manifest = QJsonObject::fromVariantMap(tileSet);
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("format"), QStringLiteral("animus-qgc-tile-set-directory"));
    QJsonArray tiles;
    bool ok = withDatabase(
        false,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql = "SELECT provider_id,zoom,x,y,path FROM tile_set_tiles "
                              "WHERE tile_set_id = ? AND state = 'available' ORDER BY zoom,x,y";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            bool stepOk = true;
            while (sqlite3_step(statement) == SQLITE_ROW)
            {
                const QString providerId = sqliteText(statement, 0);
                const int zoom = sqlite3_column_int(statement, 1);
                const int x = sqlite3_column_int(statement, 2);
                const int y = sqlite3_column_int(statement, 3);
                const QString sourcePath = sqliteText(statement, 4);
                const QString relativePath = QStringLiteral("tiles/%1/%2/%3/%4.png")
                                                 .arg(providerId,
                                                      QString::number(zoom),
                                                      QString::number(x),
                                                      QString::number(y));
                if (!copyFileAtomic(sourcePath, exportRoot.filePath(relativePath)))
                {
                    stepOk = false;
                    break;
                }
                QJsonObject tile;
                tile.insert(QStringLiteral("providerId"), providerId);
                tile.insert(QStringLiteral("zoom"), zoom);
                tile.insert(QStringLiteral("x"), x);
                tile.insert(QStringLiteral("y"), y);
                tile.insert(QStringLiteral("path"), relativePath);
                tiles.push_back(tile);
            }
            sqlite3_finalize(statement);
            return stepOk;
        });
    if (!ok)
        return false;
    manifest.insert(QStringLiteral("tiles"), tiles);

    QSaveFile manifestFile(exportRoot.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly))
    {
        setError(QStringLiteral("Failed to open tile-set manifest for export"));
        return false;
    }
    manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    return manifestFile.commit();
}

bool AnimusMapCacheManager::importTileSet(const QString &path)
{
    if (!initializeCache())
        return false;
    QDir importRoot(path);
    QFile manifestFile(importRoot.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly))
    {
        setError(QStringLiteral("Failed to open tile-set import manifest"));
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll());
    if (!document.isObject())
    {
        setError(QStringLiteral("Tile set import manifest must be a JSON object"));
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).toString() !=
        QStringLiteral("animus-qgc-tile-set-directory"))
    {
        setError(QStringLiteral("Tile set import format is unsupported"));
        return false;
    }

    const QString name = object.value(QStringLiteral("name")).toString();
    if (!createTileSet(name,
                       object.value(QStringLiteral("west")).toDouble(),
                       object.value(QStringLiteral("south")).toDouble(),
                       object.value(QStringLiteral("east")).toDouble(),
                       object.value(QStringLiteral("north")).toDouble(),
                       object.value(QStringLiteral("minZoom")).toInt(),
                       object.value(QStringLiteral("maxZoom")).toInt()))
        return false;

    const QString importedId =
        m_tileSets.constFirst().toMap().value(QStringLiteral("id")).toString();
    const QJsonArray tiles = object.value(QStringLiteral("tiles")).toArray();
    for (const QJsonValue &value : tiles)
    {
        const QJsonObject tile = value.toObject();
        const QString providerId = tile.value(QStringLiteral("providerId")).toString();
        const int zoom = tile.value(QStringLiteral("zoom")).toInt();
        const int x = tile.value(QStringLiteral("x")).toInt();
        const int y = tile.value(QStringLiteral("y")).toInt();
        const QString destination = providerTilePath(providerId, zoom, x, y);
        if (!copyFileAtomic(importRoot.filePath(tile.value(QStringLiteral("path")).toString()),
                            destination))
            return false;
        TileCoord coord{importedId, providerId, zoom, x, y, 0};
        updateTileState(coord, QStringLiteral("available"), destination, QString(), 0);
    }
    return reloadTileSets();
}

bool AnimusMapCacheManager::reloadTileSets()
{
    QVariantList loaded;
    if (!QFileInfo::exists(cacheDatabasePath()))
    {
        m_tileSets = loaded;
        refreshAggregateCounts();
        emit tileSetsChanged();
        return true;
    }

    const bool ok = withDatabase(
        false,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "SELECT ts.id,ts.name,ts.provider_id,ts.min_zoom,ts.max_zoom,ts.west,ts.south,"
                "ts.east,ts.north,ts.tile_count,ts.status,ts.last_error,ts.created_utc,"
                "ts.updated_utc,"
                "SUM(CASE WHEN tst.state = 'available' THEN 1 ELSE 0 END),"
                "SUM(CASE WHEN tst.state = 'missing' THEN 1 ELSE 0 END),"
                "SUM(CASE WHEN tst.state = 'failed' THEN 1 ELSE 0 END),"
                "SUM(CASE WHEN tst.state IN ('queued','downloading') THEN 1 ELSE 0 END) "
                "FROM tile_sets ts LEFT JOIN tile_set_tiles tst ON ts.id = tst.tile_set_id "
                "GROUP BY ts.id ORDER BY ts.created_utc DESC";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            while (sqlite3_step(statement) == SQLITE_ROW)
            {
                const int cached = sqlite3_column_int(statement, 14);
                const int missing = sqlite3_column_int(statement, 15);
                const int failed = sqlite3_column_int(statement, 16);
                const int inFlight = sqlite3_column_int(statement, 17);
                const int total = sqlite3_column_int(statement, 9);
                QString status = sqliteText(statement, 10);
                if (status != QStringLiteral("downloading") && status != QStringLiteral("queued") &&
                    status != QStringLiteral("canceled") && status != QStringLiteral("failed"))
                {
                    if (cached == 0)
                        status = QStringLiteral("empty");
                    else if (cached == total && total > 0)
                        status = QStringLiteral("complete");
                    else
                        status = QStringLiteral("partial");
                }
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
                tileSet.insert(QStringLiteral("tileCount"), total);
                tileSet.insert(QStringLiteral("status"), status);
                tileSet.insert(QStringLiteral("lastError"), sqliteText(statement, 11));
                tileSet.insert(QStringLiteral("createdUtc"), sqliteText(statement, 12));
                tileSet.insert(QStringLiteral("updatedUtc"), sqliteText(statement, 13));
                tileSet.insert(QStringLiteral("cachedCount"), cached);
                tileSet.insert(QStringLiteral("missingCount"), missing);
                tileSet.insert(QStringLiteral("failedCount"), failed);
                tileSet.insert(QStringLiteral("inFlightCount"), inFlight);
                loaded.push_back(tileSet);
            }
            sqlite3_finalize(statement);
            return true;
        });
    if (!ok)
        return false;
    m_tileSets = loaded;
    refreshAggregateCounts();
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

bool AnimusMapCacheManager::seedTileInventory(const QString &tileSetId)
{
    const QVariantMap tileSet = tileSetById(tileSetId);
    if (tileSet.isEmpty())
        return false;
    const QString providerId = tileSet.value(QStringLiteral("providerId")).toString();
    const int minZoom = tileSet.value(QStringLiteral("minZoom")).toInt();
    const int maxZoom = tileSet.value(QStringLiteral("maxZoom")).toInt();
    const double west = tileSet.value(QStringLiteral("west")).toDouble();
    const double south = tileSet.value(QStringLiteral("south")).toDouble();
    const double east = tileSet.value(QStringLiteral("east")).toDouble();
    const double north = tileSet.value(QStringLiteral("north")).toDouble();
    const QString now = utcNow();

    int cached = 0;
    int total = 0;
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            if (!execSql(database, "BEGIN TRANSACTION"))
                return false;
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "INSERT INTO tile_set_tiles "
                "(tile_set_id,provider_id,zoom,x,y,state,path,last_error,retry_count,updated_utc) "
                "VALUES (?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(tile_set_id,provider_id,zoom,x,y) DO UPDATE SET "
                "state=excluded.state,path=excluded.path,updated_utc=excluded.updated_utc";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            bool stepOk = true;
            for (int zoom = minZoom; zoom <= maxZoom && stepOk; ++zoom)
            {
                const int minX = longitudeTile(west, zoom);
                const int maxX = longitudeTile(east, zoom);
                const int minY = latitudeTile(north, zoom);
                const int maxY = latitudeTile(south, zoom);
                for (int x = minX; x <= maxX && stepOk; ++x)
                {
                    for (int y = minY; y <= maxY; ++y)
                    {
                        const QString path = providerTilePath(providerId, zoom, x, y);
                        const bool available = QFileInfo::exists(path);
                        ++total;
                        cached += available ? 1 : 0;
                        sqlite3_reset(statement);
                        sqlite3_clear_bindings(statement);
                        sqlite3_bind_text(
                            statement, 1, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(
                            statement, 2, providerId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(statement, 3, zoom);
                        sqlite3_bind_int(statement, 4, x);
                        sqlite3_bind_int(statement, 5, y);
                        sqlite3_bind_text(
                            statement, 6, available ? "available" : "missing", -1, SQLITE_STATIC);
                        sqlite3_bind_text(
                            statement, 7, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(statement, 8, "", -1, SQLITE_STATIC);
                        sqlite3_bind_int(statement, 9, 0);
                        sqlite3_bind_text(
                            statement, 10, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        if (sqlite3_step(statement) != SQLITE_DONE)
                        {
                            stepOk = false;
                            break;
                        }
                    }
                }
            }
            sqlite3_finalize(statement);
            const QString status = cached == 0 ? QStringLiteral("empty")
                                               : (cached == total ? QStringLiteral("complete")
                                                                  : QStringLiteral("partial"));
            if (stepOk)
            {
                sqlite3_stmt *update = nullptr;
                if (sqlite3_prepare_v2(database,
                                       "UPDATE tile_sets SET status = ?, updated_utc = ? WHERE "
                                       "id = ?",
                                       -1,
                                       &update,
                                       nullptr) != SQLITE_OK)
                {
                    stepOk = false;
                }
                else
                {
                    sqlite3_bind_text(update, 1, status.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(update, 2, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(
                        update, 3, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    stepOk = sqlite3_step(update) == SQLITE_DONE;
                    sqlite3_finalize(update);
                }
            }
            if (!stepOk)
            {
                execSql(database, "ROLLBACK");
                return false;
            }
            return execSql(database, "COMMIT");
        });
    if (ok)
    {
        const int percent = total > 0 ? static_cast<int>(std::round(100.0 * cached / total)) : 0;
        setStatus(QStringLiteral("%1/%2 cached tiles").arg(cached).arg(total), percent);
    }
    return ok;
}

bool AnimusMapCacheManager::updateTileState(const TileCoord &tile,
                                            const QString &state,
                                            const QString &path,
                                            const QString &lastError,
                                            int retryCount)
{
    const QString now = utcNow();
    const bool ok = withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "UPDATE tile_set_tiles SET state = ?, path = ?, last_error = ?, retry_count = ?, "
                "updated_utc = ? WHERE tile_set_id = ? AND provider_id = ? AND zoom = ? AND x = ? "
                "AND y = ?";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, state.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, path.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 3, lastError.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(statement, 4, retryCount);
            sqlite3_bind_text(statement, 5, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(
                statement, 6, tile.tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(
                statement, 7, tile.providerId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(statement, 8, tile.zoom);
            sqlite3_bind_int(statement, 9, tile.x);
            sqlite3_bind_int(statement, 10, tile.y);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
    reloadTileSets();
    return ok;
}

bool AnimusMapCacheManager::updateTileSetStatus(const QString &tileSetId, const QString &status)
{
    const QString now = utcNow();
    return withDatabase(
        true,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            if (sqlite3_prepare_v2(database,
                                   "UPDATE tile_sets SET status = ?, updated_utc = ? WHERE id = ?",
                                   -1,
                                   &statement,
                                   nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, status.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, now.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 3, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            const bool stepOk = sqlite3_step(statement) == SQLITE_DONE;
            sqlite3_finalize(statement);
            return stepOk;
        });
}

QVariantMap AnimusMapCacheManager::tileSetById(const QString &tileSetId) const
{
    for (const QVariant &entry : m_tileSets)
    {
        const QVariantMap tileSet = entry.toMap();
        if (tileSet.value(QStringLiteral("id")).toString() == tileSetId)
            return tileSet;
    }
    const_cast<AnimusMapCacheManager *>(this)->reloadTileSets();
    for (const QVariant &entry : m_tileSets)
    {
        const QVariantMap tileSet = entry.toMap();
        if (tileSet.value(QStringLiteral("id")).toString() == tileSetId)
            return tileSet;
    }
    return QVariantMap();
}

QVector<AnimusMapCacheManager::TileCoord>
AnimusMapCacheManager::missingTilesForDownload(const QString &tileSetId) const
{
    QVector<TileCoord> tiles;
    withDatabase(false,
                 [&](sqlite3 *database)
                 {
                     sqlite3_stmt *statement = nullptr;
                     const char *sql =
                         "SELECT provider_id,zoom,x,y,retry_count FROM tile_set_tiles "
                         "WHERE tile_set_id = ? AND state IN ('queued','missing','failed') "
                         "ORDER BY zoom,x,y";
                     if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                         return false;
                     while (sqlite3_step(statement) == SQLITE_ROW)
                     {
                         TileCoord tile;
                         tile.tileSetId = tileSetId;
                         tile.providerId = sqliteText(statement, 0);
                         tile.zoom = sqlite3_column_int(statement, 1);
                         tile.x = sqlite3_column_int(statement, 2);
                         tile.y = sqlite3_column_int(statement, 3);
                         tile.retryCount = sqlite3_column_int(statement, 4);
                         tiles.push_back(tile);
                     }
                     sqlite3_finalize(statement);
                     return true;
                 });
    return tiles;
}

QString
AnimusMapCacheManager::providerTileUrl(const QString &providerId, int zoom, int x, int y) const
{
    const QgcMapProvider *provider = findProvider(providerId);
    if (!provider || provider->urlTemplate.isEmpty())
        return QString();
    QString url = provider->urlTemplate;
    url.replace(QStringLiteral("{z}"), QString::number(zoom));
    url.replace(QStringLiteral("{x}"), QString::number(x));
    url.replace(QStringLiteral("{y}"), QString::number(y));
    return url;
}

QString
AnimusMapCacheManager::providerTilePath(const QString &providerId, int zoom, int x, int y) const
{
    if (zoom < MinMapZoom || zoom > MaxMapZoom || x < 0 || y < 0)
        return QString();
    return QDir(m_rootPath)
        .filePath(
            QStringLiteral("tiles/%1/%2/%3/%4.png")
                .arg(providerId, QString::number(zoom), QString::number(x), QString::number(y)));
}

QString AnimusMapCacheManager::offlineCachedTilePath(int zoom, int x, int y) const
{
    for (const QgcMapProvider &provider : m_providers)
    {
        if (provider.id == QStringLiteral("offline-cache"))
            continue;
        const QString path = providerTilePath(provider.id, zoom, x, y);
        if (QFileInfo::exists(path))
            return path;
    }
    return QString();
}

bool AnimusMapCacheManager::removeTileSetFiles(const QString &tileSetId) const
{
    QStringList paths;
    withDatabase(
        false,
        [&](sqlite3 *database)
        {
            sqlite3_stmt *statement = nullptr;
            const char *sql =
                "SELECT path FROM tile_set_tiles owned "
                "WHERE owned.tile_set_id = ? AND owned.state = 'available' AND NOT EXISTS ("
                "SELECT 1 FROM tile_set_tiles other WHERE other.tile_set_id != owned.tile_set_id "
                "AND other.provider_id = owned.provider_id AND other.zoom = owned.zoom AND "
                "other.x = owned.x AND other.y = owned.y AND other.state = 'available')";
            if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_text(statement, 1, tileSetId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(statement) == SQLITE_ROW)
                paths.push_back(sqliteText(statement, 0));
            sqlite3_finalize(statement);
            return true;
        });
    for (const QString &path : paths)
    {
        if (!path.isEmpty())
            QFile::remove(path);
    }
    return true;
}

bool AnimusMapCacheManager::copyFileAtomic(const QString &sourcePath,
                                           const QString &destinationPath) const
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        setError(QStringLiteral("Failed to open tile source file"));
        return false;
    }
    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly))
    {
        setError(QStringLiteral("Failed to open tile destination file"));
        return false;
    }
    destination.write(source.readAll());
    return destination.commit();
}

void AnimusMapCacheManager::startQueuedDownloads()
{
    while (!m_downloadQueue.isEmpty() && m_activeDownloads.size() < MaxConcurrentDownloads)
    {
        TileCoord tile = m_downloadQueue.dequeue();
        if (m_canceledDownloads.contains(tile.tileSetId))
            continue;
        const QString url = providerTileUrl(tile.providerId, tile.zoom, tile.x, tile.y);
        if (url.isEmpty())
        {
            failDownload(tile, QStringLiteral("Provider URL is not configured"));
            continue;
        }
        updateTileState(tile,
                        QStringLiteral("downloading"),
                        providerTilePath(tile.providerId, tile.zoom, tile.x, tile.y),
                        QString(),
                        tile.retryCount);
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Altair Animus Qt offline map cache"));
        QNetworkReply *reply = m_network.get(request);
        m_activeDownloads.insert(reply, tile);
        connect(reply,
                &QNetworkReply::finished,
                this,
                [this, reply]() { handleDownloadFinished(reply); });
    }
    m_inFlightTileCount = m_activeDownloads.size();
    emit tileSetsChanged();
}

void AnimusMapCacheManager::handleDownloadFinished(QNetworkReply *reply)
{
    const TileCoord tile = m_activeDownloads.take(reply);
    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool canceled = m_canceledDownloads.contains(tile.tileSetId);
    const bool ok = reply->error() == QNetworkReply::NoError && httpStatus >= 200 &&
                    httpStatus < 300 && !data.isEmpty();
    const QString error = reply->errorString();
    reply->deleteLater();

    if (canceled)
        updateTileState(tile,
                        QStringLiteral("missing"),
                        providerTilePath(tile.providerId, tile.zoom, tile.x, tile.y),
                        QString(),
                        tile.retryCount);
    else if (ok)
        finishDownload(tile, data);
    else if (tile.retryCount < MaxTileRetries)
    {
        TileCoord retry = tile;
        retry.retryCount += 1;
        updateTileState(retry,
                        QStringLiteral("queued"),
                        providerTilePath(tile.providerId, tile.zoom, tile.x, tile.y),
                        error,
                        retry.retryCount);
        m_downloadQueue.enqueue(retry);
    }
    else
        failDownload(tile, error.isEmpty() ? QStringLiteral("Tile download failed") : error);

    if (m_activeDownloads.isEmpty() && m_downloadQueue.isEmpty())
    {
        const QVariantMap tileSet = tileSetById(tile.tileSetId);
        const int cached = tileSet.value(QStringLiteral("cachedCount")).toInt();
        const int total = tileSet.value(QStringLiteral("tileCount")).toInt();
        const int failed = tileSet.value(QStringLiteral("failedCount")).toInt();
        const QString status =
            m_canceledDownloads.contains(tile.tileSetId)
                ? QStringLiteral("canceled")
                : (failed > 0 ? QStringLiteral("failed")
                              : (cached == total ? QStringLiteral("complete")
                                                 : (cached > 0 ? QStringLiteral("partial")
                                                               : QStringLiteral("empty"))));
        updateTileSetStatus(tile.tileSetId, status);
        setStatus(QStringLiteral("%1/%2 cached tiles").arg(cached).arg(total),
                  total > 0 ? static_cast<int>(std::round(100.0 * cached / total)) : 0);
    }
    startQueuedDownloads();
    reloadTileSets();
}

void AnimusMapCacheManager::finishDownload(const TileCoord &tile, const QByteArray &data)
{
    const QString path = providerTilePath(tile.providerId, tile.zoom, tile.x, tile.y);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        failDownload(tile, QStringLiteral("Failed to write tile atomically"));
        return;
    }
    updateTileState(tile, QStringLiteral("available"), path, QString(), tile.retryCount);
}

void AnimusMapCacheManager::failDownload(const TileCoord &tile, const QString &error)
{
    updateTileState(tile,
                    QStringLiteral("failed"),
                    providerTilePath(tile.providerId, tile.zoom, tile.x, tile.y),
                    error,
                    tile.retryCount);
}

void AnimusMapCacheManager::refreshAggregateCounts()
{
    m_cachedTileCount = 0;
    m_missingTileCount = 0;
    m_failedTileCount = 0;
    m_inFlightTileCount = m_activeDownloads.size();
    m_totalTileCount = 0;
    for (const QVariant &entry : m_tileSets)
    {
        const QVariantMap tileSet = entry.toMap();
        m_cachedTileCount += tileSet.value(QStringLiteral("cachedCount")).toInt();
        m_missingTileCount += tileSet.value(QStringLiteral("missingCount")).toInt();
        m_failedTileCount += tileSet.value(QStringLiteral("failedCount")).toInt();
        m_inFlightTileCount += tileSet.value(QStringLiteral("inFlightCount")).toInt();
        m_totalTileCount += tileSet.value(QStringLiteral("tileCount")).toInt();
    }
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
           std::isfinite(northDeg) && westDeg >= -180.0 && eastDeg <= 180.0 && westDeg < eastDeg &&
           southDeg >= -MaxMercatorLatitudeDeg && northDeg <= MaxMercatorLatitudeDeg &&
           southDeg < northDeg;
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
    const int tile = static_cast<int>(
        std::floor((1.0 - std::log(std::tan(latitudeRad) + 1.0 / std::cos(latitudeRad)) / Pi) /
                   2.0 * tileCount));
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
