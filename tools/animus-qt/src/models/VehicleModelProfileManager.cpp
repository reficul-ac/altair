#include "models/VehicleModelProfileManager.h"

#include "models/VehicleModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

namespace animus
{
namespace
{

const QLatin1String kDefaultProfileId("generic_fixed_wing_smooth");

double objectNumber(const QJsonObject &object, const QString &key, double fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

VehicleModelProfileManager::ModelProfile fallbackProfile()
{
    VehicleModelProfileManager::ModelProfile profile;
    profile.id = kDefaultProfileId;
    profile.name = QStringLiteral("Generic Fixed-Wing Smooth");
    profile.asset = QStringLiteral("models/generic_fixed_wing_smooth.glb");
    profile.surfaces = {
        {QStringLiteral("left_aileron"),
         QStringLiteral("Left aileron"),
         QStringLiteral("aileron_left_pivot")},
        {QStringLiteral("right_aileron"),
         QStringLiteral("Right aileron"),
         QStringLiteral("aileron_right_pivot")},
        {QStringLiteral("elevator"), QStringLiteral("Elevator"), QStringLiteral("elevator_pivot")},
        {QStringLiteral("rudder"), QStringLiteral("Rudder"), QStringLiteral("rudder_pivot")}};
    return profile;
}

VehicleModelProfileManager::ModelProfile parseProfileFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject object = document.object();
    VehicleModelProfileManager::ModelProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.name = object.value(QStringLiteral("name")).toString(profile.id);
    profile.asset = object.value(QStringLiteral("asset")).toString();
    profile.scale = objectNumber(object, QStringLiteral("scale"), profile.scale);

    const QJsonArray surfaces = object.value(QStringLiteral("surfaces")).toArray();
    profile.surfaces.reserve(surfaces.size());
    for (const QJsonValue &surfaceValue : surfaces)
    {
        const QJsonObject surfaceObject = surfaceValue.toObject();
        VehicleModelProfileManager::SurfaceProfile surface;
        surface.id = surfaceObject.value(QStringLiteral("id")).toString();
        surface.label = surfaceObject.value(QStringLiteral("label")).toString(surface.id);
        surface.node = surfaceObject.value(QStringLiteral("node")).toString();
        surface.actuatorChannel = surfaceObject.value(QStringLiteral("actuatorChannel")).toInt(0);

        const QJsonObject pwm = surfaceObject.value(QStringLiteral("pwm")).toObject();
        surface.pwmMinimum = objectNumber(pwm, QStringLiteral("minimum"), surface.pwmMinimum);
        surface.pwmNeutral = objectNumber(pwm, QStringLiteral("neutral"), surface.pwmNeutral);
        surface.pwmMaximum = objectNumber(pwm, QStringLiteral("maximum"), surface.pwmMaximum);

        const QJsonObject deflection =
            surfaceObject.value(QStringLiteral("deflectionDeg")).toObject();
        surface.deflectionMinimumDeg =
            objectNumber(deflection, QStringLiteral("minimum"), surface.deflectionMinimumDeg);
        surface.deflectionNeutralDeg =
            objectNumber(deflection, QStringLiteral("neutral"), surface.deflectionNeutralDeg);
        surface.deflectionMaximumDeg =
            objectNumber(deflection, QStringLiteral("maximum"), surface.deflectionMaximumDeg);
        surface.profilePolarity =
            objectNumber(surfaceObject, QStringLiteral("polarity"), surface.profilePolarity);

        const QJsonArray axis = surfaceObject.value(QStringLiteral("axis")).toArray();
        if (axis.size() == 3)
            surface.axis = QVariantList{
                axis.at(0).toDouble(1.0), axis.at(1).toDouble(0.0), axis.at(2).toDouble(0.0)};

        if (!surface.id.isEmpty() && !surface.node.isEmpty())
            profile.surfaces.push_back(surface);
    }

    if (profile.id.isEmpty() || profile.asset.isEmpty() || profile.surfaces.isEmpty())
        return {};
    return profile;
}

bool profileLess(const VehicleModelProfileManager::ModelProfile &lhs,
                 const VehicleModelProfileManager::ModelProfile &rhs)
{
    return lhs.id < rhs.id;
}

} // namespace

VehicleModelProfileManager::VehicleModelProfileManager(QObject *parent)
    : VehicleModelProfileManager(QDir(QStringLiteral(ANIMUS_REPO_ROOT))
                                     .filePath(QStringLiteral("tools/animus-qt/web/cesium/models")),
                                 nullptr,
                                 nullptr,
                                 parent)
{
}

VehicleModelProfileManager::VehicleModelProfileManager(const QString &profilesDirectory,
                                                       QSettings *settings,
                                                       VehicleModel *vehicle,
                                                       QObject *parent)
    : QObject(parent), m_settings(settings), m_vehicle(vehicle)
{
    setObjectName(QStringLiteral("vehicleModelProfiles"));
    loadProfiles(profilesDirectory);
    loadSettings();
    if (m_vehicle)
        connect(m_vehicle,
                &VehicleModel::actuatorChanged,
                this,
                &VehicleModelProfileManager::surfacesChanged);
}

QVariantList VehicleModelProfileManager::profiles() const
{
    QVariantList result;
    result.reserve(m_profiles.size());
    for (const ModelProfile &profile : m_profiles)
    {
        result.push_back(QVariantMap{{QStringLiteral("id"), profile.id},
                                     {QStringLiteral("name"), profile.name},
                                     {QStringLiteral("asset"), profile.asset},
                                     {QStringLiteral("scale"), profile.scale}});
    }
    return result;
}

QString VehicleModelProfileManager::selectedProfileId() const
{
    return selectedModelProfile().id;
}

void VehicleModelProfileManager::setSelectedProfileId(const QString &profileId)
{
    const int index = profileIndex(profileId);
    if (index < 0 || selectedProfileId() == m_profiles.at(index).id)
        return;
    m_selectedProfileId = m_profiles.at(index).id;
    persistSelectedProfile();
    emit selectedProfileChanged();
    emit surfacesChanged();
}

QVariantMap VehicleModelProfileManager::selectedProfile() const
{
    const ModelProfile &profile = selectedModelProfile();
    return {{QStringLiteral("id"), profile.id},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("asset"), profile.asset},
            {QStringLiteral("scale"), profile.scale}};
}

