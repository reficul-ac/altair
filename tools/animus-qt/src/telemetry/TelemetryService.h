#pragma once

#include "telemetry/MavlinkDecoder.h"

#include <QByteArray>
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

    Q_INVOKABLE void startMockTelemetry();
    Q_INVOKABLE bool startUdpTelemetry();
    Q_INVOKABLE void stop();
    bool ingestDatagram(const QByteArray &datagram);

  signals:
    void runningChanged();
    void uiRateHzChanged();
    void udpEndpointChanged();

  private slots:
    void publishMockSample();
    void readPendingDatagrams();
    void publishPendingSample();

  private:
    void applySample(const MavlinkTelemetrySample &sample);
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
    int m_uiRateHz;
    quint16 m_udpPort;
    QString m_udpHost;
    double m_elapsedS;
    MavlinkTelemetrySample m_pendingSample;
};

} // namespace animus
