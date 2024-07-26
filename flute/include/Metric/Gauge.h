// Modified version of https://github.com/jupp0r/prometheus-cpp/blob/66e60b47c3bf5a2f81707a6c236214e788c58525/core/include/prometheus/gauge.h

#pragma once


#include <mutex>
#include <string>

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
namespace Metric {

/// \brief A gauge metric to represent a value that can arbitrarily go up and
/// down.
///
/// The class represents the metric type gauge:
/// https://prometheus.io/docs/concepts/metric_types/#gauge
///
/// Gauges are typically used for measured values like temperatures or current
/// memory usage, but also "counts" that can go up and down, like the number of
/// running processes.
///
/// The class is thread-safe. No concurrent call to any API of this type causes
/// a data race.
class Gauge {
 public:
  static const char* metric_type;

  /// \brief Create a gauge that starts at 0.
  Gauge(const std::string&, const std::string&);

  /// \brief Increment the gauge by 1.
  void Increment();

  /// \brief Increment the gauge by the given amount.
  void Increment(double);

  /// \brief Decrement the gauge by 1.
  void Decrement();

  /// \brief Decrement the gauge by the given amount.
  void Decrement(double);

  /// \brief Set the gauge to the given value.
  void Set(double);

  /// \brief Set the gauge to the current unix time in seconds.
  void SetToCurrentTime();

  /// \brief Get the current value of the gauge.
  double Value() const;

  void setLogFile(const std::string& filename);

 private:
  void Change(double);
  double _value = 0.0;
  void writeToLog();

  std::string _name;
  std::string _doc;
  std::string _logFilename;
  TracyLockable(std::mutex, _mutex); // Mutex to protect the value
};

}  // namespace Metric
}  // namespace LibFlute