QVariantList VehicleModelProfileManager::surfaces() const
{
    QVariantList result;
    const ModelProfile &profile = selectedModelProfile();
    result.reserve(profile.surfaces.size());
    for (const SurfaceProfile &surface : profile.surfaces)
        result.push_back(surfaceMap(surface));
    return result;
}

void VehicleModelProfileManager::setVehicleModel(VehicleModel *vehicle)
{
    if (m_vehicle == vehicle)
        return;
    if (m_vehicle)
        disconnect(m_vehicle,
                   &VehicleModel::actuatorChanged,
                   this,
                   &VehicleModelProfileManager::surfacesChanged);
    m_vehicle = vehicle;
    if (m_vehicle)
        connect(m_vehicle,
                &VehicleModel::actuatorChanged,
                this,
                &VehicleModelProfileManager::surfacesChanged);
    emit surfacesChanged();
}

const VehicleModelProfileManager::ModelProfile &
VehicleModelProfileManager::selectedModelProfile() const
{
    const int index = selectedProfileIndex();
    return m_profiles.at(index);
}

QVariantMap VehicleModelProfileManager::selectedModelMap() const
{
    const ModelProfile &profile = selectedModelProfile();
    return {{QStringLiteral("profile"), profile.id},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("asset"), profile.asset},
            {QStringLiteral("scale"), profile.scale}};
}

