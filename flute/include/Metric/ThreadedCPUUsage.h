#pragma once

#include "Metric/Gauge.h"
#include "Metric/Metrics.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <math.h>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>
#include <thread>
#include <vector>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
namespace Metric {

class ThreadedCPUUsage {
public:
    ThreadedCPUUsage();

    ~ThreadedCPUUsage();

    // Add a thread to be monitored for CPU usage
    void addThread(std::jthread::id threadId, std::string threadName);

    // Remove a thread from the list of monitored threads
    void removeThread(std::jthread::id threadId);

private:
    struct ThreadGauge {
        std::shared_ptr<Gauge> gauge;
        std::jthread::id threadId;
    };

    std::vector<ThreadGauge> threadGauges; // List of thread gauges
    TracyLockable(std::mutex, threadGaugesMutex); // Mutex to protect access to threadGauges
    std::jthread cpuUsageThread; // Thread for measuring CPU usage
    std::atomic<bool> stopThread; // Atomic flag to signal thread termination

    void measureCPUUsageThread();
};

} // namespace Metric
} // namespace LibFlute
