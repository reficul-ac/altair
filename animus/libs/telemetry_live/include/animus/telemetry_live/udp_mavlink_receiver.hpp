#pragma once

#include "animus/telemetry_core/telemetry.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

namespace animus::telemetry_live
{

struct UdpMavlinkReceiverConfig
{
    std::string bind_host = "127.0.0.1";
    std::uint16_t bind_port = 14550;
    std::size_t max_datagram_bytes = 2048U;
};

struct UdpMavlinkDatagram
{
    std::vector<std::uint8_t> bytes;
    double receive_time_s = 0.0;
};

struct UdpMavlinkReceiverStats
{
    std::uint64_t datagrams = 0;
    std::uint64_t bytes = 0;
    std::uint64_t receive_errors = 0;
    std::uint64_t dropped_datagrams = 0;
    std::size_t queued_datagrams = 0;
    std::size_t queue_high_water = 0;
    std::size_t last_drain_datagrams = 0;
    std::size_t last_drain_queue_before = 0;
    double last_packet_age_s = 0.0;
    bool connected = false;
    bool stale = true;
};

class UdpMavlinkReceiver
{
  public:
    explicit UdpMavlinkReceiver(UdpMavlinkReceiverConfig config);
    ~UdpMavlinkReceiver();

    UdpMavlinkReceiver(const UdpMavlinkReceiver &) = delete;
    UdpMavlinkReceiver &operator=(const UdpMavlinkReceiver &) = delete;

    void start();
    void stop();
    [[nodiscard]] std::vector<UdpMavlinkDatagram> drain();
    [[nodiscard]] UdpMavlinkReceiverStats stats() const;
    [[nodiscard]] std::string local_endpoint() const;

  private:
    void receive_next();
    [[nodiscard]] double now_s() const;

    UdpMavlinkReceiverConfig config_;
    asio::io_context io_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint sender_;
    std::vector<std::uint8_t> receive_buffer_;
    std::thread thread_;

    mutable std::mutex mutex_;
    std::vector<UdpMavlinkDatagram> queue_;
    UdpMavlinkReceiverStats stats_;
    std::optional<double> last_packet_time_s_;
    double start_time_s_ = 0.0;
    bool running_ = false;
};

} // namespace animus::telemetry_live
