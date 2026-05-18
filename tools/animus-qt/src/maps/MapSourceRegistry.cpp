#include "maps/MapSourceRegistry.h"

namespace animus
{

MapSourceRegistry::MapSourceRegistry(QObject *parent) : QAbstractListModel(parent)
{
    m_sources.push_back(
        {"offline-pack", "Offline Map Pack", "animus-pack", "Active map pack attribution", false});
    m_sources.push_back({"osm", "OpenStreetMap", "osm", "OpenStreetMap contributors", true});
    m_sources.push_back({"satellite",
                         "Licensed Satellite",
                         "raster-provider",
                         "Operator-provided licensed imagery",
                         true});
    m_activeSourceId = QStringLiteral("offline-pack");
}

int MapSourceRegistry::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_sources.size();
}

QVariant MapSourceRegistry::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sources.size())
        return QVariant();
    const MapSource &source = m_sources.at(index.row());
    switch (role)
    {
    case IdRole:
        return source.id;
    case LabelRole:
        return source.label;
    case ProviderRole:
        return source.provider;
    case AttributionRole:
        return source.attribution;
    case NetworkRequiredRole:
        return source.networkRequired;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> MapSourceRegistry::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "sourceId";
    roles[LabelRole] = "label";
    roles[ProviderRole] = "provider";
    roles[AttributionRole] = "attribution";
    roles[NetworkRequiredRole] = "networkRequired";
    return roles;
}

QString MapSourceRegistry::activeSourceId() const
{
    return m_activeSourceId;
}

void MapSourceRegistry::setActiveSourceId(const QString &activeSourceId)
{
    if (!findSource(activeSourceId) || m_activeSourceId == activeSourceId)
        return;
    m_activeSourceId = activeSourceId;
    emit activeSourceChanged();
}

QString MapSourceRegistry::activeLabel() const
{
    const MapSource *source = findSource(m_activeSourceId);
    return source ? source->label : QString();
}

QString MapSourceRegistry::activeProvider() const
{
    const MapSource *source = findSource(m_activeSourceId);
    return source ? source->provider : QString();
}

QString MapSourceRegistry::activeAttribution() const
{
    const MapSource *source = findSource(m_activeSourceId);
    return source ? source->attribution : QString();
}

QString MapSourceRegistry::sourceIdAt(int row) const
{
    if (row < 0 || row >= m_sources.size())
        return QString();
    return m_sources.at(row).id;
}

int MapSourceRegistry::sourceIndex(const QString &sourceId) const
{
    for (int i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources.at(i).id == sourceId)
            return i;
    }
    return -1;
}

QString MapSourceRegistry::sourceLabel(const QString &sourceId) const
{
    const MapSource *source = findSource(sourceId);
    return source ? source->label : QString();
}

QString MapSourceRegistry::sourceProvider(const QString &sourceId) const
{
    const MapSource *source = findSource(sourceId);
    return source ? source->provider : QString();
}

bool MapSourceRegistry::sourceExists(const QString &sourceId) const
{
    return findSource(sourceId) != nullptr;
}

bool MapSourceRegistry::sourceRequiresNetwork(const QString &sourceId) const
{
    const MapSource *source = findSource(sourceId);
    return source ? source->networkRequired : true;
}

const MapSource *MapSourceRegistry::findSource(const QString &sourceId) const
{
    for (const MapSource &source : m_sources)
    {
        if (source.id == sourceId)
            return &source;
    }
    return nullptr;
}

} // namespace animus
