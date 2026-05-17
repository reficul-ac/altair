#pragma once

#include <QAbstractListModel>
#include <QDir>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

namespace animus {

struct MapPack {
    QString id;
    QString name;
    QString description;
    QString path;
    QString license;
    QString attribution;
    QString imageryFormat;
    QString terrainFormat;
    int minZoom;
    int maxZoom;
    bool has2dImagery;
    bool has3dTerrain;
};

class MapPackManager final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY packsChanged)
    Q_PROPERTY(QString activePackId READ activePackId WRITE setActivePackId NOTIFY activePackChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        PathRole,
        LicenseRole,
        AttributionRole,
        ImageryFormatRole,
        TerrainFormatRole,
        MinZoomRole,
        MaxZoomRole,
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

    Q_INVOKABLE bool reload();
    Q_INVOKABLE QString validationError() const;
    Q_INVOKABLE QString activeAttribution() const;

signals:
    void packsChanged();
    void activePackChanged();

private:
    bool loadPack(const QDir &packDir, MapPack *pack, QString *error) const;
    const MapPack *findPack(const QString &packId) const;

    QString m_rootPath;
    QString m_activePackId;
    QString m_validationError;
    QVector<MapPack> m_packs;
};

} // namespace animus
