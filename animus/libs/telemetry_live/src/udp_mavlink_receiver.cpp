#include "animus/telemetry_live/udp_mavlink_receiver.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace animus::telemetry_live
{
namespace
{

constexpr double stale_after_s = 2.0;
constexpr std::size_t max_queued_datagrams = 4096U;

} // namespace

UdpMavlinkReceiver::UdpMavlinkReceiver(UdpMavlinkReceiverConfig config)
    : config_(std::move(config)), socket_(io_), receive_buffer_(config_.max_datagram_bytes)
{
    if (config_.max_datagram_bytes == 0U)
    {
        throw std::invalid_argument("UDP MAVLink receiver datagram size must be positive");
    }
}

UdpMavlinkReceiver::~UdpMavlinkReceiver()
{
    stop();
}

void UdpMavlinkReceiver::start()
{
    if (running_)
    {
        return;
    }
    const auto address = asio::ip::make_address(config_.bind_host);
    socket_.open(asio::ip::udp::v4());
    socket_.bind(asio::ip::udp::endpoint(address, config_.bind_port));
    start_time_s_ = std::chrono::duration<double>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    running_ = true;
    receive_next();
    thread_ = std::thread([this]() { io_.run(); });
}

void UdpMavlinkReceiver::stop()
{
    if (!running_)
    {
        return;
    }
    running_ = false;
    asio::error_code ignored;
    socket_.cancel(ignored);
    socket_.close(ignored);
    io_.stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

std::vector<UdpMavlinkDatagram> UdpMavlinkReceiver::drain()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<UdpMavlinkDatagram> drained;
    stats_.last_drain_queue_before = queue_.size();
    drained.swap(queue_);
    stats_.last_drain_datagrams = drained.size();
    stats_.queued_datagrams = 0U;
    return drained;
}

UdpMavlinkReceiverStats UdpMavlinkReceiver::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    UdpMavlinkReceiverStats snapshot = stats_;
    if (last_packet_time_s_)
    {
        snapshot.last_packet_age_s = std::max(0.0, now_s() - *last_packet_time_s_);
        snapshot.connected = true;
        snapshot.stale = snapshot.last_packet_age_s > stale_after_s;
    }
    return snapshot;
}

std::string UdpMavlinkReceiver::local_endpoint() const
{
    asio::error_code error;
    const auto endpoint = socket_.local_endpoint(error);
    if (error)
    {
        return config_.bind_host + ":" + std::to_string(config_.bind_port);
    }
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

void UdpMavlinkReceiver::receive_next()
{
    socket_.async_receive_from(
        asio::buffer(receive_buffer_),
        sender_,
        [this](const asio::error_code &error, const std::size_t byte_count)
        {
            if (!running_)
            {
                return;
            }
            const double received_at = now_s();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (error)
                {
                    ++stats_.receive_errors;
                }
                else
                {
                    ++stats_.datagrams;
                    stats_.bytes += byte_count;
                    last_packet_time_s_ = received_at;
                    stats_.connected = true;
                    stats_.stale = false;
                    if (queue_.size() >= max_queued_datagrams)
                    {
                        queue_.erase(queue_.begin());
                        ++stats_.dropped_datagrams;
                    }
                    UdpMavlinkDatagram datagram;
                    datagram.receive_time_s = received_at;
                    datagram.bytes.assign(receive_buffer_.begin(),
                                          receive_buffer_.begin() +
                                              static_cast<std::ptrdiff_t>(byte_count));
                    queue_.push_back(std::move(datagram));
                    stats_.queued_datagrams = queue_.size();
                    stats_.queue_high_water =
                        std::max(stats_.queue_high_water, stats_.queued_datagrams);
                }
            }
            receive_next();
        });
}

double UdpMavlinkReceiver::now_s() const
{
    const double absolute_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return absolute_s - start_time_s_;
}

} // namespace animus::telemetry_live
