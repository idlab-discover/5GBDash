//
// async_client.cpp
// ~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2010 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "Utils/FakeNetworkSocket.h"

using boost::asio::ip::tcp;

namespace LibFlute {

class Client: public boost::enable_shared_from_this<Client>
{
public:
  using ContentCallback = std::function<void(char * buffer, size_t bytes_recvd)>;
  using CompletionCallback = std::function<void(size_t , size_t)>;

  Client(boost::asio::io_service& io_service, const std::string& server, const std::string& port,
      const std::string& path, const std::string& post_data, ContentCallback contentCallback, CompletionCallback completionCallback);

  virtual ~Client() {
    resolver_.cancel();
    socket_.close();
    request_.consume(request_.size());
    response_.consume(response_.size());
  };

  void start();

  void set_fake_network_socket(std::shared_ptr<LibFlute::FakeNetworkSocket> fake_network_socket) {
      _fake_network_socket = fake_network_socket;
  }

private:

  void handle_resolve(const boost::system::error_code& err,
      tcp::resolver::iterator endpoint_iterator);

  void handle_connect(const boost::system::error_code& err,
      tcp::resolver::iterator endpoint_iterator);

  void handle_write_request(const boost::system::error_code& err);

  void handle_read_status_line(const boost::system::error_code& err, size_t bytes_recvd);

  void handle_read_headers(const boost::system::error_code& err, size_t bytes_recvd);

  void handle_read_content(const boost::system::error_code& err, size_t bytes_recvd);

  // IO context for sender thread
  boost::asio::io_service& io_service_;
  tcp::resolver resolver_;
  tcp::socket socket_;
  boost::asio::streambuf request_;
  boost::asio::streambuf response_;
  const std::string& post_data_;
  tcp::resolver::query _query;

  std::shared_ptr<LibFlute::FakeNetworkSocket> _fake_network_socket = nullptr;

  ContentCallback _content_callback;
  CompletionCallback _completion_callback;

  std::chrono::time_point<std::chrono::high_resolution_clock, std::chrono::microseconds> _startReceiveTime;
  size_t _bytes_recvd_total;
  size_t _latency_us;
};

} // namespace LibFlute