#include "Metric/Metrics.h"
#include "Metric/Gauge.h"
#include "Metric/ThreadedCPUUsage.h"
#include <memory>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>

#include "public/tracy/Tracy.hpp"

LibFlute::Metric::Metrics* LibFlute::Metric::Metrics::_instance = nullptr;

LibFlute::Metric::Metrics::Metrics(): threadedCPUUsage(std::make_unique<ThreadedCPUUsage>()) {
    // Constructor implementation
}

LibFlute::Metric::Metrics& LibFlute::Metric::Metrics::getInstance() {
    if (_instance == nullptr) {
        _instance = new LibFlute::Metric::Metrics();
    }
    return *_instance;
}

void LibFlute::Metric::Metrics::addThread(std::jthread::id threadId, std::string threadName) {
    threadedCPUUsage->addThread(threadId, threadName);
}

void LibFlute::Metric::Metrics::removeThread(std::jthread::id threadId) {
    threadedCPUUsage->removeThread(threadId);
}

std::shared_ptr<LibFlute::Metric::Gauge> LibFlute::Metric::Metrics::getOrCreateGauge(const std::string& name) {
    std::lock_guard<LockableBase(std::mutex)> lock(_mutex);
    auto it = _gauges.find(name);
    if (it != _gauges.end()) {
        return it->second; // Return existing gauge
    } else {
        auto newGauge = std::make_shared<Gauge>(name, "");
        if (!_logFilename.empty()) {
            newGauge->setLogFile(_logFilename); // Set log filename if available
        }
        _gauges[name] = newGauge;
        return newGauge; // Return newly created gauge
    }
}

void LibFlute::Metric::Metrics::setLogFile(const std::string& filename) {
    _logFilename = filename;
    if (_logFilename.empty()) {
      return;
    }

    // Load the file, iterate over each line, create a map of gauge names and their values
    std::ifstream file(_logFilename);
    if (!file.is_open()) {
      // File not found, not overwriting.
      return;
    }

    std::string line;

    std::map<std::string, double> gaugeValues;


    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::vector<std::string> words;

        std::string word;
        while (std::getline(iss, word, ';')) {
            words.push_back(word);
        }

        if (words.size() < 3) {
            continue;
        }

        // words[0] is the timestamp
        std::string name = words[1];
        double value = std::stod(words[2]);

        gaugeValues[name] = value;
    }


    // Iterate over the map and set the values of the gauges
    for (auto it = gaugeValues.begin(); it != gaugeValues.end(); ++it) {
        auto gauge = getOrCreateGauge(it->first);
        gauge->Set(it->second);
    }
    
    // Close the file
    file.close();
}