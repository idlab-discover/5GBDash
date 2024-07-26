#pragma once

#include <boost/asio.hpp> // Include necessary Boost.Asio headers
#include <boost/circular_buffer.hpp>
#include <functional> // Include necessary headers for std::function
#include <mutex>
#include <atomic>
#include <thread>


#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"

#include "public/tracy/Tracy.hpp"
#include "public/common/TracySystem.hpp"

namespace LibFlute {

    using AsyncHandler = std::function<void(boost::system::error_code, std::size_t)>;
    using ReadHandler = AsyncHandler;
    using WriteHandler = AsyncHandler;

    using RetrieveFunction = std::function<std::string(const std::string&)>;


    class FakeNetworkSocket {
    public:
        // Constructor to initialize the circular buffers with specified capacities
        FakeNetworkSocket(size_t sender_capacity, size_t network_capacity, size_t receiver_capacity, boost::asio::io_service& sender_io_service, boost::asio::io_service& receiver_io_service);
        
        // Destructor to join threads on object destruction
        virtual ~FakeNetworkSocket();

        // Asynchronously send data to a remote endpoint
        void async_send_to(const boost::asio::const_buffer& buffer,
            LibFlute::WriteHandler handler);

        // Asynchronously receive data from a remote endpoint
        void async_receive_from(const boost::asio::mutable_buffer& buffers,
            LibFlute::ReadHandler handler);


        // Move data from sender buffer to network buffer
        void move_item_from_sender_to_network();

        // Move data from network buffer to receiver buffer
        void move_item_from_network_to_receiver();

        // Set packet loss rate (0.0 - 1.0)
        void set_loss_rate(double loss_rate);

        // Start background threads for processing
        void start_threads();

        // Stop background threads
        void stop_threads();

        void set_retrieve_function(RetrieveFunction retrieveFunction) {
            this->retrieveFunction = retrieveFunction;
        }

        std::string retrieve(const std::string &request_text) {
            if (!retrieveFunction) {
                // Return an empty vector if no retrieve function is set
                return {};
            }
            return retrieveFunction(request_text);
        }

    private:
        // Circular buffers for sender, network, and receiver
        boost::circular_buffer_space_optimized<std::string> sender_to_network_buffer;
        boost::circular_buffer_space_optimized<std::string> network_buffer;
        boost::circular_buffer_space_optimized<std::string> network_to_receiver_buffer;

        // Mutexes to ensure thread safety for accessing buffers
        TracyLockable(std::mutex, sender_to_network_mutex);
        TracyLockable(std::mutex, network_mutex);
        TracyLockable(std::mutex, network_to_receiver_mutex);

        // Atomic variable to hold the loss rate
        std::atomic<double> loss_rate = 0.0; // Atomic for safe concurrent access

        // Threads for processing data asynchronously
        std::jthread sender_thread;
        std::jthread receiver_thread;

        // IO context for sender thread
        boost::asio::io_service& sender_io_service;
        // IO context for receiver thread
        boost::asio::io_service& receiver_io_service;

        // Flag to signal thread termination
        std::atomic<bool> terminate_threads = true;

        LibFlute::Metric::Metrics& metricsInstance;

        RetrieveFunction retrieveFunction;

        // Helper methods for thread execution
        void sender_thread_function();
        void receiver_thread_function();
    };

} // namespace LibFlute
