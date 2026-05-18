#pragma once

#include "maps/MapPackManager.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace animus
{

class TileImageProvider final : public QQuickAsyncImageProvider
{
  public:
    explicit TileImageProvider(const MapPackManager *mapPacks);

    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
    QImage loadTileImage(const QString &id, const QSize &requestedSize) const;

  private:
    const MapPackManager *m_mapPacks;
};

} // namespace animus
