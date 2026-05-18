#pragma once

#include <QObject>
#include <QString>

namespace animus
{

class MapSourceRegistry;

class OfflineMapManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool networkAllowed READ networkAllowed NOTIFY modeChanged)

  public:
    enum Mode
    {
        Online,
        CachedOffline,
        StrictOffline
    };
    Q_ENUM(Mode)

    explicit OfflineMapManager(MapSourceRegistry *registry, QObject *parent = nullptr);

    Mode mode() const;
    void setMode(Mode mode);

    bool networkAllowed() const;
    Q_INVOKABLE bool canUseSource(const QString &sourceId) const;
    Q_INVOKABLE QString modeLabel() const;
    Q_INVOKABLE QString sourceBlockReason(const QString &sourceId) const;

  signals:
    void modeChanged();

  private:
    MapSourceRegistry *m_registry;
    Mode m_mode;
};

} // namespace animus
