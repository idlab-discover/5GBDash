#include "Metric/ThreadedCPUUsage.h"

#include "spdlog/spdlog.h"
#include "public/common/TracySystem.hpp"

namespace LibFlute {
namespace Metric {

ThreadedCPUUsage::ThreadedCPUUsage(): stopThread(false) {
    // Start the thread for measuring CPU usage
    cpuUsageThread = std::jthread(&ThreadedCPUUsage::measureCPUUsageThread, this);
}

ThreadedCPUUsage::~ThreadedCPUUsage() {
    spdlog::debug("ThreadedCPUUsage: Destructor called");
    // Signal the thread to stop
    stopThread.store(true, std::memory_order_relaxed);
    // Join the CPU usage measurement thread
    if (cpuUsageThread.joinable()) {
        cpuUsageThread.join();
        // spdlog::debug("ThreadedCPUUsage: CPU usage measurement thread joined");
    }
}

// Add a thread to be monitored for CPU usage
void ThreadedCPUUsage::addThread(std::jthread::id threadId, std::string threadName) {
    ZoneScopedN("ThreadedCPUUsage::addThread");
    tracy::SetThreadName(threadName.c_str());

    /*
    std::lock_guard<LockableBase(std::mutex)> lock(threadGaugesMutex);
    // Check if the thread is already being monitored
    for (auto& threadGauge : threadGauges) {
        if (threadGauge.threadId == threadId) {
            // Remove the existing gauge for this thread
            threadGauges.erase(std::remove_if(threadGauges.begin(), threadGauges.end(),
                                              [threadId](const ThreadGauge& threadGauge) {
                                                  return threadGauge.threadId == threadId;
                                              }),
                               threadGauges.end());
        }
    }

    auto newGauge = Metrics::getInstance().getOrCreateGauge("cpu_usage_" + threadName);
    threadGauges.push_back({newGauge, threadId});
    */
}

// Remove a thread from the list of monitored threads
void ThreadedCPUUsage::removeThread(std::jthread::id threadId) {
    ZoneScopedN("ThreadedCPUUsage::removeThread");
    std::lock_guard<LockableBase(std::mutex)> lock(threadGaugesMutex);
    // Remove the thread gauge from the list of thread gauges using c++17
    threadGauges.erase(std::remove_if(threadGauges.begin(), threadGauges.end(),
                                      [threadId](const ThreadGauge& threadGauge) {
                                          return threadGauge.threadId == threadId;
                                      }),
                       threadGauges.end());
}

void ThreadedCPUUsage::measureCPUUsageThread() {
    tracy::SetThreadName("CPU Usage Thread");
    ZoneScopedN("ThreadedCPUUsage::measureCPUUsageThread");
    while (!stopThread.load(std::memory_order_relaxed)) {
        std::unique_lock<LockableBase(std::mutex)> lock(threadGaugesMutex);

        // Measure CPU usage for each thread
        for (auto& threadGauge : threadGauges) {
            try {
                //pthread_t pthreadId = pthread_self();
                //pid_t tid = syscall(SYS_gettid);

                struct rusage usage;
                int result = getrusage(RUSAGE_THREAD, &usage);

                double cpuUsage = (result == 0) ? (static_cast<double>(usage.ru_utime.tv_sec) +
                                                   static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0)
                                                 : 0.0;

                // convert cpuTime to percentage of total CPU time
                cpuUsage = cpuUsage / sysconf(_SC_CLK_TCK);
                // Round to 2 decimal places
                cpuUsage = std::round(cpuUsage * 100) / 100;

                // Update the gauge with the measured CPU usage
                threadGauge.gauge->Set(cpuUsage);
            } catch (...) {
                // Handle exceptions, e.g., if the thread no longer exists
                threadGauge.gauge->Set(-1.0); // Set to -1.0 to indicate an error.
            }
        }


        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(250)); // Adjust sleep interval as needed
    }
}

} // namespace Metric
} // namespace LibFlute
