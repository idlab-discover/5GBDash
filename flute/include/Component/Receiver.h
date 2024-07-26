// libflute - FLUTE/ALC library
//
// Copyright (C) 2021 Klaus Kühnhammer (Österreichische Rundfunksender GmbH & Co KG)
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
#include "Packet/AlcPacket.h"
#include "Object/FileBase.h"
#include "Object/File.h"
#include "Object/FileStream.h"
#include "Object/FileDeliveryTable.h"
#include "Recovery/Fetcher.h"
#include "Utils/flute_types.h"
#include "spdlog/spdlog.h"
#include "Utils/IpSec.h"
#include "Metric/Metrics.h"
#include "Utils/FakeNetworkSocket.h"
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/circular_buffer.hpp>

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
  /**
   *  FLUTE receiver class. Construct an instance of this to receive files from a FLUTE/ALC session.
   */
  class Receiver {
    public:
     /**
      *  Definition of a file reception completion callback function that can be
      *  registered through ::register_completion_callback.
      *
      *  @returns shared_ptr to the received file
      */
      typedef std::function<void(std::shared_ptr<LibFlute::FileBase>)> removal_callback_t;
      typedef std::function<void(std::shared_ptr<LibFlute::FileBase>)> completion_callback_t;
      typedef std::function<void(uint32_t, std::string)> emit_message_callback_t;
     /**
      *  Default constructor.
      *
      *  @param iface Address of the (local) interface to bind the receiving socket to. 0.0.0.0 = any.
      *  @param address Multicast address
      *  @param port Target port 
      *  @param tsi TSI value of the session 
      *  @param io_service Boost io_service to run the socket operations in (must be provided by the caller)
      */
      Receiver( const std::string& iface, const std::string& address, const std::string& retreival_url, 
          short port, uint64_t tsi,
          boost::asio::io_service& io_service,
          std::shared_ptr<LibFlute::FakeNetworkSocket> fake_network_socket = nullptr);

     /**
      *  Default destructor.
      */
      virtual ~Receiver();

     /**
      *  Enable IPSEC ESP decryption of FLUTE payloads.
      *
      *  @param spi Security Parameter Index value to use
      *  @param key AES key as a hex string (without leading 0x). Must be an even number of characters long.
      */
      void enable_ipsec( uint32_t spi, const std::string& aes_key);

     /**
      *  List all current files
      *
      *  @return Vector of all files currently in the FDT
      */
      std::vector<std::shared_ptr<LibFlute::FileBase>> file_list();

     /**
      *  Remove files from the list that are older than max_age seconds
      */
      void remove_expired_files(unsigned max_age);

     /**
      *  Remove a file from the list that matches the passed content location
      */
      void remove_file_with_content_location(const std::string& cl);

     /**
      *  Register a callback for file reception notifications
      *
      *  @param cb Function to call on file completion
      */
      void register_completion_callback(completion_callback_t cb) { _completion_cb = cb; };

      /**
       * Register a callback for file removal notifications
       * 
       * @param cb Function to call on file removal
      */
      void register_removal_callback(removal_callback_t cb) { _removal_cb = cb; };

      /**
       * Register a callback for emitting messages
       * 
       * @param cb Function to call on message emission
      */
     void register_emit_message_callback(emit_message_callback_t cb) { _emit_message_cb = cb; };

      void stop() { _running = false; }

      void resolve_fdt_for_buffered_alcs();

      bool handle_alc_buffer();

      void handle_alc_step_four(std::shared_ptr<LibFlute::FileBase> file, std::shared_ptr<LibFlute::AlcPacket> alc_ptr);

      void set_video_ids_ptr(std::shared_ptr<std::vector<std::string>> video_ids_ptr) { _video_ids_ptr = video_ids_ptr; }
    private:

      void handle_receive_from(const boost::system::error_code& error,
          size_t bytes_recvd);
      void handle_alc_step_one(char* data, size_t len, bool buffer_if_unknown);
      void handle_alc_step_two(std::shared_ptr<AlcPacket> alc_ptr, bool buffer_if_unknown);
      void handle_alc_step_three(std::shared_ptr<AlcPacket> alc_ptr);
      void handle_fdt_step_one();
      void handle_fdt_step_two();
      void pop_toi_from_buffer_fronts(uint64_t toi);
      void await_file_spawn_threads();
      void spawn_file(const LibFlute::FileDeliveryTable::FileEntry& entry);
      LibFlute::Fetcher _fetcher;
      boost::asio::ip::udp::socket _socket;
      boost::asio::ip::udp::endpoint _sender_endpoint;

      std::shared_ptr<LibFlute::FakeNetworkSocket> _fake_network_socket = nullptr;

      enum { max_length = 2048 };
      char _data[max_length];
      uint64_t _tsi;
      std::unique_ptr<LibFlute::FileDeliveryTable> _fdt;
      std::map<uint64_t, std::shared_ptr<LibFlute::FileBase>> _files;
      std::map<uint64_t, std::vector<uint64_t>> _stream_tois;
      mutable TracyLockable(std::mutex, _spawn_files_mutex);
      mutable TracyLockable(std::mutex, _files_mutex);
      mutable TracyLockable(std::mutex, _buffer_mutex);
      std::string _mcast_address;

      removal_callback_t _removal_cb = nullptr;
      completion_callback_t _completion_cb = nullptr;
      emit_message_callback_t _emit_message_cb = nullptr;

      boost::circular_buffer_space_optimized<std::shared_ptr<LibFlute::AlcPacket>> _unknown_alc_buffer;
      boost::circular_buffer_space_optimized<std::shared_ptr<LibFlute::AlcPacket>> _alc_buffer;

      bool _running = true;

      std::vector<std::jthread> _file_spawn_threads;
      std::shared_ptr<std::vector<std::string>> _video_ids_ptr;
  };
};
