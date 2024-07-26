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
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "Utils/flute_types.h"
#include "Packet/EncodingSymbol.h"

namespace LibFlute {
  /**
   *  A class for parsing and creating ALC packets
   */
  class AlcPacket {
    public:
     /**
      *  Create an ALC packet from payload data
      *
      *  @param data Received data to be parsed
      *  @param len Length of the buffer
      */
      AlcPacket(char* data, size_t len);

     /**
      *  Create an ALC packet from encoding symbols 
      *
      *  @param tsi Transport Stream Identifier
      *  @param toi Transport Object Identifier
      *  @param fec_oti OTI values
      *  @param symbols Vector of encoding symbols
      *  @param max_size Maximum payload size
      *  @param fdt_instance_id FDT instance ID (only relevant for FDT with TOI=0)
      */
      AlcPacket(uint16_t tsi, uint16_t toi, FecOti fec_oti, const std::vector<EncodingSymbol>& symbols, size_t max_size, uint32_t fdt_instance_id);

     /**
      *  Default destructor.
      */
      ~AlcPacket();

     /**
      *  Get the TSI
      */
      uint64_t tsi() const { return _tsi; };

     /**
      *  Get the TOI
      */
      uint64_t toi() const {
        if (this) {
          return _toi;
        }

        throw "Invalid packet";
      };

     /**
      *  Get the FEC OTI values
      */
      const FecOti& fec_oti() const { return _fec_oti; };

     /**
      *  Get the LCT header length
      */
      size_t header_length() const  { return _lct_header.lct_header_len * 4; };

     /**
      *  Get the FDT instance ID 
      */
      uint32_t fdt_instance_id() const { return _fdt_instance_id; };

     /**
      *  Get the FEC scheme
      */
      FecScheme fec_scheme() const { return _fec_oti.encoding_id; };

     /**
      *  Get the content encoding
      */
      ContentEncoding content_encoding() const { return _content_encoding; };

     /**
      *  Get a pointer to the payload data of the constructed packet
      */
      char* data() const { return _buffer; };

     /**
      *  Get the payload size
      */
      size_t size() const { return _len; };

      /**
       * Weather or not this packet may be buffered if the TOI is unknown
      */
      bool may_buffer_if_unknown = false;

    private:
      uint64_t _tsi = 0;
      uint64_t _toi = 0;

      uint32_t _fdt_instance_id = 0;

      uint32_t _source_block_number = 0;
      uint32_t _encoding_symbol_id = 0;

      ContentEncoding _content_encoding = ContentEncoding::NONE;
      FecOti _fec_oti = {};

      char* _buffer = nullptr;
      size_t _len;

      // RFC5651 5.1 - LCT Header Format
      struct __attribute__((packed)) lct_header_t {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint8_t res1:1;
        uint8_t source_packet_indicator:1;
        uint8_t congestion_control_flag:2;
        uint8_t version:4;

        uint8_t close_object_flag:1;
        uint8_t close_session_flag:1;
        uint8_t res:2;
        uint8_t half_word_flag:1;
        uint8_t toi_flag:2;
        uint8_t tsi_flag:1;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint8_t version:4;
        uint8_t congestion_control_flag:2;
        uint8_t source_packet_indicator:1;
        uint8_t res1:1;

        uint8_t tsi_flag:1;
        uint8_t toi_flag:2;
        uint8_t half_word_flag:1;
        uint8_t res2:2;
        uint8_t close_session_flag:1;
        uint8_t close_object_flag:1;
#else
#error "Endianness can not be determined"
#endif
        uint8_t lct_header_len; // 8 bits
        uint8_t codepoint; // 8 bits
      } _lct_header;
      static_assert(sizeof(_lct_header) == 4);

      enum HeaderExtension { 
        EXT_NOP  =   0,
        EXT_AUTH =   1,
        EXT_TIME =   2,
        EXT_FTI  =  64,
        EXT_FDT  = 192,
        EXT_CENC = 193
      };

  };
};

