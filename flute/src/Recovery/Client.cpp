//
// async_client.cpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2010 Christopher M. Kohlhoff (chris at kohlhoff dot com)
// Copyright (C) 2023 Casper Haems (IDLab, Ghent University, in collaboration with imec)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>

#include <Recovery/Client.h>

#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"

using boost::asio::ip::tcp;

LibFlute::Client::Client(boost::asio::io_service& io_service,
      const std::string& host, const std::string& port, const std::string& path, const std::string& post_data, LibFlute::Client::ContentCallback contentCallback, LibFlute::Client::CompletionCallback completionCallback)
    : io_service_(io_service),
      resolver_(io_service),
      socket_(io_service),
      post_data_(post_data),
      _content_callback(contentCallback),
      _completion_callback(completionCallback),
      _query(host, port, boost::asio::ip::resolver_query_base::numeric_service)
  {
    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    std::ostream request_stream(&request_);
    if (post_data_.empty()) {
        request_stream << "GET " << path << " HTTP/1.0\r\n";
    } else {
        request_stream << "POST " << path << " HTTP/1.0\r\n";
    }
    request_stream << "Host: " << host << ":" << port << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n";
    if (!post_data_.empty()) {
      request_stream << "Content-Length: " << post_data_.size() << "\r\n";
    }
    request_stream << "\r\n";
    if (!post_data_.empty()) {
      request_stream << post_data_;
    }
  }

auto LibFlute::Client::start() -> void {
    _startReceiveTime = std::chrono::time_point_cast<std::chrono::microseconds>  (std::chrono::high_resolution_clock::now());
    _bytes_recvd_total = 0;

    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.


    if (_fake_network_socket) {
      // Create a copy of the post_data
      std::string post_data_copy = post_data_;

      io_service_.post([&, post_data_copy]() {
        try {
          // Retreive the data from the fake network socket
          auto result = _fake_network_socket->retrieve(post_data_copy);
          if (result.length() == 0) {
            spdlog::warn("[FETCHER] Client: No data retrieved from fake network socket");
            _completion_callback(-1, -1);
            return;
          }

          // Set the latency
          auto endTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
          auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - _startReceiveTime);
          _latency_us = elapsedTime.count();
          spdlog::trace("[FETCHER] Retrieved {} bytes from fake network socket", result.size());

          io_service_.post([&, result]() {
            const char* delimiter = "\r\n\r\n";
            size_t pos = 0;
            size_t start = 0;

            while ((pos = result.find(delimiter, start)) != std::string::npos) {
                size_t length = pos - start;
                char* buffer = new char[length + 1];
                std::memcpy(buffer, result.c_str() + start, length);
                buffer[length] = '\0';

                _content_callback(buffer, length + 4);

                delete[] buffer;

                start = pos + strlen(delimiter);
            }

            if (start < result.size()) {
                size_t length = result.size() - start;
                char* buffer = new char[length + 1];
                std::memcpy(buffer, result.c_str() + start, length);
                buffer[length] = '\0';

                _content_callback(buffer, length + 4);

                delete[] buffer;
            }
            _completion_callback(result.size(), _latency_us);
          });

        } catch (const std::exception &ex) {
          spdlog::error("[FETCHER] Unhandled exception: {}", ex.what());
          _completion_callback(-1, -1);
        } catch (...) {
          spdlog::error("[FETCHER] Unhandled exception");
          _completion_callback(-1, -1);
        }
      });

      return;
    }
    
    resolver_.async_resolve(_query,
        boost::bind(&LibFlute::Client::handle_resolve, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::iterator));
}

auto LibFlute::Client::handle_resolve(const boost::system::error_code& err,
      tcp::resolver::iterator endpoint_iterator) -> void
  {
    if (!err)
    {
      // Attempt a connection to the first endpoint in the list. Each endpoint
      // will be tried until we successfully establish a connection.
      tcp::endpoint endpoint = *endpoint_iterator;
      socket_.async_connect(endpoint,
          boost::bind(&LibFlute::Client::handle_connect, shared_from_this(),
            boost::asio::placeholders::error, ++endpoint_iterator));
    }
    else
    {
      spdlog::warn("[FETCHER] Failed to handle resolve while fetching: {}", err.message());
      _completion_callback(-1, -1);
    }
  }

