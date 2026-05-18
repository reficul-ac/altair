#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QVariantList>
#include <QVector>

#include <functional>

struct sqlite3;

namespace animus
{

struct QgcMapProvider
{
    QString id;
    QString label;
    QString typeId;
    QString typeLabel;
    QString pluginName;
    QString attribution;
    QString urlTemplate;
    bool networkRequired;
    bool operatorConfigured;
};

class AnimusMapCacheManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY cacheChanged)
    Q_PROPERTY(QString cacheDatabasePath READ cacheDatabasePath NOTIFY cacheChanged)
    Q_PROPERTY(QString activeProviderId READ activeProviderId WRITE setActiveProviderId NOTIFY
                   activeProviderChanged)
    Q_PROPERTY(QString activeMapTypeId READ activeMapTypeId NOTIFY activeProviderChanged)
    Q_PROPERTY(QString activePluginName READ activePluginName NOTIFY activeProviderChanged)
    Q_PROPERTY(QString activeAttribution READ activeAttribution NOTIFY activeProviderChanged)
    Q_PROPERTY(QString activeStatus READ activeStatus NOTIFY statusChanged)
    Q_PROPERTY(QVariantList tileSets READ tileSets NOTIFY tileSetsChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY statusChanged)
    Q_PROPERTY(int cachedTileCount READ cachedTileCount NOTIFY tileSetsChanged)
    Q_PROPERTY(int missingTileCount READ missingTileCount NOTIFY tileSetsChanged)
    Q_PROPERTY(int failedTileCount READ failedTileCount NOTIFY tileSetsChanged)
    Q_PROPERTY(int inFlightTileCount READ inFlightTileCount NOTIFY tileSetsChanged)
    Q_PROPERTY(int totalTileCount READ totalTileCount NOTIFY tileSetsChanged)

  public:
    enum Roles
    {
        ProviderIdRole = Qt::UserRole + 1,
        LabelRole,
        TypeIdRole,
        TypeLabelRole,
        PluginNameRole,
        AttributionRole,
        NetworkRequiredRole,
        OperatorConfiguredRole
    };
    Q_ENUM(Roles)

    explicit AnimusMapCacheManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const;
    void setRootPath(const QString &rootPath);
    QString cacheDatabasePath() const;

    QString activeProviderId() const;
    void setActiveProviderId(const QString &providerId);
    QString activeMapTypeId() const;
    QString activePluginName() const;
    QString activeAttribution() const;
    QString activeStatus() const;
    QVariantList tileSets() const;
    int progressPercent() const;
    int cachedTileCount() const;
    int missingTileCount() const;
    int failedTileCount() const;
    int inFlightTileCount() const;
    int totalTileCount() const;

    Q_INVOKABLE QString providerIdAt(int row) const;
    Q_INVOKABLE int providerIndex(const QString &providerId) const;
    Q_INVOKABLE bool providerRequiresNetwork(const QString &providerId) const;
    Q_INVOKABLE bool providerConfigured(const QString &providerId) const;
    Q_INVOKABLE QString providerBlockReason(const QString &providerId, bool networkAllowed) const;
    Q_INVOKABLE bool initializeCache();
    Q_INVOKABLE bool ensureDefaultCruise6DofTileSet();
    Q_INVOKABLE int estimateTileCount(double westDeg,
                                      double southDeg,
                                      double eastDeg,
                                      double northDeg,
                                      int minZoom,
                                      int maxZoom) const;
    Q_INVOKABLE double estimateSizeMb(int tileCount) const;
    Q_INVOKABLE QString
    tileUrlFor(const QString &providerId, int zoom, int x, int y, bool networkAllowed) const;
    Q_INVOKABLE QString cachedTilePathFor(const QString &providerId, int zoom, int x, int y) const;
    Q_INVOKABLE bool createTileSet(const QString &name,
                                   double westDeg,
                                   double southDeg,
                                   double eastDeg,
                                   double northDeg,
                                   int minZoom,
                                   int maxZoom);
    Q_INVOKABLE bool downloadTileSet(const QString &tileSetId);
    Q_INVOKABLE bool cancelTileSetDownload(const QString &tileSetId);
    Q_INVOKABLE bool deleteTileSet(const QString &tileSetId);
    Q_INVOKABLE bool exportTileSet(const QString &tileSetId, const QString &path) const;
    Q_INVOKABLE bool importTileSet(const QString &path);
    Q_INVOKABLE bool reloadTileSets();
    Q_INVOKABLE QString lastError() const;

  signals:
    void cacheChanged();
    void activeProviderChanged();
    void statusChanged();
    void tileSetsChanged();

  private:
    struct TileCoord
    {
        QString tileSetId;
        QString providerId;
        int zoom = 0;
        int x = 0;
        int y = 0;
        int retryCount = 0;
    };

    const QgcMapProvider *findProvider(const QString &providerId) const;
    bool withDatabase(bool write, const std::function<bool(sqlite3 *)> &callback) const;
    bool seedTileInventory(const QString &tileSetId);
    bool updateTileState(const TileCoord &tile,
                         const QString &state,
                         const QString &path,
                         const QString &lastError,
                         int retryCount);
    bool updateTileSetStatus(const QString &tileSetId, const QString &status);
    QVariantMap tileSetById(const QString &tileSetId) const;
    QVector<TileCoord> missingTilesForDownload(const QString &tileSetId) const;
    QString providerTileUrl(const QString &providerId, int zoom, int x, int y) const;
    QString providerTilePath(const QString &providerId, int zoom, int x, int y) const;
    QString offlineCachedTilePath(int zoom, int x, int y) const;
    bool removeTileSetFiles(const QString &tileSetId) const;
    bool copyFileAtomic(const QString &sourcePath, const QString &destinationPath) const;
    void startQueuedDownloads();
    void handleDownloadFinished(QNetworkReply *reply);
    void finishDownload(const TileCoord &tile, const QByteArray &data);
    void failDownload(const TileCoord &tile, const QString &error);
    void refreshAggregateCounts();
    static bool validBounds(double westDeg, double southDeg, double eastDeg, double northDeg);
    static int longitudeTile(double longitudeDeg, int zoom);
    static int latitudeTile(double latitudeDeg, int zoom);
    void setStatus(const QString &status, int progressPercent);
    void setError(const QString &error) const;

    QVector<QgcMapProvider> m_providers;
    QString m_rootPath;
    QString m_activeProviderId;
    QString m_status;
    QVariantList m_tileSets;
    int m_progressPercent;
    int m_cachedTileCount = 0;
    int m_missingTileCount = 0;
    int m_failedTileCount = 0;
    int m_inFlightTileCount = 0;
    int m_totalTileCount = 0;
    QNetworkAccessManager m_network;
    QQueue<TileCoord> m_downloadQueue;
    QHash<QNetworkReply *, TileCoord> m_activeDownloads;
    QSet<QString> m_canceledDownloads;
    mutable QString m_lastError;
};

} // namespace animus
