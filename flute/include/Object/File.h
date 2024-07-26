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
#include <map>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <semaphore>
#include "Packet/AlcPacket.h"
#include "Object/FileBase.h"
#include "Object/FileDeliveryTable.h"
#include "Packet/EncodingSymbol.h"

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
  /**
   *  Represents a file being transmitted or received
   */
  class File: public LibFlute::FileBase {
    public:      
     /**
      *  Create a file from an FDT entry (used for reception)
      *
      *  @param entry FDT entry
      */
      File(LibFlute::FileDeliveryTable::FileEntry entry);

     /**
      *  Create a file from the given parameters (used for transmission)
      *
      *  @param toi TOI of the file
      *  @param content_location Content location URI to use
      *  @param content_type MIME type
      *  @param expires Expiry value (in seconds since the NTP epoch)
      *  @param should_be_complete_at Expiry value (in seconds since the NTP epoch)
      *  @param data Pointer to the data buffer
      *  @param length Length of the buffer
      *  @param copy_data Copy the buffer. If false (the default), the caller must ensure the buffer remains valid 
      *                   while the file is being transmitted.
      */
      File(uint32_t toi, 
          FecOti fec_oti,
          std::string content_location,
          std::string content_type,
          uint64_t expires,
          uint64_t should_be_complete_at,
          char* data,
          size_t length,
          bool copy_data = false,
          bool calculate_hash = true);

      ~File();

      /**
      * Free the data buffer
      */
      void free_buffer();

      /**
      *  Write the data from an encoding symbol into the appropriate place in the buffer
      */
      void put_symbol(const EncodingSymbol& symbol);

      /**
      *  Get the data buffer
      */
      char* buffer() const { return _buffer; };

    private:
      void calculate_partitioning();
      void create_blocks();

      void check_file_completion(bool check_hash = true, bool extract_data = true);

      int calculate_md5(char *data, int length, unsigned char *return_sum);

      uint32_t _nof_source_symbols = 0;
      uint32_t _nof_source_blocks = 0;
      uint32_t _nof_large_source_blocks = 0;
      uint32_t _large_source_block_length = 0;
      uint32_t _small_source_block_length = 0;

      char* _buffer = nullptr;
      bool _own_buffer = false;

  };
};
