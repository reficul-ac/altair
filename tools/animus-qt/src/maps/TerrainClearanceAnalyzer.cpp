#include "maps/TerrainClearanceAnalyzer.h"

#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"

#include <QModelIndex>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace animus
{
namespace
{

constexpr double WarningClearanceM = 20.0;
constexpr double CautionClearanceM = 50.0;
constexpr int RecentTrailSamples = 30;

QVariantMap unknown(const QString &message, const VehicleModel &vehicle)
{
    return {{QStringLiteral("aglM"), 0.0},
            {QStringLiteral("homeRelativeAltitudeM"),
             vehicle.homeValid() ? vehicle.altitudeM() - vehicle.homeAltitudeM() : 0.0},
            {QStringLiteral("homeAltitudeValid"), vehicle.homeValid()},
            {QStringLiteral("terrainHeightM"), vehicle.terrainCurrentHeightM()},
            {QStringLiteral("terrainReportValid"), vehicle.terrainValid()},
            {QStringLiteral("trendMps"), 0.0},
            {QStringLiteral("minimumRecentClearanceM"), 0.0},
            {QStringLiteral("state"), QStringLiteral("unknown")},
            {QStringLiteral("message"), message}};
}

} // namespace

QVariantMap TerrainClearanceAnalyzer::analyze(const VehicleModel &vehicle,
                                              const BreadcrumbPathModel &trail)
{
    if (!vehicle.positionValid())
        return unknown(QStringLiteral("position unavailable"), vehicle);
    if (!vehicle.terrainValid())
        return unknown(QStringLiteral("terrain report unavailable"), vehicle);

    const double terrainHeightM = vehicle.terrainCurrentHeightM();
    const double aglM = vehicle.altitudeM() - terrainHeightM;
    double minimumRecentClearanceM = aglM;
    double trendMps = 0.0;

    const int rowCount = trail.rowCount();
    const int firstRow = std::max(0, rowCount - RecentTrailSamples);
    int validRows = 0;
    double firstClearanceM = 0.0;
    double firstTimestampS = 0.0;
    double lastClearanceM = 0.0;
    double lastTimestampS = 0.0;

    for (int row = firstRow; row < rowCount; ++row)
    {
        const QModelIndex index = trail.index(row, 0);
        const double altitudeM = trail.data(index, BreadcrumbPathModel::AltitudeRole).toDouble();
        const double timestampS = trail.data(index, BreadcrumbPathModel::TimestampRole).toDouble();
        const double clearanceM = altitudeM - terrainHeightM;
        minimumRecentClearanceM = std::min(minimumRecentClearanceM, clearanceM);
        if (validRows == 0)
        {
            firstClearanceM = clearanceM;
            firstTimestampS = timestampS;
        }
        lastClearanceM = clearanceM;
        lastTimestampS = timestampS;
        ++validRows;
    }

    if (validRows >= 2 && lastTimestampS > firstTimestampS)
        trendMps = (lastClearanceM - firstClearanceM) / (lastTimestampS - firstTimestampS);

    QString state = QStringLiteral("clear");
    QString message = QStringLiteral("clearance clear");
    if (minimumRecentClearanceM < WarningClearanceM)
    {
        state = QStringLiteral("warning");
        message = QStringLiteral("terrain clearance warning");
    }
    else if (minimumRecentClearanceM < CautionClearanceM)
    {
        state = QStringLiteral("caution");
        message = QStringLiteral("terrain clearance caution");
    }

    return {{QStringLiteral("aglM"), aglM},
            {QStringLiteral("homeRelativeAltitudeM"),
             vehicle.homeValid() ? vehicle.altitudeM() - vehicle.homeAltitudeM() : 0.0},
            {QStringLiteral("homeAltitudeValid"), vehicle.homeValid()},
            {QStringLiteral("terrainHeightM"), terrainHeightM},
            {QStringLiteral("terrainReportValid"), true},
            {QStringLiteral("trendMps"), trendMps},
            {QStringLiteral("minimumRecentClearanceM"), minimumRecentClearanceM},
            {QStringLiteral("state"), state},
            {QStringLiteral("message"), message}};
}

} // namespace animus