QVariantList VehicleModelProfileManager::mappedControlSurfaces() const
{
    QVariantList result;
    const ModelProfile &profile = selectedModelProfile();
    result.reserve(profile.surfaces.size());
    for (const SurfaceProfile &surface : profile.surfaces)
    {
        const bool valid = m_vehicle && m_vehicle->servoOutputValid(surface.actuatorChannel);
        const double deflectionDeg =
            valid ? mappedDeflectionDeg(surface, m_vehicle->servoOutputPwm(surface.actuatorChannel))
                  : 0.0;
        const double polarity = effectivePolarity(surface);
        result.push_back(QVariantMap{
            {QStringLiteral("id"), surface.id},
            {QStringLiteral("label"), surface.label},
            {QStringLiteral("node"), surface.node},
            {QStringLiteral("axis"), surface.axis},
            {QStringLiteral("actuatorChannel"), surface.actuatorChannel},
            {QStringLiteral("profilePolarity"), surface.profilePolarity},
            {QStringLiteral("polarity"), polarity},
            {QStringLiteral("polarityOverride"),
             m_polarityOverrides.contains(surface.id) ? QVariant(polarity) : QVariant()},
            {QStringLiteral("polarityReversed"), polarity != surface.profilePolarity},
            {QStringLiteral("deflectionDeg"), deflectionDeg},
            {QStringLiteral("valid"), valid}});
    }
    return result;
}

void VehicleModelProfileManager::reverseSurfacePolarity(const QString &surfaceId)
{
    const SurfaceProfile *surface = surfaceProfile(surfaceId);
    if (!surface)
        return;
    const double reversed = -effectivePolarity(*surface);
    if (reversed == surface->profilePolarity)
        m_polarityOverrides.remove(surfaceId);
    else
        m_polarityOverrides.insert(surfaceId, reversed);
    persistPolarityOverrides();
    emit surfacesChanged();
}

void VehicleModelProfileManager::resetSurfacePolarity(const QString &surfaceId)
{
    if (!m_polarityOverrides.contains(surfaceId))
        return;
    m_polarityOverrides.remove(surfaceId);
    persistPolarityOverrides();
    emit surfacesChanged();
}

void VehicleModelProfileManager::resetAllSurfacePolarity()
{
    if (m_polarityOverrides.isEmpty())
        return;
    m_polarityOverrides.clear();
    persistPolarityOverrides();
    emit surfacesChanged();
}

double VehicleModelProfileManager::surfacePolarity(const QString &surfaceId) const
{
    const SurfaceProfile *surface = surfaceProfile(surfaceId);
    return surface ? effectivePolarity(*surface) : 1.0;
}

double VehicleModelProfileManager::mappedDeflectionDeg(const QString &surfaceId, int pwm) const
{
    const SurfaceProfile *surface = surfaceProfile(surfaceId);
    return surface ? mappedDeflectionDeg(*surface, pwm) : 0.0;
}

void VehicleModelProfileManager::loadProfiles(const QString &profilesDirectory)
{
    const QDir dir(profilesDirectory);
    const QFileInfoList files =
        dir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files)
    {
        const ModelProfile profile = parseProfileFile(file.absoluteFilePath());
        if (!profile.id.isEmpty())
            m_profiles.push_back(profile);
    }
    if (m_profiles.isEmpty())
        m_profiles.push_back(fallbackProfile());
    std::sort(m_profiles.begin(), m_profiles.end(), profileLess);
    m_selectedProfileId =
        profileIndex(kDefaultProfileId) >= 0 ? kDefaultProfileId : m_profiles.constFirst().id;
}

void VehicleModelProfileManager::loadSettings()
{
    if (!m_settings)
        return;
    const QString storedProfile =
        m_settings->value(QStringLiteral("terrain3d/modelProfile/selectedProfileId")).toString();
    if (profileIndex(storedProfile) >= 0)
        m_selectedProfileId = storedProfile;
    m_polarityOverrides =
        m_settings->value(QStringLiteral("terrain3d/modelProfile/polarityOverrides")).toMap();
}

