#pragma once

#include "maps/TileSource.h"

#include <QAbstractListModel>
#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

namespace animus
{

struct MapPack
{
    QString id;
    QString name;
    QString description;
    QString path;
    QString license;
    QString attribution;
    QString imageryFormat;
    QString imagerySourceStatus;
    QString terrainFormat;
    QString tileDatabasePath;
    int minZoom;
    int maxZoom;
    bool hasBounds;
    double westDeg;
    double southDeg;
    double eastDeg;
    double northDeg;
    bool has2dImagery;
    bool has3dTerrain;
};

class MapPackManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY packsChanged)
    Q_PROPERTY(
        QString activePackId READ activePackId WRITE setActivePackId NOTIFY activePackChanged)
    Q_PROPERTY(QString activePackPath READ activePackPath NOTIFY activePackChanged)
    Q_PROPERTY(QString activeTileDatabasePath READ activeTileDatabasePath NOTIFY activePackChanged)
    Q_PROPERTY(int activeMinZoom READ activeMinZoom NOTIFY activePackChanged)
    Q_PROPERTY(int activeMaxZoom READ activeMaxZoom NOTIFY activePackChanged)
    Q_PROPERTY(bool activeHasMbtilesImagery READ activeHasMbtilesImagery NOTIFY activePackChanged)
    Q_PROPERTY(
        QString activeImagerySourceStatus READ activeImagerySourceStatus NOTIFY activePackChanged)
    Q_PROPERTY(bool activeHasBounds READ activeHasBounds NOTIFY activePackChanged)
    Q_PROPERTY(double activeWestDeg READ activeWestDeg NOTIFY activePackChanged)
    Q_PROPERTY(double activeSouthDeg READ activeSouthDeg NOTIFY activePackChanged)
    Q_PROPERTY(double activeEastDeg READ activeEastDeg NOTIFY activePackChanged)
    Q_PROPERTY(double activeNorthDeg READ activeNorthDeg NOTIFY activePackChanged)

  public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        PathRole,
        LicenseRole,
        AttributionRole,
        ImageryFormatRole,
        ImagerySourceStatusRole,
        TerrainFormatRole,
        TileDatabasePathRole,
        MinZoomRole,
        MaxZoomRole,
        HasBoundsRole,
        WestDegRole,
        SouthDegRole,
        EastDegRole,
        NorthDegRole,
        Has2dImageryRole,
        Has3dTerrainRole
    };
    Q_ENUM(Roles)

    explicit MapPackManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const;
    void setRootPath(const QString &rootPath);

    QString activePackId() const;
    void setActivePackId(const QString &activePackId);
    QString activePackPath() const;
    QString activeTileDatabasePath() const;
    int activeMinZoom() const;
    int activeMaxZoom() const;
    bool activeHasMbtilesImagery() const;
    QString activeImagerySourceStatus() const;
    bool activeHasBounds() const;
    double activeWestDeg() const;
    double activeSouthDeg() const;
    double activeEastDeg() const;
    double activeNorthDeg() const;

    Q_INVOKABLE bool reload();
    Q_INVOKABLE QString validationError() const;
    Q_INVOKABLE QString activeAttribution() const;
    MbtilesPack mbtilesPackInfo(const QString &packId) const;

  signals:
    void packsChanged();
    void activePackChanged();

  private:
    static constexpr const char *DefaultPackId = "default-sitl-stanford";

    bool loadPack(const QDir &packDir, MapPack *pack, QString *error) const;
    const MapPack *findPack(const QString &packId) const;
    static bool parseBounds(const QJsonObject &object, MapPack *pack, QString *error);
    static bool validateMbtilesDatabase(const QString &databasePath,
                                        const QString &expectedName,
                                        const QString &expectedAttribution,
                                        QString *error);

    QString m_rootPath;
    QString m_activePackId;
    QString m_validationError;
    QVector<MapPack> m_packs;
};

} // namespace animus
