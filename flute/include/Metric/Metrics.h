
#pragma once

#include "Metric/Gauge.h"
#include "Metric/ThreadedCPUUsage.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
namespace Metric {

class ThreadedCPUUsage;

class Metrics {
public:
    static Metrics& getInstance();

    std::shared_ptr<Gauge> getOrCreateGauge(const std::string& name);
    void setLogFile(const std::string& filename);

    // Add a thread to be monitored for CPU usage
    void addThread(std::jthread::id threadId, std::string threadName);

    // Remove a thread from the list of monitored threads
    void removeThread(std::jthread::id threadId);

private:
    Metrics(); // Private constructor
    Metrics(const Metrics&) = delete; // Delete copy constructor
    Metrics& operator=(const Metrics&) = delete; // Delete assignment operator

    static Metrics* _instance;
    TracyLockable(std::mutex, _mutex); // Mutex to protect the value
    std::string _logFilename;
    std::unordered_map<std::string, std::shared_ptr<Gauge>> _gauges;
    std::unique_ptr<ThreadedCPUUsage> threadedCPUUsage; // Threaded CPU usage gauge
};

}  // namespace Metric
}  // namespace LibFlute