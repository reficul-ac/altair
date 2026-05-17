#include "maps/MapPackManager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace animus {

MapPackManager::MapPackManager(QObject *parent) : QAbstractListModel(parent), m_rootPath(QStringLiteral("map_packs")) {}

int MapPackManager::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_packs.size();
}

QVariant MapPackManager::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_packs.size()) return QVariant();
    const MapPack &pack = m_packs.at(index.row());
    switch (role) {
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
    case MinZoomRole:
        return pack.minZoom;
    case MaxZoomRole:
        return pack.maxZoom;
    case Has2dImageryRole:
        return pack.has2dImagery;
    case Has3dTerrainRole:
        return pack.has3dTerrain;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> MapPackManager::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "packId";
    roles[NameRole] = "name";
    roles[DescriptionRole] = "description";
    roles[PathRole] = "path";
    roles[LicenseRole] = "license";
    roles[AttributionRole] = "attribution";
    roles[ImageryFormatRole] = "imageryFormat";
    roles[TerrainFormatRole] = "terrainFormat";
    roles[MinZoomRole] = "minZoom";
    roles[MaxZoomRole] = "maxZoom";
    roles[Has2dImageryRole] = "has2dImagery";
    roles[Has3dTerrainRole] = "has3dTerrain";
    return roles;
}

QString MapPackManager::rootPath() const { return m_rootPath; }

void MapPackManager::setRootPath(const QString &rootPath) {
    if (m_rootPath == rootPath) return;
    m_rootPath = rootPath;
    reload();
    emit packsChanged();
}

QString MapPackManager::activePackId() const { return m_activePackId; }

void MapPackManager::setActivePackId(const QString &activePackId) {
    if (!activePackId.isEmpty() && !findPack(activePackId)) return;
    if (m_activePackId == activePackId) return;
    m_activePackId = activePackId;
    emit activePackChanged();
}

bool MapPackManager::reload() {
    QDir root(m_rootPath);
    QVector<MapPack> loaded;
    QString firstError;

    if (root.exists()) {
        const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            MapPack pack;
            QString error;
            if (loadPack(QDir(entry.absoluteFilePath()), &pack, &error)) {
                loaded.push_back(pack);
            } else if (firstError.isEmpty()) {
                firstError = error;
            }
        }
    }

    beginResetModel();
    m_packs = loaded;
    m_validationError = firstError;
    if (!m_activePackId.isEmpty() && !findPack(m_activePackId)) m_activePackId.clear();
    endResetModel();
    emit packsChanged();
    emit activePackChanged();
    return firstError.isEmpty();
}

QString MapPackManager::validationError() const { return m_validationError; }

QString MapPackManager::activeAttribution() const {
    const MapPack *pack = findPack(m_activePackId);
    return pack ? pack->attribution : QString();
}

bool MapPackManager::loadPack(const QDir &packDir, MapPack *pack, QString *error) const {
    QFile metadata(packDir.filePath(QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("%1: metadata.json is missing").arg(packDir.dirName());
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
    if (!document.isObject()) {
        if (error) *error = QStringLiteral("%1: metadata.json must be an object").arg(packDir.dirName());
        return false;
    }

    const QJsonObject object = document.object();
    const int schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(0);
    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    const QString license = object.value(QStringLiteral("license")).toString().trimmed();
    const QString attribution = object.value(QStringLiteral("attribution")).toString().trimmed();
    if (schemaVersion != 1 || name.isEmpty() || license.isEmpty() || attribution.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1: schemaVersion 1, name, license, and attribution are required")
                         .arg(packDir.dirName());
        return false;
    }

    const QJsonObject imagery = object.value(QStringLiteral("imagery")).toObject();
    const QJsonObject terrain = object.value(QStringLiteral("terrain")).toObject();
    const QString imageryFormat = imagery.value(QStringLiteral("format")).toString().trimmed();
    const QString terrainFormat = terrain.value(QStringLiteral("format")).toString(QStringLiteral("none")).trimmed();
    if (imageryFormat != QStringLiteral("xyz") && imageryFormat != QStringLiteral("mbtiles")) {
        if (error) *error = QStringLiteral("%1: imagery.format must be xyz or mbtiles").arg(packDir.dirName());
        return false;
    }
    if (terrainFormat != QStringLiteral("none") && terrainFormat != QStringLiteral("quantized-mesh")) {
        if (error) *error = QStringLiteral("%1: terrain.format must be none or quantized-mesh").arg(packDir.dirName());
        return false;
    }

    pack->id = packDir.dirName();
    pack->name = name;
    pack->description = object.value(QStringLiteral("description")).toString();
    pack->path = packDir.absolutePath();
    pack->license = license;
    pack->attribution = attribution;
    pack->imageryFormat = imageryFormat;
    pack->terrainFormat = terrainFormat;
    pack->minZoom = object.value(QStringLiteral("minZoom")).toInt(0);
    pack->maxZoom = object.value(QStringLiteral("maxZoom")).toInt(pack->minZoom);
    pack->has2dImagery =
        imageryFormat == QStringLiteral("xyz") || QFileInfo::exists(packDir.filePath(QStringLiteral("2d/imagery.mbtiles")));
    pack->has3dTerrain =
        terrainFormat == QStringLiteral("quantized-mesh") && QFileInfo::exists(packDir.filePath(QStringLiteral("3d/terrain/layer.json")));
    return true;
}

const MapPack *MapPackManager::findPack(const QString &packId) const {
    for (const MapPack &pack : m_packs) {
        if (pack.id == packId) return &pack;
    }
    return nullptr;
}

} // namespace animus
