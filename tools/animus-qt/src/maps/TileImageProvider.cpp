#include "maps/TileImageProvider.h"

#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QQuickTextureFactory>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>

namespace animus
{
namespace
{

QImage emptyTile(const QSize &requestedSize)
{
    const QSize size = requestedSize.isValid() ? requestedSize : QSize(256, 256);
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    return image;
}

class TileImageResponse final : public QQuickImageResponse
{
  public:
    TileImageResponse(std::function<QImage()> loadImage, QSize requestedSize)
        : m_requestedSize(requestedSize)
    {
        connect(&m_watcher,
                &QFutureWatcher<QImage>::finished,
                this,
                [this]()
                {
                    m_image = m_watcher.result();
                    if (m_image.isNull())
                        m_image = emptyTile(m_requestedSize);
                    emit finished();
                });
        m_watcher.setFuture(QtConcurrent::run(std::move(loadImage)));
    }

    ~TileImageResponse() override
    {
        m_watcher.cancel();
        m_watcher.waitForFinished();
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_error;
    }

    void cancel() override
    {
        m_watcher.cancel();
    }

  private:
    mutable QImage m_image;
    QSize m_requestedSize;
    QString m_error;
    QFutureWatcher<QImage> m_watcher;
};

bool parseTileId(const QString &id, QString *packId, int *zoom, int *x, int *y)
{
    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 4)
        return false;
    if (parts.at(0).contains(QLatin1String("..")) || parts.at(0).contains(QLatin1Char('\\')))
        return false;

    bool okZoom = false;
    bool okX = false;
    bool okY = false;
    const int parsedZoom = parts.at(1).toInt(&okZoom);
    const int parsedX = parts.at(2).toInt(&okX);
    const int parsedY = parts.at(3).toInt(&okY);
    if (!okZoom || !okX || !okY)
        return false;

    *packId = parts.at(0);
    *zoom = parsedZoom;
    *x = parsedX;
    *y = parsedY;
    return true;
}

QString resolveTilePath(const LocalXyzPack &pack, int zoom, int x, int y)
{
    if (!pack.valid)
        return QString();
    if (zoom < pack.minZoom || zoom > pack.maxZoom || x < 0 || y < 0)
        return QString();
    if (zoom < 0 || zoom > 30)
        return QString();
    const int maxIndex = (1 << zoom) - 1;
    if (x > maxIndex || y > maxIndex)
        return QString();

    const QDir tileRoot(pack.tileRootPath);
    const QString relativePath = QStringLiteral("%1/%2/%3.png").arg(zoom).arg(x).arg(y);
    const QString absolutePath = QFileInfo(tileRoot.filePath(relativePath)).canonicalFilePath();
    const QString canonicalRoot = QFileInfo(pack.tileRootPath).canonicalFilePath();
    if (absolutePath.isEmpty() || canonicalRoot.isEmpty())
        return QString();
    if (absolutePath != canonicalRoot &&
        !absolutePath.startsWith(canonicalRoot + QDir::separator()))
        return QString();
    if (!absolutePath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        return QString();
    return absolutePath;
}

QImage loadTileImageFromPack(const LocalXyzPack &pack,
                             int zoom,
                             int x,
                             int y,
                             const QSize &requestedSize)
{
    const QString path = resolveTilePath(pack, zoom, x, y);
    if (path.isEmpty())
        return emptyTile(requestedSize);

    QImage image(path);
    if (image.isNull())
        return emptyTile(requestedSize);
    if (requestedSize.isValid() && image.size() != requestedSize)
        image = image.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace

TileImageProvider::TileImageProvider(const MapPackManager *mapPacks) : m_mapPacks(mapPacks) {}

QQuickImageResponse *TileImageProvider::requestImageResponse(const QString &id,
                                                            const QSize &requestedSize)
{
    QString packId;
    int zoom = 0;
    int x = 0;
    int y = 0;
    LocalXyzPack pack{false, QString(), 0, 0};
    if (m_mapPacks && parseTileId(id, &packId, &zoom, &x, &y))
        pack = m_mapPacks->localXyzPackInfo(packId);

    return new TileImageResponse(
        [pack, zoom, x, y, requestedSize]()
        { return loadTileImageFromPack(pack, zoom, x, y, requestedSize); },
        requestedSize);
}

QImage TileImageProvider::loadTileImage(const QString &id, const QSize &requestedSize) const
{
    if (!m_mapPacks)
        return emptyTile(requestedSize);

    QString packId;
    int zoom = 0;
    int x = 0;
    int y = 0;
    if (!parseTileId(id, &packId, &zoom, &x, &y))
        return emptyTile(requestedSize);

    return loadTileImageFromPack(m_mapPacks->localXyzPackInfo(packId), zoom, x, y, requestedSize);
}

} // namespace animus
