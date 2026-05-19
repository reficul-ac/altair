#include "maps/CesiumBridge.h"
#include "maps/qgc/AnimusMapCacheManager.h"
#include "maps/MapSourceRegistry.h"
#include "maps/OfflineMapManager.h"
#include "models/VehicleModel.h"
#include "telemetry/BreadcrumbPathModel.h"
#include "telemetry/TelemetryService.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

namespace
{

class CaptureWriter final : public QObject
{
    Q_OBJECT

  public:
    Q_INVOKABLE bool writePngDataUrl(const QString &path, const QString &dataUrl)
    {
        const QString prefix = QStringLiteral("data:image/png;base64,");
        if (!dataUrl.startsWith(prefix))
            return false;
        const QByteArray png = QByteArray::fromBase64(dataUrl.mid(prefix.size()).toLatin1());
        if (png.isEmpty())
            return false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write(png) == png.size();
    }
};

struct CaptureOptions
{
    QString captureDir;
    QString captureWorkspace;
    QString udpHost = QStringLiteral("127.0.0.1");
    bool mockTelemetry = false;
    bool startUdpTelemetry = false;
    int captureDelayMs = 1000;
    int udpPort = 14551;
    bool quitAfterCapture = false;
    bool captureTerrainWebEngine = false;
    bool requested = false;
};

bool parseArgs(const QStringList &args, CaptureOptions *options)
{
    for (int i = 1; i < args.size(); ++i)
    {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--capture-dir"))
        {
            if (i + 1 >= args.size())
                return false;
            options->captureDir = args.at(++i);
            options->requested = true;
        }
        else if (arg == QStringLiteral("--capture-workspace"))
        {
            if (i + 1 >= args.size())
                return false;
            options->captureWorkspace = args.at(++i);
            options->requested = true;
        }
        else if (arg == QStringLiteral("--mock-telemetry"))
        {
            options->mockTelemetry = true;
        }
        else if (arg == QStringLiteral("--start-udp-telemetry"))
        {
            options->startUdpTelemetry = true;
        }
        else if (arg == QStringLiteral("--udp-host"))
        {
            if (i + 1 >= args.size())
                return false;
            options->udpHost = args.at(++i);
        }
        else if (arg == QStringLiteral("--udp-port"))
        {
            if (i + 1 >= args.size())
                return false;
            bool ok = false;
            const int port = args.at(++i).toInt(&ok);
            if (!ok || port <= 0 || port > 65535)
                return false;
            options->udpPort = port;
        }
        else if (arg == QStringLiteral("--capture-delay-ms"))
        {
            if (i + 1 >= args.size())
                return false;
            bool ok = false;
            const int delay = args.at(++i).toInt(&ok);
            if (!ok || delay < 0)
                return false;
            options->captureDelayMs = delay;
        }
        else if (arg == QStringLiteral("--quit-after-capture"))
        {
            options->quitAfterCapture = true;
        }
        else if (arg == QStringLiteral("--capture-terrain-webengine"))
        {
            options->captureTerrainWebEngine = true;
        }
    }

    if (!options->requested)
        return true;
    if (options->captureDir.isEmpty() || options->captureWorkspace.isEmpty())
        return false;
    return options->captureWorkspace == QStringLiteral("map-2d") ||
           options->captureWorkspace == QStringLiteral("terrain-3d") ||
           options->captureWorkspace == QStringLiteral("setup");
}

} // namespace