auto LibFlute::Client::handle_connect(const boost::system::error_code& err,
      tcp::resolver::iterator endpoint_iterator) -> void
  {
    if (!err)
    {
      // The connection was successful. Send the request.
      boost::asio::async_write(socket_, request_,
          boost::bind(&LibFlute::Client::handle_write_request, shared_from_this(),
            boost::asio::placeholders::error));
    }
    else if (endpoint_iterator != tcp::resolver::iterator())
    {
      // The connection failed. Try the next endpoint in the list.
      socket_.close();
      tcp::endpoint endpoint = *endpoint_iterator;
      socket_.async_connect(endpoint,
          boost::bind(&LibFlute::Client::handle_connect, shared_from_this(),
            boost::asio::placeholders::error, ++endpoint_iterator));
    }
    else
    {
      spdlog::warn("[FETCHER] Failed to handle connect while fetching: {}", err.message());
      _completion_callback(-1, -1);
    }
  }

auto LibFlute::Client::handle_write_request(const boost::system::error_code& err) -> void
  {
    if (!err)
    {
      // Read the response status line.
      boost::asio::async_read_until(socket_, response_, "\r\n",
          boost::bind(&LibFlute::Client::handle_read_status_line, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      spdlog::warn("[FETCHER] Failed to handle write request while fetching: {}", err.message());
      _completion_callback(-1, -1);
    }
  }

auto LibFlute::Client::handle_read_status_line(const boost::system::error_code& err, size_t bytes_recvd) -> void
  {
    _bytes_recvd_total += bytes_recvd;
    auto endTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
    auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - _startReceiveTime);
    _latency_us = elapsedTime.count();

    if (!err)
    {
      // Check that response is OK.
      std::istream response_stream(&response_);
      std::string http_version;
      response_stream >> http_version;
      unsigned int status_code;
      response_stream >> status_code;
      std::string status_message;
      std::getline(response_stream, status_message);
      if (!response_stream || http_version.substr(0, 5) != "HTTP/")
      {
        spdlog::debug("[FETCHER] Invalid response");
        _completion_callback(_bytes_recvd_total, _latency_us);
        return;
      }
      if (status_code != 200)
      {
        spdlog::debug("[FETCHER] Response returned with status code {}", status_code);
        _completion_callback(_bytes_recvd_total, _latency_us);
        return;
      }

      LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
      auto fetcher_latency = metricsInstance.getOrCreateGauge("fetcher_latency");
      fetcher_latency->Set(_latency_us);

      // Read the response headers, which are terminated by a blank line.
      boost::asio::async_read_until(socket_, response_, "\r\n\r\n",
          boost::bind(&LibFlute::Client::handle_read_headers, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      spdlog::warn("[FETCHER] Failed to read status line while fetching: {}", err.message());
      _completion_callback(_bytes_recvd_total, _latency_us);
    }
  }

auto LibFlute::Client::handle_read_headers(const boost::system::error_code& err, size_t bytes_recvd) -> void
  {
    if (!err)
    {
      _bytes_recvd_total += bytes_recvd;
      // Process the response headers.
      std::istream response_stream(&response_);
      std::string header;
      while (std::getline(response_stream, header) && header != "\r") {
        // Intentionally empty loop, we ignore the headers
      }

      // Start reading remaining data until EOF.
      boost::asio::async_read_until(socket_, response_,
        "\r\n\r\n",
        boost::bind(&LibFlute::Client::handle_read_content, shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      spdlog::warn("[FETCHER] Failed to read headers while fetching: {}", err.message());
      _completion_callback(_bytes_recvd_total, _latency_us);
    }
  }

auto LibFlute::Client::handle_read_content(const boost::system::error_code& err, size_t bytes_recvd) -> void
  {
    if (!err)
    {
      _bytes_recvd_total += bytes_recvd;
      try {
        // Write all of the data that has been read so far.
        char buffer[bytes_recvd];
        boost::asio::buffer_copy(boost::asio::buffer(buffer, bytes_recvd), response_.data(), bytes_recvd);
        response_.consume(bytes_recvd);

        _content_callback(buffer, bytes_recvd);

        // Continue reading remaining data until EOF.
        boost::asio::async_read_until(socket_, response_,
          "\r\n\r\n",
          boost::bind(&LibFlute::Client::handle_read_content, shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
      } catch (const std::exception &ex) {
        std::cout << "Caught exception \"" << ex.what() << "\"\n";
        spdlog::error("[FETCHER] Unhandled exception: {}", ex.what());
        _completion_callback(_bytes_recvd_total, _latency_us);
      } 
    }
    else if (err == boost::asio::error::eof)
    {
      _completion_callback(_bytes_recvd_total, _latency_us);
    } else {
      spdlog::warn("[FETCHER] Failed to read content while fetching: {}", err.message());
      _completion_callback(_bytes_recvd_total, _latency_us);
    }
  }