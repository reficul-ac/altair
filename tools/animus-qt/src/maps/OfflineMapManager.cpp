#include "maps/OfflineMapManager.h"

#include "maps/MapSourceRegistry.h"

namespace animus {

OfflineMapManager::OfflineMapManager(MapSourceRegistry *registry, QObject *parent)
    : QObject(parent), m_registry(registry), m_mode(StrictOffline) {}

OfflineMapManager::Mode OfflineMapManager::mode() const { return m_mode; }

void OfflineMapManager::setMode(Mode mode) {
    if (m_mode == mode) return;
    m_mode = mode;
    emit modeChanged();
}

bool OfflineMapManager::networkAllowed() const { return m_mode == Online; }

bool OfflineMapManager::canUseSource(const QString &sourceId) const {
    if (!m_registry) return false;
    if (m_mode == Online) return true;
    return !m_registry->sourceRequiresNetwork(sourceId);
}

QString OfflineMapManager::modeLabel() const {
    switch (m_mode) {
    case Online:
        return QStringLiteral("Online");
    case CachedOffline:
        return QStringLiteral("Cached/offline");
    case StrictOffline:
        return QStringLiteral("Strict offline");
    }
    return QStringLiteral("Unknown");
}

} // namespace animus
