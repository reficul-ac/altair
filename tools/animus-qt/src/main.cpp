#include "maps/CesiumBridge.h"
#include "maps/qgc/AnimusMapCacheManager.h"
#include "maps/MapSourceRegistry.h"
#include "maps/NavigationOverlayModels.h"
#include "maps/OfflineMapManager.h"
#include "models/VehicleModelProfileManager.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
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

    Q_INVOKABLE bool writeTextFile(const QString &path, const QString &text)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        const QByteArray data = text.toUtf8();
        return file.write(data) == data.size();
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
    bool verifyTerrainControlSurfaces = false;
    bool seedMapCacheFixture = false;
    bool requested = false;
    QString mapCacheRoot;
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
        else if (arg == QStringLiteral("--verify-terrain-control-surfaces"))
        {
            options->verifyTerrainControlSurfaces = true;
        }
        else if (arg == QStringLiteral("--map-cache-root"))
        {
            if (i + 1 >= args.size())
                return false;
            options->mapCacheRoot = args.at(++i);
        }
        else if (arg == QStringLiteral("--seed-map-cache-fixture"))
        {
            options->seedMapCacheFixture = true;
        }
    }

    if (!options->requested)
        return true;
    if (options->captureDir.isEmpty() || options->captureWorkspace.isEmpty())
        return false;
    return options->captureWorkspace == QStringLiteral("map-2d") ||
           options->captureWorkspace == QStringLiteral("terrain-3d") ||
           options->captureWorkspace == QStringLiteral("fpv") ||
           options->captureWorkspace == QStringLiteral("tactical") ||
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

    const bool webEngineTerrainEnabled =
        !capture.requested || capture.captureWorkspace == QStringLiteral("terrain-3d") ||
        capture.captureWorkspace == QStringLiteral("fpv") ||
        capture.captureWorkspace == QStringLiteral("tactical") || capture.captureTerrainWebEngine;
    if (webEngineTerrainEnabled)
        QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Altair"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimusQt"));

    animus::VehicleModel vehicle;
    animus::BreadcrumbPathModel trail;
    animus::MapSourceRegistry mapSources;
    animus::OfflineMapManager offlineMaps(&mapSources);
    animus::AnimusMapCacheManager mapCache;
    animus::NavigationOverlayModels navigationOverlays;
    animus::TelemetryService telemetry(&vehicle, &trail);
    QSettings settings;
    animus::VehicleModelProfileManager modelProfiles(
        QDir(QStringLiteral(ANIMUS_REPO_ROOT))
            .filePath(QStringLiteral("tools/animus-qt/web/cesium/models")),
        &settings,
        &vehicle);
    if (capture.requested)
        navigationOverlays.seedCruise6DofFixture();

    animus::CesiumBridge cesium(&vehicle, &trail, &modelProfiles, &navigationOverlays);
    CaptureWriter captureWriter;

    if (!capture.mapCacheRoot.isEmpty())
        mapCache.setRootPath(capture.mapCacheRoot);
    if (capture.seedMapCacheFixture)
        mapCache.seedDefaultCruise6DofFixtureTiles();
    else
        mapCache.ensureDefaultCruise6DofTileSet();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("vehicleModel"), &vehicle);
    engine.rootContext()->setContextProperty(QStringLiteral("breadcrumbModel"), &trail);
    engine.rootContext()->setContextProperty(QStringLiteral("navigationOverlays"),
                                             &navigationOverlays);
    engine.rootContext()->setContextProperty(QStringLiteral("mapSources"), &mapSources);
    engine.rootContext()->setContextProperty(QStringLiteral("offlineMaps"), &offlineMaps);
    engine.rootContext()->setContextProperty(QStringLiteral("mapCache"), &mapCache);
    engine.rootContext()->setContextProperty(QStringLiteral("telemetryService"), &telemetry);
    engine.rootContext()->setContextProperty(QStringLiteral("vehicleModelProfiles"),
                                             &modelProfiles);
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
            [&app, root, capture, &cesium]()
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
                if (capture.captureWorkspace == QStringLiteral("terrain-3d") ||
                    capture.captureWorkspace == QStringLiteral("fpv") ||
                    capture.captureWorkspace == QStringLiteral("tactical"))
                {
                    const bool tactical = capture.captureWorkspace == QStringLiteral("tactical");
                    const bool fpv = capture.captureWorkspace == QStringLiteral("fpv");
                    QObject *webWorkspace = root->findChild<QObject *>(
                        tactical
                            ? QStringLiteral("tacticalAttitudeView")
                            : (fpv ? QStringLiteral("fpvView") : QStringLiteral("terrain3DView")));
                    if (!webWorkspace)
                    {
                        qCritical("failed to find WebEngine workspace view for capture");
                        QCoreApplication::exit(6);
                        return;
                    }
                    if (capture.verifyTerrainControlSurfaces)
                    {
                        QEventLoop inspectLoop;
                        QObject::connect(webWorkspace,
                                         SIGNAL(controlSurfaceInspectionFinished(bool, QString)),
                                         &inspectLoop,
                                         SLOT(quit()));
                        webWorkspace->setProperty("lastControlSurfaceInspectionOk", false);
                        webWorkspace->setProperty("lastControlSurfaceInspectionError",
                                                  QStringLiteral("inspection timed out"));
                        const QString diagnosticPath = dir.filePath(
                            capture.captureWorkspace + QStringLiteral("-control-surfaces.json"));
                        const QVariantMap verificationSnapshot =
                            cesium.controlSurfaceVerificationSnapshot();
                        const bool inspectInvoked =
                            QMetaObject::invokeMethod(webWorkspace,
                                                      "inspectControlSurfaces",
                                                      Q_ARG(QVariant, diagnosticPath),
                                                      Q_ARG(QVariant, verificationSnapshot));
                        if (!inspectInvoked)
                        {
                            qCritical("failed to invoke terrain 3D control-surface inspection");
                            QCoreApplication::exit(6);
                            return;
                        }
                        QTimer::singleShot(20000, &inspectLoop, &QEventLoop::quit);
                        inspectLoop.exec();
                        const bool inspectOk =
                            webWorkspace->property("lastControlSurfaceInspectionOk").toBool();
                        const QString inspectError =
                            webWorkspace->property("lastControlSurfaceInspectionError").toString();
                        if (!inspectOk)
                        {
                            qCritical("terrain 3D control-surface inspection failed: %s",
                                      qPrintable(inspectError));
                            QCoreApplication::exit(6);
                            return;
                        }
                    }
                    if (tactical || fpv)
                    {
                        QEventLoop cameraLoop;
                        QObject::connect(webWorkspace,
                                         SIGNAL(controlSurfaceInspectionFinished(bool, QString)),
                                         &cameraLoop,
                                         SLOT(quit()));
                        webWorkspace->setProperty("lastControlSurfaceInspectionOk", false);
                        webWorkspace->setProperty("lastControlSurfaceInspectionError",
                                                  QStringLiteral("camera inspection timed out"));
                        const QString diagnosticPath =
                            dir.filePath(tactical ? QStringLiteral("tactical-camera.json")
                                                  : QStringLiteral("fpv-camera.json"));
                        const bool cameraInvoked = QMetaObject::invokeMethod(
                            webWorkspace, "inspectCameraState", Q_ARG(QVariant, diagnosticPath));
                        if (!cameraInvoked)
                        {
                            qCritical("failed to invoke WebEngine camera inspection");
                            QCoreApplication::exit(6);
                            return;
                        }
                        QTimer::singleShot(10000, &cameraLoop, &QEventLoop::quit);
                        cameraLoop.exec();
                        const bool cameraOk =
                            webWorkspace->property("lastControlSurfaceInspectionOk").toBool();
                        if (!cameraOk)
                        {
                            qCritical("WebEngine camera inspection failed");
                            QCoreApplication::exit(6);
                            return;
                        }
                    }

                    QEventLoop loop;
                    QObject::connect(
                        webWorkspace, SIGNAL(captureFinished(bool, QString)), &loop, SLOT(quit()));
                    webWorkspace->setProperty("lastCaptureOk", false);
                    webWorkspace->setProperty("lastCaptureError",
                                              QStringLiteral("capture timed out"));
                    const bool invoked = QMetaObject::invokeMethod(
                        webWorkspace, "captureCesiumPng", Q_ARG(QVariant, path));
                    if (!invoked)
                    {
                        qCritical("failed to invoke terrain 3D capture");
                        QCoreApplication::exit(6);
                        return;
                    }
                    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
                    loop.exec();
                    const bool captureOk = webWorkspace->property("lastCaptureOk").toBool();
                    const QString captureError =
                        webWorkspace->property("lastCaptureError").toString();
                    if (!captureOk)
                    {
                        if (tactical)
                        {
                            qCritical("tactical Cesium/WebEngine capture failed: %s",
                                      qPrintable(captureError));
                            QCoreApplication::exit(6);
                            return;
                        }
                        const QImage fallbackImage = window->grabWindow();
                        if (fallbackImage.isNull() || !fallbackImage.save(path, "PNG"))
                        {
                            qCritical("failed to write fallback WebEngine workspace capture: %s",
                                      qPrintable(captureError));
                            QCoreApplication::exit(6);
                            return;
                        }
                    }
                    const QString workspacePath =
                        dir.filePath(capture.captureWorkspace + QStringLiteral("-workspace.png"));
                    const QImage workspaceImage = window->grabWindow();
                    if (workspaceImage.isNull() || !workspaceImage.save(workspacePath, "PNG"))
                    {
                        qCritical("failed to write WebEngine workspace capture");
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
                QVariant chromeDiagnosticJson;
                if (QMetaObject::invokeMethod(root,
                                              "workspaceChromeDiagnosticsJson",
                                              Q_RETURN_ARG(QVariant, chromeDiagnosticJson)))
                {
                    QFile chromeFile(
                        dir.filePath(capture.captureWorkspace + QStringLiteral("-chrome.json")));
                    if (chromeFile.open(QIODevice::WriteOnly | QIODevice::Text))
                        chromeFile.write(chromeDiagnosticJson.toString().toUtf8());
                }
                if (capture.captureWorkspace == QStringLiteral("terrain-3d"))
                {
                    QFile clearanceFile(dir.filePath(QStringLiteral("terrain-3d-clearance.json")));
                    if (clearanceFile.open(QIODevice::WriteOnly | QIODevice::Text))
                    {
                        clearanceFile.write(
                            QJsonDocument::fromVariant(
                                cesium.snapshot().value(QStringLiteral("clearance")))
                                .toJson(QJsonDocument::Indented));
                    }
                }
                if (capture.quitAfterCapture)
                    QCoreApplication::quit();
            });
    }

    return app.exec();
}

#include "main.moc"
