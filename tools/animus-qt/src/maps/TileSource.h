#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace animus
{

struct MbtilesPack
{
    bool valid;
    QString databasePath;
    int minZoom;
    int maxZoom;
};

class TileSource
{
  public:
    virtual ~TileSource() = default;
    virtual QImage loadTile(int zoom, int column, int row, const QSize &requestedSize) const = 0;
};

class MbtilesTileSource final : public TileSource
{
  public:
    explicit MbtilesTileSource(MbtilesPack pack);

    QImage loadTile(int zoom, int column, int row, const QSize &requestedSize) const override;

    static int mbtilesRow(int zoom, int row);
    static QImage emptyTile(const QSize &requestedSize);

  private:
    MbtilesPack m_pack;
};

} // namespace animus
