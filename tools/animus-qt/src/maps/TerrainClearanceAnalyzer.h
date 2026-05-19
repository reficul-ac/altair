#pragma once

#include <QVariantMap>

namespace animus
{

class BreadcrumbPathModel;
class VehicleModel;

class TerrainClearanceAnalyzer final
{
  public:
    static QVariantMap analyze(const VehicleModel &vehicle, const BreadcrumbPathModel &trail);
};

} // namespace animus
