#include "Utils/FakeNetworkSocket.h"

#include <type_traits>
#include <functional>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <bitset>


LibFlute::FakeNetworkSocket::FakeNetworkSocket(size_t sender_capacity, size_t network_capacity, size_t receiver_capacity, boost::asio::io_service& sender_io_service, boost::asio::io_service& receiver_io_service)
    : sender_to_network_buffer(sender_capacity),
      network_buffer(network_capacity),
      network_to_receiver_buffer(receiver_capacity),
      sender_io_service(sender_io_service),
      receiver_io_service(receiver_io_service),
      metricsInstance(LibFlute::Metric::Metrics::getInstance()){
    // Constructor implementation
}

void LibFlute::FakeNetworkSocket::async_send_to(const boost::asio::const_buffer& buffer, LibFlute::WriteHandler handler) {
    ZoneScopedN("FakeNetworkSocket::async_send_to");
    // Create a string buffer
    std::string buffer_str(boost::asio::buffer_cast<const char*>(buffer), boost::asio::buffer_size(buffer));
    // Create a copy of the buffer_str
    std::string buffer_str_copy = buffer_str;
    std::size_t bytes_transferred = buffer_str_copy.size();

    // Asynchronous send implementation
    sender_io_service.post([&, buffer_str_copy, handler, bytes_transferred]() mutable {
        ZoneScopedN("FakeNetworkSocket::async_send_to::post_handler");


        std::unique_lock<LockableBase(std::mutex)> sender_lock(sender_to_network_mutex);
        if (sender_to_network_buffer.full()) {
            spdlog::warn("[NETWORK] Sender buffer is full, dropping oldest packet");
        }
        sender_to_network_buffer.push_back(buffer_str_copy);
        sender_lock.unlock();

        // Call the handler asynchronously
        sender_io_service.post([&, handler, bytes_transferred]() mutable {
            boost::system::error_code ec;
            handler(ec, bytes_transferred);
        });
    });
}

void LibFlute::FakeNetworkSocket::async_receive_from(const boost::asio::mutable_buffer& buffer, LibFlute::ReadHandler handler) {    
    // Simulate async operation by posting the handler with success and the actual number of bytes transferred
    receiver_io_service.post([&, buffer, handler]() mutable {
        std::unique_lock<LockableBase(std::mutex)> receiver_lock(network_to_receiver_mutex);
        if (network_to_receiver_buffer.empty()) {
            receiver_lock.unlock();
            handler(boost::system::error_code(), 0);
            return;
        }

        ZoneScopedN("FakeNetworkSocket::async_receive_from::post_handler");

        std::string data = network_to_receiver_buffer.front();
        network_to_receiver_buffer.pop_front();
        receiver_lock.unlock();


        // We ignore all the data that doesn't fit into the buffer,
        // the receiver should have pushed data that is at most MTU size
        auto min_size = std::min(data.size(), boost::asio::buffer_size(buffer));

        //spdlog::info("[NETWORK] Pushing to receiver: {} bytes", min_size);

        std::memcpy(boost::asio::buffer_cast<char*>(buffer), data.c_str(), min_size);

        handler(boost::system::error_code(), min_size);
    });
}

