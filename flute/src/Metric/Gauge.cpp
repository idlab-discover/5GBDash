// Modified version of https://github.com/jupp0r/prometheus-cpp/blob/66e60b47c3bf5a2f81707a6c236214e788c58525/core/src/gauge.cc

#include "Metric/Gauge.h"

#include <fstream>
#include <chrono>
#include <ctime>
#include <iostream>
#include <iomanip>
#include "spdlog/spdlog.h"

#include "public/tracy/Tracy.hpp"

const char* LibFlute::Metric::Gauge::metric_type = "gauge"; // Initialize the static const member

LibFlute::Metric::Gauge::Gauge(const std::string& name, const std::string& documentation):
  _name{name}, _doc{documentation} {
  }

void LibFlute::Metric::Gauge::Increment() { Increment(1.0); }

void LibFlute::Metric::Gauge::Increment(const double value) { Change(value); }

void LibFlute::Metric::Gauge::Decrement() { Decrement(1.0); }

void LibFlute::Metric::Gauge::Decrement(const double value) { Change(-1.0 * value); }

void LibFlute::Metric::Gauge::Set(const double value) {
  std::lock_guard<LockableBase(std::mutex)> lock(_mutex);
  _value = value;
  TracyPlot(_name.c_str(), _value);
  LibFlute::Metric::Gauge::writeToLog();
  }

void LibFlute::Metric::Gauge::Change(const double value) {
  std::lock_guard<LockableBase(std::mutex)> lock(_mutex);
  _value += value;
  TracyPlot(_name.c_str(), _value);
  LibFlute::Metric::Gauge::writeToLog();

}

void LibFlute::Metric::Gauge::SetToCurrentTime() {
  const auto time = std::time(nullptr);
  Set(static_cast<double>(time));
}

double LibFlute::Metric::Gauge::Value() const { return _value; }

void LibFlute::Metric::Gauge::setLogFile(const std::string& filename) {
    _logFilename = filename;
}

void LibFlute::Metric::Gauge::writeToLog() {
    if (_logFilename.empty()) {
      return;
    }
    std::ofstream logfile(_logFilename, std::ios::app);
    if (logfile.is_open()) {
        auto now = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        char timeBuffer[24];
        std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
        logfile << timeBuffer << "," << std::setfill('0') << std::setw(3) << ms.count() << ";" << _name << ";" << LibFlute::Metric::Gauge::Value() << "\n";
        logfile.close();
    } else {
        // File couldn't be opened for append, try creating it
        std::ofstream newFile(_logFilename);
        if (newFile.is_open()) {
            newFile.close();
            LibFlute::Metric::Gauge::writeToLog();
        } else {
            spdlog::error("Error opening or creating log file: {}", _logFilename);
        }
    }
}