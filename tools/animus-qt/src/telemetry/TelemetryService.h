#pragma once

#include "telemetry/MavlinkDecoder.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QUdpSocket;

namespace animus
{

class BreadcrumbPathModel;
class VehicleModel;

class TelemetryService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int uiRateHz READ uiRateHz WRITE setUiRateHz NOTIFY uiRateHzChanged)
    Q_PROPERTY(quint16 udpPort READ udpPort WRITE setUdpPort NOTIFY udpEndpointChanged)
    Q_PROPERTY(QString udpHost READ udpHost WRITE setUdpHost NOTIFY udpEndpointChanged)
    Q_PROPERTY(int datagramCount READ datagramCount NOTIFY countersChanged)
    Q_PROPERTY(int decodedSampleCount READ decodedSampleCount NOTIFY countersChanged)
    Q_PROPERTY(int decodeErrorCount READ decodeErrorCount NOTIFY countersChanged)
    Q_PROPERTY(double lastDatagramAgeS READ lastDatagramAgeS NOTIFY freshnessChanged)
    Q_PROPERTY(double lastDecodedAgeS READ lastDecodedAgeS NOTIFY freshnessChanged)
    Q_PROPERTY(bool linkFresh READ linkFresh NOTIFY freshnessChanged)

  public:
    explicit TelemetryService(VehicleModel *vehicle,
                              BreadcrumbPathModel *trail,
                              QObject *parent = nullptr);
    ~TelemetryService() override = default;

    bool running() const;
    int uiRateHz() const;
    void setUiRateHz(int uiRateHz);
    quint16 udpPort() const;
    void setUdpPort(quint16 udpPort);
    QString udpHost() const;
    void setUdpHost(const QString &udpHost);
    int datagramCount() const;
    int decodedSampleCount() const;
    int decodeErrorCount() const;
    double lastDatagramAgeS() const;
    double lastDecodedAgeS() const;
    bool linkFresh() const;

    Q_INVOKABLE void startMockTelemetry();
    Q_INVOKABLE bool startUdpTelemetry();
    Q_INVOKABLE void stop();
    bool ingestDatagram(const QByteArray &datagram);
    void updateFreshnessForElapsedMs(qint64 elapsedMs);

  signals:
    void runningChanged();
    void uiRateHzChanged();
    void udpEndpointChanged();
    void countersChanged();
    void freshnessChanged();

  private slots:
    void publishMockSample();
    void readPendingDatagrams();
    void publishPendingSample();

  private:
    void applySample(const MavlinkTelemetrySample &sample);
    void mergePendingSample(const MavlinkTelemetrySample &sample);
    qint64 elapsedMs() const;
    void ensureClockStarted();
    void markDatagramReceived();
    void markDecodedSample();
    void updateFreshness(qint64 nowMs);
    void setRunning(bool running);

    VehicleModel *m_vehicle;
    BreadcrumbPathModel *m_trail;
    MavlinkDecoder m_decoder;
    QUdpSocket *m_socket;
    QTimer m_timer;
    QTimer m_publishTimer;
    bool m_running;
    bool m_mockRunning;
    bool m_hasPendingSample;
    bool m_hasDatagramTime;
    bool m_hasDecodedTime;
    bool m_linkFresh;
    int m_uiRateHz;
    int m_datagramCount;
    int m_decodedSampleCount;
    int m_decodeErrorCount;
    quint16 m_udpPort;
    QString m_udpHost;
    double m_elapsedS;
    qint64 m_lastDatagramMs;
    qint64 m_lastDecodedMs;
    qint64 m_freshnessTimeoutMs;
    QElapsedTimer m_clock;
    MavlinkTelemetrySample m_pendingSample;
};

} // namespace animus
