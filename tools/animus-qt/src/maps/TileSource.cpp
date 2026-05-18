#include "maps/TileSource.h"

#include <QByteArray>
#include <QtGlobal>

#include <sqlite3.h>

#include <utility>

namespace animus
{
namespace
{

bool validTileRequest(const MbtilesPack &pack, int zoom, int column, int row)
{
    if (!pack.valid || pack.databasePath.isEmpty())
        return false;
    if (zoom < pack.minZoom || zoom > pack.maxZoom || zoom < 0 || zoom > 30)
        return false;
    if (column < 0 || row < 0)
        return false;
    const int maxIndex = (1 << zoom) - 1;
    return column <= maxIndex && row <= maxIndex;
}

} // namespace

MbtilesTileSource::MbtilesTileSource(MbtilesPack pack) : m_pack(std::move(pack))
{
}

int MbtilesTileSource::mbtilesRow(int zoom, int row)
{
    if (zoom < 0 || zoom > 30 || row < 0)
        return -1;
    const int maxIndex = (1 << zoom) - 1;
    if (row > maxIndex)
        return -1;
    return maxIndex - row;
}

QImage MbtilesTileSource::emptyTile(const QSize &requestedSize)
{
    const QSize size = requestedSize.isValid() ? requestedSize : QSize(256, 256);
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    return image;
}

QImage MbtilesTileSource::loadTile(int zoom,
                                   int column,
                                   int row,
                                   const QSize &requestedSize) const
{
    if (!validTileRequest(m_pack, zoom, column, row))
        return emptyTile(requestedSize);

    sqlite3 *database = nullptr;
    if (sqlite3_open_v2(m_pack.databasePath.toUtf8().constData(),
                        &database,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                        nullptr) != SQLITE_OK)
    {
        if (database)
            sqlite3_close(database);
        return emptyTile(requestedSize);
    }

    sqlite3_stmt *statement = nullptr;
    const char *sql =
        "SELECT tile_data FROM tiles "
        "WHERE zoom_level = ? AND tile_column = ? AND tile_row = ? LIMIT 1";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        sqlite3_close(database);
        return emptyTile(requestedSize);
    }

    sqlite3_bind_int(statement, 1, zoom);
    sqlite3_bind_int(statement, 2, column);
    sqlite3_bind_int(statement, 3, mbtilesRow(zoom, row));

    QImage image;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        const void *blob = sqlite3_column_blob(statement, 0);
        const int size = sqlite3_column_bytes(statement, 0);
        if (blob && size > 0)
        {
            const QByteArray bytes(static_cast<const char *>(blob), size);
            image.loadFromData(bytes);
        }
    }

    sqlite3_finalize(statement);
    sqlite3_close(database);

    if (image.isNull())
        return emptyTile(requestedSize);
    if (requestedSize.isValid() && image.size() != requestedSize)
        image = image.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace animus