void LibFlute::FakeNetworkSocket::move_item_from_sender_to_network() {
    // Move data from sender buffer to network buffer
    // Process data from sender buffer to network buffer
    std::unique_lock<LockableBase(std::mutex)> sender_lock(sender_to_network_mutex);
    if (sender_to_network_buffer.empty()) {
        sender_lock.unlock();
        return;
    }

    ZoneScopedN("FakeNetworkSocket::move_item_from_sender_to_network");

    std::string data = sender_to_network_buffer.front();
    sender_to_network_buffer.pop_front();
    sender_lock.unlock();

    auto lr = loss_rate.load();

    if (lr> 0.0 && lr <= 1.0) {
        double random = ((double) rand() / (RAND_MAX));
        if (random < lr) {
            // Drop packet
            spdlog::trace("[NETWORK] Dropped packet");
            /*
            auto len_to_display = data.size() < 50 ? data.size() : 50;
            std::stringstream hex_stream;
            std::stringstream ascii_stream;
            std::stringstream binary_stream;
            hex_stream << std::hex << std::setfill('0');
            for (int i = 0; i < len_to_display; ++i) {
                auto c = data[i];
                hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
                if (i < len_to_display / 4) {
                    binary_stream << std::bitset<8>(static_cast<unsigned char>(c)); // Add binary stream
                }
                if (c >= 32 && c <= 126) {
                    ascii_stream << c;
                } else {
                    ascii_stream << '?';
                }
                if (i % 2 == 1) {
                    hex_stream << ' ';  // Add a space every two characters
                    ascii_stream << ' ' << ' ' << ' ';
                    binary_stream << ' ';
                } else if (i < len_to_display / 4) {
                    binary_stream << '.';
                }
            }

            spdlog::trace("[NETWORK] First {} / {} bytes in hex:   {}", len_to_display, data.size(), hex_stream.str());
            spdlog::trace("[NETWORK] First {} / {} bytes in ascii: {}", len_to_display, data.size(), ascii_stream.str());
            spdlog::trace("[NETWORK] First {}  / {} bytes in bin:   {}", len_to_display / 8, data.size(), binary_stream.str());
            */
            return;
        }
    }

    std::unique_lock<LockableBase(std::mutex)> network_lock(network_mutex);
    if (network_buffer.full()) {
        spdlog::warn("[NETWORK] Network buffer is full, dropping oldest packet");
    }
    network_buffer.push_back(data);
    network_lock.unlock();
}

void LibFlute::FakeNetworkSocket::move_item_from_network_to_receiver() {
    // Move data from network buffer to receiver buffer
    std::unique_lock<LockableBase(std::mutex)> network_lock(network_mutex);
    if (network_buffer.empty()) {
        network_lock.unlock();
        return;
    }

    ZoneScopedN("FakeNetworkSocket::move_item_from_network_to_receiver");

    std::string data = network_buffer.front();
    network_buffer.pop_front();
    network_lock.unlock();

    std::unique_lock<LockableBase(std::mutex)> receiver_lock(network_to_receiver_mutex);
    if (network_to_receiver_buffer.full()) {
        spdlog::warn("Receiver buffer is full, dropping oldest packet");
    }
    network_to_receiver_buffer.push_back(data);
    receiver_lock.unlock();
}

void LibFlute::FakeNetworkSocket::set_loss_rate(double loss_rate) {
    // Set loss rate implementation
    this->loss_rate.store(loss_rate);
}

void LibFlute::FakeNetworkSocket::start_threads() {
    ZoneScopedN("FakeNetworkSocket::start_threads");
    // Start background threads for processing
    terminate_threads.store(false);
    sender_thread = std::jthread(&FakeNetworkSocket::sender_thread_function, this);
    receiver_thread = std::jthread(&FakeNetworkSocket::receiver_thread_function, this);
}

void LibFlute::FakeNetworkSocket::stop_threads() {
    // Check if termination is already in progress
    if (terminate_threads.load()) {
        return;
    }
    // Stop background threads
    terminate_threads.store(true);
    if (sender_thread.joinable()) {
        sender_thread.join();
        // spdlog::debug("[NETWORK] FakeNetworkSocket: Sender to network thread joined");
    }
    if (receiver_thread.joinable()) {
        receiver_thread.join();
        // spdlog::debug("[NETWORK] FakeNetworkSocket: Receiver from network thread joined");
    }
}

LibFlute::FakeNetworkSocket::~FakeNetworkSocket() {
    // Destructor to join threads on object destruction
    spdlog::debug("[NETWORK] Destroying FakeNetworkSocket");
    stop_threads();
}

void LibFlute::FakeNetworkSocket::sender_thread_function() {
    metricsInstance.addThread(std::this_thread::get_id(), "FakeNetworkSocket::sender_thread");
    // Sender thread function implementation
    while (!terminate_threads.load()) {
        move_item_from_sender_to_network();
    }
}

void LibFlute::FakeNetworkSocket::receiver_thread_function() {
    metricsInstance.addThread(std::this_thread::get_id(), "FakeNetworkSocket::receiver_thread");
    // Receiver thread function implementation
    while (!terminate_threads.load()) {
        // Process data from network buffer to receiver buffer
        move_item_from_network_to_receiver();
    }
}