void VehicleModelProfileManager::persistSelectedProfile() const
{
    if (!m_settings)
        return;
    m_settings->setValue(QStringLiteral("terrain3d/modelProfile/selectedProfileId"),
                         selectedProfileId());
}

void VehicleModelProfileManager::persistPolarityOverrides() const
{
    if (!m_settings)
        return;
    m_settings->setValue(QStringLiteral("terrain3d/modelProfile/polarityOverrides"),
                         m_polarityOverrides);
}

int VehicleModelProfileManager::selectedProfileIndex() const
{
    const int index = profileIndex(m_selectedProfileId);
    return index >= 0 ? index : 0;
}

int VehicleModelProfileManager::profileIndex(const QString &profileId) const
{
    for (int index = 0; index < m_profiles.size(); ++index)
    {
        if (m_profiles.at(index).id == profileId)
            return index;
    }
    return -1;
}

const VehicleModelProfileManager::SurfaceProfile *
VehicleModelProfileManager::surfaceProfile(const QString &surfaceId) const
{
    const ModelProfile &profile = selectedModelProfile();
    for (const SurfaceProfile &surface : profile.surfaces)
    {
        if (surface.id == surfaceId)
            return &surface;
    }
    return nullptr;
}

QVariantMap VehicleModelProfileManager::surfaceMap(const SurfaceProfile &surface) const
{
    const bool valid = m_vehicle && m_vehicle->servoOutputValid(surface.actuatorChannel);
    const double deflectionDeg =
        valid ? mappedDeflectionDeg(surface, m_vehicle->servoOutputPwm(surface.actuatorChannel))
              : 0.0;
    const double polarity = effectivePolarity(surface);
    return {{QStringLiteral("id"), surface.id},
            {QStringLiteral("label"), surface.label},
            {QStringLiteral("node"), surface.node},
            {QStringLiteral("axis"), surface.axis},
            {QStringLiteral("actuatorChannel"), surface.actuatorChannel},
            {QStringLiteral("profilePolarity"), surface.profilePolarity},
            {QStringLiteral("polarity"), polarity},
            {QStringLiteral("polarityOverride"),
             m_polarityOverrides.contains(surface.id) ? QVariant(polarity) : QVariant()},
            {QStringLiteral("polarityReversed"), polarity != surface.profilePolarity},
            {QStringLiteral("deflectionDeg"), deflectionDeg},
            {QStringLiteral("valid"), valid}};
}

double VehicleModelProfileManager::effectivePolarity(const SurfaceProfile &surface) const
{
    bool ok = false;
    const double override =
        m_polarityOverrides.value(surface.id, surface.profilePolarity).toDouble(&ok);
    return ok ? override : surface.profilePolarity;
}

double VehicleModelProfileManager::mappedDeflectionDeg(const SurfaceProfile &surface, int pwm) const
{
    const double boundedPwm =
        std::clamp(static_cast<double>(pwm), surface.pwmMinimum, surface.pwmMaximum);
    const double polarity = effectivePolarity(surface);
    if (boundedPwm < surface.pwmNeutral)
    {
        const double span = surface.pwmNeutral - surface.pwmMinimum;
        if (span <= 0.0)
            return polarity * surface.deflectionNeutralDeg;
        const double ratio = (surface.pwmNeutral - boundedPwm) / span;
        return polarity * (surface.deflectionNeutralDeg +
                           ratio * (surface.deflectionMinimumDeg - surface.deflectionNeutralDeg));
    }

    const double span = surface.pwmMaximum - surface.pwmNeutral;
    if (span <= 0.0)
        return polarity * surface.deflectionNeutralDeg;
    const double ratio = (boundedPwm - surface.pwmNeutral) / span;
    return polarity * (surface.deflectionNeutralDeg +
                       ratio * (surface.deflectionMaximumDeg - surface.deflectionNeutralDeg));
}

} // namespace animus
