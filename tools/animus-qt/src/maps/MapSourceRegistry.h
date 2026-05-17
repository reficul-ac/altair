#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

namespace animus {

struct MapSource {
    QString id;
    QString label;
    QString provider;
    QString attribution;
    bool networkRequired;
};

class MapSourceRegistry final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString activeSourceId READ activeSourceId WRITE setActiveSourceId NOTIFY activeSourceChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        ProviderRole,
        AttributionRole,
        NetworkRequiredRole
    };
    Q_ENUM(Roles)

    explicit MapSourceRegistry(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeSourceId() const;
    void setActiveSourceId(const QString &activeSourceId);

    Q_INVOKABLE QString activeAttribution() const;
    Q_INVOKABLE bool sourceRequiresNetwork(const QString &sourceId) const;

signals:
    void activeSourceChanged();

private:
    const MapSource *findSource(const QString &sourceId) const;

    QVector<MapSource> m_sources;
    QString m_activeSourceId;
};

} // namespace animus
