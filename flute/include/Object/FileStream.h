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
  class FileStream: public LibFlute::FileBase, public std::enable_shared_from_this<FileStream>{
    public:
      typedef std::function<void(uint32_t, std::string)> emit_message_callback_t;
     /**
      *  Create a file from an FDT entry (used for reception)
      *
      *  @param entry FDT entry
      */
      FileStream(LibFlute::FileDeliveryTable::FileEntry entry);

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
      FileStream(uint32_t toi, 
          FecOti fec_oti,
          std::string content_location,
          std::string content_type,
          uint64_t expires,
          uint64_t should_be_complete_at,
          char* data,
          size_t length,
          bool copy_data = false,
          bool calculate_hash = true);

      ~FileStream();

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
      char* buffer() const;


      /**
       * Calculates how much space has not been filled by stream content
       * In other words, how much space is available for new content to be pushed to the stream
       */
      std::size_t available_space();

      /**
       * Pushes content to the stream
      */
      std::size_t push_to_file(std::string content);

      void register_emit_message_callback(emit_message_callback_t callback);

      void set_next_file(std::shared_ptr<FileStream> next_file);

      void set_previous_file(std::shared_ptr<FileStream> previous_file);

    private:

      struct SymbolWrapper {
        bool found;
        uint16_t source_block_id;
        LibFlute::SourceBlock::Symbol symbol;
        std::shared_ptr<LibFlute::FileStream> file;
      };

      void calculate_partitioning();
      void create_blocks();

      void check_file_completion(bool check_hash = true, bool extract_data = true);

      bool create_empty_source_block_buffer(LibFlute::SourceBlock& source_block);

      void try_to_extract_messages(LibFlute::SourceBlock& source_block, LibFlute::SourceBlock::Symbol& current_symbol);

      SymbolWrapper get_previous_symbol_in_stream(std::shared_ptr<LibFlute::FileStream> current_file, uint16_t current_source_block_id, uint16_t current_source_symbol_id);

      SymbolWrapper get_next_symbol_in_stream(std::shared_ptr<LibFlute::FileStream> current_file, uint16_t current_source_block_id, uint16_t current_source_symbol_id);

      uint32_t _nof_source_symbols = 0; // T = Total number of source symbols in the file
      uint32_t _nof_source_blocks = 0; // N = Total number of source blocks in the file
      uint32_t _nof_large_source_blocks = 0; // I = Total number of source blocks that use _large_source_block_length
      uint32_t _large_source_block_length = 0; // A_large = Number of source symbols in a large (normal) source block
      uint32_t _small_source_block_length = 0; // A_small = Number of source symbols in a small source block

      bool _own_buffer = false;

      uint16_t _next_source_block_input = 0;
      uint16_t _next_symbol_input = 0;
      emit_message_callback_t _emit_message_callback = nullptr;

      // Shared pointers to the next and previous files in the chain
      std::shared_ptr<FileStream> _next_file = nullptr;
      std::shared_ptr<FileStream> _previous_file = nullptr;
  };
};
