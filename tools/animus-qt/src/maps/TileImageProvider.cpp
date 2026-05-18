#include "maps/TileImageProvider.h"

#include <QStringList>

namespace animus
{
namespace
{

bool parseTileId(const QString &id, QString *packId, int *zoom, int *column, int *row)
{
    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 4)
        return false;
    if (parts.at(0).contains(QLatin1String("..")) || parts.at(0).contains(QLatin1Char('\\')))
        return false;

    bool okZoom = false;
    bool okColumn = false;
    bool okRow = false;
    const int parsedZoom = parts.at(1).toInt(&okZoom);
    const int parsedColumn = parts.at(2).toInt(&okColumn);
    const int parsedRow = parts.at(3).toInt(&okRow);
    if (!okZoom || !okColumn || !okRow)
        return false;

    *packId = parts.at(0);
    *zoom = parsedZoom;
    *column = parsedColumn;
    *row = parsedRow;
    return true;
}

} // namespace

TileImageProvider::TileImageProvider(const MapPackManager *mapPacks)
    : QQuickImageProvider(QQuickImageProvider::Image), m_mapPacks(mapPacks)
{
}

QImage TileImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = loadTileImage(id, requestedSize);
    if (size)
        *size = image.size();
    return image;
}

QImage TileImageProvider::loadTileImage(const QString &id, const QSize &requestedSize) const
{
    if (!m_mapPacks)
        return MbtilesTileSource::emptyTile(requestedSize);

    QString packId;
    int zoom = 0;
    int column = 0;
    int row = 0;
    if (!parseTileId(id, &packId, &zoom, &column, &row))
        return MbtilesTileSource::emptyTile(requestedSize);

    const MbtilesTileSource source(m_mapPacks->mbtilesPackInfo(packId));
    return source.loadTile(zoom, column, row, requestedSize);
}

} // namespace animus
