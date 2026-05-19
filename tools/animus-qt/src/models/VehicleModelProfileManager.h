#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QSettings;

namespace animus
{

class VehicleModel;

class VehicleModelProfileManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QString selectedProfileId READ selectedProfileId WRITE setSelectedProfileId NOTIFY
                   selectedProfileChanged)
    Q_PROPERTY(QVariantMap selectedProfile READ selectedProfile NOTIFY selectedProfileChanged)
    Q_PROPERTY(QVariantList surfaces READ surfaces NOTIFY surfacesChanged)

  public:
    struct SurfaceProfile
    {
        QString id;
        QString label;
        QString node;
        int actuatorChannel = 0;
        double pwmMinimum = 1000.0;
        double pwmNeutral = 1500.0;
        double pwmMaximum = 2000.0;
        double deflectionMinimumDeg = 0.0;
        double deflectionNeutralDeg = 0.0;
        double deflectionMaximumDeg = 0.0;
        double profilePolarity = 1.0;
        QVariantList axis{1.0, 0.0, 0.0};
    };

    struct ModelProfile
    {
        QString id;
        QString name;
        QString asset;
        double scale = 1.0;
        QVector<SurfaceProfile> surfaces;
    };

    explicit VehicleModelProfileManager(QObject *parent = nullptr);
    VehicleModelProfileManager(const QString &profilesDirectory,
                               QSettings *settings,
                               VehicleModel *vehicle,
                               QObject *parent = nullptr);

    QVariantList profiles() const;
    QString selectedProfileId() const;
    void setSelectedProfileId(const QString &profileId);
    QVariantMap selectedProfile() const;
    QVariantList surfaces() const;

    void setVehicleModel(VehicleModel *vehicle);
    const ModelProfile &selectedModelProfile() const;
    QVariantMap selectedModelMap() const;
    QVariantList mappedControlSurfaces() const;

    Q_INVOKABLE void reverseSurfacePolarity(const QString &surfaceId);
    Q_INVOKABLE void resetSurfacePolarity(const QString &surfaceId);
    Q_INVOKABLE void resetAllSurfacePolarity();
    Q_INVOKABLE double surfacePolarity(const QString &surfaceId) const;
    Q_INVOKABLE double mappedDeflectionDeg(const QString &surfaceId, int pwm) const;

  signals:
    void profilesChanged();
    void selectedProfileChanged();
    void surfacesChanged();

  private:
    void loadProfiles(const QString &profilesDirectory);
    void loadSettings();
    void persistSelectedProfile() const;
    void persistPolarityOverrides() const;
    int selectedProfileIndex() const;
    int profileIndex(const QString &profileId) const;
    const SurfaceProfile *surfaceProfile(const QString &surfaceId) const;
    QVariantMap surfaceMap(const SurfaceProfile &surface) const;
    double effectivePolarity(const SurfaceProfile &surface) const;
    double mappedDeflectionDeg(const SurfaceProfile &surface, int pwm) const;

    QVector<ModelProfile> m_profiles;
    QString m_selectedProfileId;
    QVariantMap m_polarityOverrides;
    QPointer<QSettings> m_settings;
    QPointer<VehicleModel> m_vehicle;
};

} // namespace animus
