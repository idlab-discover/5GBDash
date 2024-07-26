// libflute - FLUTE/ALC library
//
// Copyright (C) 2023 Casper Haems (IDLab, Ghent University, in collaboration with imec)
//
// Licensed under the License terms and conditions for use, reproduction, and
// distribution of 5G-MAG software (the “License”).  You may not use this file
// except in compliance with the License.  You may obtain a copy of the License at
// https://www.5g-mag.com/reference-tools.  Unless required by applicable law or
// agreed to in writing, software distributed under the License is distributed on
// an “AS IS” BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.
// 
// See the License for the specific language governing permissions and limitations
// under the License.
//
#pragma once
#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <iostream>
#include <string>
#include <regex>
#include <map>
#include <thread>
#include <atomic>

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/shared_ptr.hpp>

#include <Recovery/Client.h>
#include "Utils/flute_types.h"

#include "Utils/FakeNetworkSocket.h"

namespace LibFlute {
  /**
   *  FLUTE receiver class. Construct an instance of this to receive files from a FLUTE/ALC session.
   */
  class Fetcher {
    public:
     /**
      *  Definition of a file reception completion callback function that can be
      *  registered through ::register_completion_callback.
      *
      *  @returns to the received file
      */
      typedef std::function<void(const char*, size_t)> callback_t;
 
      /**
       * Construct a new Fetcher instance.
       * 
       * @param url The URL to fetch from
       */
      Fetcher( const std::string& url);

      virtual ~Fetcher();

      void fetch_alcs(const uint32_t toi, LibFlute::FecScheme fec, const std::string &content_location, std::shared_ptr<std::map<uint16_t, std::vector<uint16_t>>> missing_symbols);

      void fetch_fdt();

     /**
      *  Register a callback for file reception notifications
      *
      *  @param cb Function to call on file completion
      */
      void register_alc_callback(callback_t cb) { _alc_cb = cb; };
      void register_fdt_callback(callback_t cb) { _fdt_cb = cb; };


      void set_fake_network_socket(std::shared_ptr<LibFlute::FakeNetworkSocket> fake_network_socket) {
        _fake_network_socket = fake_network_socket;
      }

    private:
      void handle_ALC(char * buffer, size_t bytes_recvd);

      void handle_FDT(char * buffer, size_t bytes_recvd);

      std::string _url;
      boost::asio::io_service _io_service;
      std::jthread _io_service_thread;
      std::atomic<bool> _stop_thread = false;

      std::regex _url_regex;

      callback_t _alc_cb = nullptr;
      callback_t _fdt_cb = nullptr;

      std::shared_ptr<LibFlute::FakeNetworkSocket> _fake_network_socket = nullptr;

      LibFlute::Metric::Metrics& metricsInstance;

      std::vector<boost::shared_ptr<LibFlute::Client>> _activeClients;
  };
};
