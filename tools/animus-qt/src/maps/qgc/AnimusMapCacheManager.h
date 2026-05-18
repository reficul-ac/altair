#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
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

    Q_INVOKABLE QString providerIdAt(int row) const;
    Q_INVOKABLE int providerIndex(const QString &providerId) const;
    Q_INVOKABLE bool providerRequiresNetwork(const QString &providerId) const;
    Q_INVOKABLE bool providerConfigured(const QString &providerId) const;
    Q_INVOKABLE QString providerBlockReason(const QString &providerId, bool networkAllowed) const;
    Q_INVOKABLE bool initializeCache();
    Q_INVOKABLE int estimateTileCount(double westDeg,
                                      double southDeg,
                                      double eastDeg,
                                      double northDeg,
                                      int minZoom,
                                      int maxZoom) const;
    Q_INVOKABLE double estimateSizeMb(int tileCount) const;
    Q_INVOKABLE bool createTileSet(const QString &name,
                                   double westDeg,
                                   double southDeg,
                                   double eastDeg,
                                   double northDeg,
                                   int minZoom,
                                   int maxZoom);
    Q_INVOKABLE bool downloadTileSet(const QString &tileSetId);
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
    const QgcMapProvider *findProvider(const QString &providerId) const;
    bool withDatabase(bool write, const std::function<bool(sqlite3 *)> &callback) const;
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
    mutable QString m_lastError;
};

} // namespace animus