int main(int argc, char *argv[])
{
    CaptureOptions capture;
    QStringList args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i)
        args.push_back(QString::fromLocal8Bit(argv[i]));
    if (!parseArgs(args, &capture))
    {
        qCritical("invalid animus_qt capture arguments");
        return 2;
    }

    const bool webEngineTerrainEnabled = !capture.requested ||
                                         capture.captureWorkspace == QStringLiteral("terrain-3d") ||
                                         capture.captureTerrainWebEngine;
    if (webEngineTerrainEnabled)
        QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);

    animus::VehicleModel vehicle;
    animus::BreadcrumbPathModel trail;
    animus::MapSourceRegistry mapSources;
    animus::OfflineMapManager offlineMaps(&mapSources);
    animus::AnimusMapCacheManager mapCache;
    animus::TelemetryService telemetry(&vehicle, &trail);
    animus::CesiumBridge cesium(&vehicle, &trail);
    CaptureWriter captureWriter;

    mapCache.ensureDefaultCruise6DofTileSet();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("vehicleModel"), &vehicle);
    engine.rootContext()->setContextProperty(QStringLiteral("breadcrumbModel"), &trail);
    engine.rootContext()->setContextProperty(QStringLiteral("mapSources"), &mapSources);
    engine.rootContext()->setContextProperty(QStringLiteral("offlineMaps"), &offlineMaps);
    engine.rootContext()->setContextProperty(QStringLiteral("mapCache"), &mapCache);
    engine.rootContext()->setContextProperty(QStringLiteral("telemetryService"), &telemetry);
    engine.rootContext()->setContextProperty(QStringLiteral("cesiumBridge"), &cesium);
    engine.rootContext()->setContextProperty(QStringLiteral("captureWriter"), &captureWriter);
    engine.rootContext()->setContextProperty(QStringLiteral("webEngineTerrainEnabled"),
                                             webEngineTerrainEnabled);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/Animus/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (capture.startUdpTelemetry)
    {
        telemetry.setUdpHost(capture.udpHost);
        telemetry.setUdpPort(static_cast<quint16>(capture.udpPort));
        QTimer::singleShot(0,
                           &telemetry,
                           [&telemetry]()
                           {
                               if (!telemetry.startUdpTelemetry())
                                   qWarning("failed to start Animus UDP telemetry");
                           });
    }

    if (capture.requested)
    {
        QObject *root = engine.rootObjects().constFirst();
        if (capture.mockTelemetry)
            telemetry.startMockTelemetry();
        const bool selected = QMetaObject::invokeMethod(
            root, "selectWorkspace", Q_ARG(QVariant, QVariant(capture.captureWorkspace)));
        if (!selected)
        {
            qCritical("failed to select capture workspace");
            return 3;
        }

        QTimer::singleShot(
            capture.captureDelayMs,
            &app,
            [&app, root, capture]()
            {
                QQuickWindow *window = qobject_cast<QQuickWindow *>(root);
                if (!window)
                {
                    qCritical("root QML object is not a QQuickWindow");
                    QCoreApplication::exit(4);
                    return;
                }

                QDir dir(capture.captureDir);
                if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
                {
                    qCritical("failed to create capture directory");
                    QCoreApplication::exit(5);
                    return;
                }

                const QString path =
                    dir.filePath(capture.captureWorkspace + QStringLiteral(".png"));
                if (capture.captureWorkspace == QStringLiteral("terrain-3d"))
                {
                    QObject *terrainView =
                        root->findChild<QObject *>(QStringLiteral("terrain3DView"));
                    if (!terrainView)
                    {
                        qCritical("failed to find terrain 3D view for capture");
                        QCoreApplication::exit(6);
                        return;
                    }
                    QEventLoop loop;
                    QObject::connect(
                        terrainView, SIGNAL(captureFinished(bool, QString)), &loop, SLOT(quit()));
                    terrainView->setProperty("lastCaptureOk", false);
                    terrainView->setProperty("lastCaptureError",
                                             QStringLiteral("capture timed out"));
                    const bool invoked = QMetaObject::invokeMethod(
                        terrainView, "captureCesiumPng", Q_ARG(QVariant, path));
                    if (!invoked)
                    {
                        qCritical("failed to invoke terrain 3D capture");
                        QCoreApplication::exit(6);
                        return;
                    }
                    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
                    loop.exec();
                    const bool captureOk = terrainView->property("lastCaptureOk").toBool();
                    const QString captureError =
                        terrainView->property("lastCaptureError").toString();
                    if (!captureOk)
                    {
                        qCritical("failed to write terrain 3D Cesium capture: %s",
                                  qPrintable(captureError));
                        QCoreApplication::exit(6);
                        return;
                    }
                }
                else
                {
                    const QImage image = window->grabWindow();
                    if (image.isNull() || !image.save(path, "PNG"))
                    {
                        qCritical("failed to write capture screenshot");
                        QCoreApplication::exit(6);
                        return;
                    }
                }
                if (!QFileInfo::exists(path))
                {
                    qCritical("failed to write capture screenshot");
                    QCoreApplication::exit(6);
                    return;
                }
                if (capture.quitAfterCapture)
                    QCoreApplication::quit();
            });
    }

    return app.exec();
}

#include "main.moc"
