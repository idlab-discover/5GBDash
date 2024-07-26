// libflute - FLUTE/ALC library
//
// Copyright (C) 2021 Klaus Kühnhammer (Österreichische Rundfunksender GmbH & Co KG)
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
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <queue>
#include <string>
#include <map>
#include <mutex>
#include "Object/FileBase.h"
#include "Object/File.h"
#include "Object/FileStream.h"
#include "Packet/AlcPacket.h"
#include "Object/FileDeliveryTable.h"
#include "Utils/flute_types.h"
#include "Utils/FakeNetworkSocket.h"

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
  /**
   *  FLUTE transmitter class. Construct an instance of this to send data through a FLUTE/ALC session.
   */
  class Transmitter {
    public:
     /**
      *  Definition of a file transmission completion callback function that can be
      *  registered through ::register_completion_callback.
      *
      *  @param toi TOI of the file that has completed transmission
      */
      typedef std::function<void(uint32_t)> completion_callback_t;

     /**
      *  Default constructor.
      *
      *  @param address Target multicast address
      *  @param port Target port 
      *  @param tsi TSI value for the session 
      *  @param mtu Path MTU to size FLUTE packets for 
      *  @param rate_limit Transmit rate limit (in kbps)
      *  @param io_service Boost io_service to run the socket operations in (must be provided by the caller)
      */
      Transmitter( const std::string& address, 
          short port, uint64_t tsi, unsigned short mtu,
          uint32_t rate_limit,
          FecScheme fec_scheme,
          boost::asio::io_service& io_service):
          Transmitter(address, port, tsi, mtu, rate_limit, fec_scheme, io_service, 1, 1) { };

      Transmitter( const std::string& address,
                   short port, uint64_t tsi, unsigned short mtu,
                   uint32_t rate_limit,
                   FecScheme fec_scheme,
                   boost::asio::io_service& io_service, uint16_t toi, uint32_t instance_id);

     /**
      *  Default destructor.
      */
      virtual ~Transmitter();

     /**
      *  Enable IPSEC ESP encryption of FLUTE payloads.
      *
      *  @param spi Security Parameter Index value to use
      *  @param key AES key as a hex string (without leading 0x). Must be an even number of characters long.
      */
      void enable_ipsec( uint32_t spi, const std::string& aes_key);

     /**
      *  Transmit a file. 
      *  The caller must ensure the data buffer passed here remains valid until the completion callback 
      *  for this file is called.
      *
      *  @param content_location URI to set in the content location field of the generated FDT entry
      *  @param content_type MIME type to set in the content type field of the generated FDT entry
      *  @param expires Expiry timestamp (based on NTP epoch)
      *  @param data Pointer to the data buffer (managed by caller)
      *  @param length Length of the data buffer (in bytes)
      *
      *  @return TOI of the file
      */
      uint16_t send(
        const std::string& content_location,
        const std::string& content_type,
        uint32_t expires,
        uint64_t deadline,
        char* data,
        size_t length);

        
      uint16_t create_empty_file_for_stream(
        uint32_t stream_id, 
        const std::string& content_type,
        uint32_t expires,
        uint64_t deadline,
        uint32_t max_source_block_length,
        uint32_t file_length);

      uint32_t current_instance_id() {
          return _fdt->instance_id();
      }

     /**
      *  Convenience function to get the current timestamp for expiry calculation
      *
      *  @return seconds since the NTP epoch
      */
      uint64_t seconds_since_epoch();

     /**
      *  Register a callback for file transmission completion notifications
      *
      *  @param cb Function to call on file completion
      */
      void register_completion_callback(completion_callback_t cb) { _completion_cb = cb; };

      /**
       * Set whether or not to stop the transmitter when all files have been transmitted.
       * @param stop_when_done
       */
      void set_stop_when_done(bool stop_when_done) { _stop_when_done = stop_when_done; }

      /**
       * Set the rate limit for the transmitter.
       * @param rate_limit
       */
      void set_rate_limit(uint32_t rate_limit) { _rate_limit = rate_limit; }

      void clear_files();

      std::shared_ptr<FileBase> get_file(uint32_t toi);

      void set_remove_after_transmission(bool remove_after_transmission) {
          _remove_after_transmission = remove_after_transmission;
      }

      std::vector<uint16_t> remove_expired_files();

      void set_fake_network_socket(std::shared_ptr<LibFlute::FakeNetworkSocket> fake_network_socket) {
          _fake_network_socket = fake_network_socket;
      }

      std::string fdt_string();

    private:
      void send_fdt(bool should_lock);
      void send_next_packet();
      void fdt_send_tick();

      void file_transmitted(uint32_t toi, bool should_lock);

      std::shared_ptr<LibFlute::FakeNetworkSocket> _fake_network_socket = nullptr;

      boost::asio::ip::udp::socket _socket;
      boost::asio::ip::udp::endpoint _endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string("239.0.0.1"), 16000);
      boost::asio::io_service& _io_service;
      boost::asio::deadline_timer _send_timer;
      boost::asio::deadline_timer _fdt_timer;
      uint64_t _last_fdt_sent = 0;
      bool _remove_after_transmission = true;

      uint64_t _tsi;
      uint16_t _mtu;

      std::unique_ptr<LibFlute::FileDeliveryTable> _fdt;
      std::map<uint32_t, std::shared_ptr<LibFlute::FileBase>> _files;
      TracyLockable(std::mutex, _files_mutex);

      unsigned _fdt_repeat_interval = 1; // Seconds
      uint16_t _toi = 1;

      uint32_t _max_payload;
      FecOti _fec_oti;

      completion_callback_t _completion_cb = nullptr;
      std::string _mcast_address;

      uint32_t _rate_limit = 0;

      bool _stop_when_done = false;
  };
};
