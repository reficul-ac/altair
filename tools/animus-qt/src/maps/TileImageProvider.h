#pragma once

#include "maps/MapPackManager.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace animus
{

class TileImageProvider final : public QQuickImageProvider
{
  public:
    explicit TileImageProvider(const MapPackManager *mapPacks);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    QImage loadTileImage(const QString &id, const QSize &requestedSize) const;

  private:
    const MapPackManager *m_mapPacks;
};

} // namespace animus
