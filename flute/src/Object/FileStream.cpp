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
#include "Object/FileStream.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "openssl/md5.h"
#include "openssl/evp.h"
#include "Utils/base64.h"
#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"

#include "public/tracy/Tracy.hpp"

LibFlute::FileStream::FileStream(LibFlute::FileDeliveryTable::FileEntry entry)
  : LibFlute::FileBase::FileBase(std::move(entry))
  , _own_buffer{true}
{
  ZoneScopedN("FileStream::FileStream");
  spdlog::debug("[{}] Creating file (TOI {}, FEC {}, length {}) from file entry", _purpose, _meta.toi, _meta.fec_oti.encoding_id, _meta.content_length);

  if (_meta.fec_transformer != 0) {
    throw "FEC transformer not supported yet";
  }

  calculate_partitioning();
  create_blocks();

  spdlog::debug("[{}] Created file with {} source blocks and {} symbols per block", _purpose, _source_blocks.size(), _source_blocks.begin()->second.symbols.size());
}

LibFlute::FileStream::FileStream(uint32_t toi,
    FecOti fec_oti,
    std::string content_location,
    std::string content_type,
    uint64_t expires,
    uint64_t should_be_complete_at,
    char* data,
    size_t length,
    bool copy_data,
    bool calculate_hash)
  : LibFlute::FileBase::FileBase(toi, fec_oti, content_location, content_type, expires, should_be_complete_at, data, length, copy_data, false)
  , _own_buffer{copy_data || data == nullptr}
{
  ZoneScopedN("FileStream::FileStream");

  if (toi == 0) {
    throw "TOI must not be 0, use the normal File class for TOI 0";
  }

  if (_meta.fec_transformer != 0) {
    throw "FEC transformer not supported yet";
  }


  calculate_partitioning();
  // This creates the source blocks and symbols without any buffer
  create_blocks();

  if (!data) {
    // spdlog::debug("[{}] Creating an empty file stream", _purpose);
    return;
  }

  // Iterate over all source blocks
  auto buffer_ptr = data;
  auto total_offset = 0;
  for (auto& source_block : _source_blocks)
  {
    if (source_block.second.symbols.size() == 0) {
      throw "Block has no symbols";
    }

    if (copy_data) {
      // Allocate the buffer for this block
      buffer_ptr = (char*)malloc(source_block.second.length);
      if (buffer_ptr == nullptr) {
        throw "Failed to allocate memory for source block";
      }
      memcpy(buffer_ptr, data + total_offset, source_block.second.length);
    }
    // Iterate over each symbol in the block and place the pointer to the buffer
    for (auto& symbol : source_block.second.symbols)
    {
      // Place the pointer to the buffer at the correct position
      symbol.second.data = buffer_ptr;
      // The buffer of this symbol has content now
      symbol.second.has_content = true;
      // Calculate the offset for the next symbol
      buffer_ptr += symbol.second.length;
      // Calculate the offset for the next symbol, used for copying the buffer
      total_offset += symbol.second.length;
    }
  }

}

LibFlute::FileStream::~FileStream()
{
  ZoneScopedN("FileStream::~FileStream");
  spdlog::debug("[{}] Destructing file for TOI {}", _purpose, _meta.toi);
  free_buffer();
}

auto LibFlute::FileStream::buffer() const -> char*
{
  ZoneScopedN("FileStream::buffer");
  if (_own_buffer) {
    throw "This object is a stream with multiple buffers.";
  } else {
    if (_source_blocks.size() == 0) {
      throw "No source blocks available";
    } else if (_source_blocks.begin()->second.symbols.size() == 0) {
      throw "No symbols available";
    } else if (_source_blocks.begin()->second.symbols.begin()->second.data == nullptr) {
      throw "No buffer available";
    } else if (_source_blocks.begin()->second.symbols.begin()->second.has_content == false) {
      throw "No content available";
    }
    // Go to the first source block and return the buffer of the first symbol
    return _source_blocks.begin()->second.symbols.begin()->second.data;
  }
}

auto LibFlute::FileStream::free_buffer() -> void
{
  ZoneScopedN("FileStream::free_buffer");
  if (_own_buffer)
  {
    // Iterate over all source blocks
    for (auto& block : _source_blocks)
    {
      // Check if the first symbol has a buffer
      if (block.second.symbols.size() > 0 && block.second.symbols.begin()->second.data != nullptr)
      {
        // Free the buffer for this block
        free(block.second.symbols.begin()->second.data);
        // Overwrite the pointer with nullptr
        block.second.symbols.begin()->second.data = nullptr;
        // We removed the buffer, so the content is not available anymore
        block.second.symbols.begin()->second.has_content = false;
      }
    }
    _own_buffer = false;
  } else {
    // spdlog::debug("[{}] No buffer to free for TOI {}", _purpose, _meta.toi);
  }
}

auto LibFlute::FileStream::put_symbol( const LibFlute::EncodingSymbol& symbol ) -> void
{
  ZoneScopedN("FileStream::put_symbol");
  if(_complete) {
    spdlog::debug("[{}] Not handling symbol {}, SBN {} since file is already complete", _purpose,symbol.id(),symbol.source_block_number());
    return;
  }

  if (symbol.source_block_number() > _source_blocks.size()) {
    throw "Source Block number too high";
  } 

  LibFlute::SourceBlock& source_block = _source_blocks[ symbol.source_block_number() ];

  if(source_block.complete){
      spdlog::trace("[{}] Ignoring symbol {} since block {} is already complete", _purpose,symbol.id(),symbol.source_block_number());
	  return;
  }

  if (source_block.symbols.size() == 0) {
    throw "Block has no symbols";
  }

  if (symbol.id() > source_block.symbols.size()) {
    throw "Encoding Symbol ID too high";
  }

  const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);

  create_empty_source_block_buffer(source_block);

  auto startTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());

  LibFlute::SourceBlock::Symbol& target_symbol = source_block.symbols[symbol.id()];

  if (!target_symbol.complete) {
    if (target_symbol.length != symbol.len()) {
      spdlog::info("[{}] Symbol length mismatch for TOI {}, target length {}, symbol length {}", _purpose, _meta.toi, target_symbol.length, symbol.len());
    }

    if (target_symbol.length == 0) {
      spdlog::info("[{}] Symbol length is 0 for TOI {}, SBN {}, ESID {}, received length {}", _purpose, _meta.toi, symbol.source_block_number(), symbol.id(), symbol.len());
      return;
    }

    symbol.decode_to(target_symbol.data, target_symbol.length);
    target_symbol.complete = true;
    target_symbol.has_content = true; // The buffer of this symbol has content now
    if (_meta.fec_transformer) {
      auto error_occured = false;
      _process_symbol_semaphore.acquire();
      try
      {
      _meta.fec_transformer->process_symbol(source_block,target_symbol,symbol.id());
      }
      catch(...)
      {
        error_occured = true;
        _process_symbol_semaphore.release();
        target_symbol.complete = false;
        // Clear the target_symbol data
        memset(target_symbol.data, 0, target_symbol.length);
        throw "Exception while processing the symbol with the FEC transformer";
      }
      if (!error_occured) {
        _process_symbol_semaphore.release();
      }
    }

    if (target_symbol.complete) {
      check_source_block_completion(source_block);
      check_file_completion();

      // Print the content of this symbol as a chars, replace non alphanumeric characters with dots
      std::string content(target_symbol.data, target_symbol.length);
      for (char& c : content) {
        switch (c) {
          case '\0':
            c = '.';
            break;
          case '\r':
            c = '<';
            break;
          case '\n':
            c = '/';
            break;
          default:
            if (!std::isalnum(c)) {
                c = '?';
            }
            break;
        }
      }
      spdlog::debug("[{}] Received symbol {} for TOI {}, SBN {}, ESID {}, content: {}", _purpose, symbol.id(), _meta.toi, symbol.source_block_number(), symbol.id(), content);

      if (meta().stream_id > 0) {
        try_to_extract_messages(source_block, target_symbol);
      }
    }
  }

  auto endTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
  auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
  double elapsedTimeMilliseconds = static_cast<double>(elapsedTime) / 1000.0; // us to ms

  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
  auto symbol_processing_time = metricsInstance.getOrCreateGauge("symbol_processing_time");
  symbol_processing_time->Set(elapsedTimeMilliseconds);

}

auto LibFlute::FileStream::check_file_completion(bool check_hash, bool extract_data) -> void
{
  // NOTE: content lock should be locked in the parent function.
  ZoneScopedN("FileStream::check_file_completion");
  _complete = std::all_of(_source_blocks.begin(), _source_blocks.end(), [](const auto& block){ return block.second.complete; });
}

auto LibFlute::FileStream::calculate_partitioning() -> void
{
  ZoneScopedN("FileStream::calculate_partitioning");
  // Calculate source block partitioning (RFC5052 9.1) 
  _nof_source_symbols = ceil((double)_meta.fec_oti.transfer_length / (double)_meta.fec_oti.encoding_symbol_length);
  _nof_source_blocks = ceil((double)_nof_source_symbols / (double)_meta.fec_oti.max_source_block_length);
  _large_source_block_length = ceil((double)_nof_source_symbols / (double)_nof_source_blocks);
  _small_source_block_length = floor((double)_nof_source_symbols / (double)_nof_source_blocks);
  _nof_large_source_blocks = _nof_source_symbols - _small_source_block_length * _nof_source_blocks;
}

auto LibFlute::FileStream::create_blocks() -> void
{
  ZoneScopedN("File::create_blocks");
  // Create the required source blocks and encoding symbols

  // Check if the deadline has passed
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()
                  ).count();
  if (_meta.should_be_complete_at > 0 && _meta.should_be_complete_at < now) {
    spdlog::info("[{}] Deadline for file has passed with TOI {}, we won't create the file", _purpose, _meta.toi);
    throw std::runtime_error("Deadline for file has passed");
    return;
  } else if (_meta.expires > 0 && _meta.expires * 1000 < now) { // expires is in seconds, convert to milliseconds
    spdlog::info("[{}] File has expired with TOI {}, we won't create the file", _purpose, _meta.toi);
    throw std::runtime_error("File has expired");
    return;
  }

  if (_meta.fec_transformer){
    spdlog::error("[{}] FEC has not been implemented yet (TOI {})", _purpose, _meta.toi);
    throw std::runtime_error("FEC has not been implemented yet");
  }

  size_t remaining_size = _meta.fec_oti.transfer_length;
  uint16_t block_id = 0;
  while (remaining_size > 0) {
    auto block_length = ( block_id < _nof_large_source_blocks ) ? _large_source_block_length : _small_source_block_length;
    LibFlute::SourceBlock block{
      .id = block_id,
      .complete = false,
      .length = 0,
      .symbols = {}};

    auto total_buffer_size = 0;
    for (uint16_t symbol_id = 0; symbol_id < block_length; symbol_id++) { // For each symbol in the block
      auto symbol_length = std::min(remaining_size, (size_t)_meta.fec_oti.encoding_symbol_length);

      LibFlute::SourceBlock::Symbol symbol{
        .id = symbol_id,
        .data = nullptr, // We delay the allocation of the buffer until we receive the first symbol
        .length = symbol_length,
        .has_content = false, // We have no data so there should be no content
        .complete = false};

      block.symbols[ symbol_id ] = symbol;
      
      remaining_size -= symbol_length;
      total_buffer_size += symbol_length;
      
      if (remaining_size <= 0) break;
    }
    block.length = total_buffer_size;
    _source_blocks[block_id++] = block;
  }
}

auto LibFlute::FileStream::create_empty_source_block_buffer(LibFlute::SourceBlock& source_block) -> bool
{
  // NOTE: content lock should be locked in the parent function.
  ZoneScopedN("FileStream::create_empty_source_block_buffer");
  if (source_block.symbols.size() == 0) {
    throw "Block has no symbols";
  }

  // Check if the first symbol has a buffer
  if (source_block.symbols.begin()->second.data != nullptr)
  {
    return false;
  }

  // Allocate the buffer for this block
  source_block.symbols.begin()->second.data = (char*)malloc(source_block.length);
  if (source_block.symbols.begin()->second.data == nullptr) {
    throw "Failed to allocate memory for symbol";
  }
  // Iterate over each symbol in the block and place the pointer to the buffer
  auto offset = 0;
  for (auto& symbol : source_block.symbols)
  {
    if (symbol.first == 0) {
      offset += symbol.second.length;
      // We have already allocated the buffer for the first symbol
      continue;
    }

    // Prevent memory corruption
    if (offset >= source_block.length) {
      // This happens when symbol count * symbol length > block length --> Should not happen
      throw "Offset exceeds block length";
    }
    // Place the pointer to the buffer at the correct position
    symbol.second.data = source_block.symbols.begin()->second.data + offset;
    symbol.second.has_content = false; // The buffer is empty, so the symbol has no content
    // Calculate the offset for the next symbol
    offset += symbol.second.length;
  }

  return true;
}

auto LibFlute::FileStream::available_space() -> std::size_t
{
  // Note: We don't care about the buffer in this function, we only care about the length
  ZoneScopedN("FileStream::available_space");
  const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);

  // Check if the _next_source_block_input is valid
  if (_next_source_block_input >= _source_blocks.size()) {
    // We have already pushed data to all the source blocks
    return 0;
  }

  std::size_t available_space = 0;
  // Iterate over all source blocks
  for (auto it = _source_blocks.lower_bound(_next_source_block_input); it != _source_blocks.end(); ++it) {
    auto& sourceBlock = it->second;
    // If the first symbol has no content, use the entire block length as available space
    if (!sourceBlock.symbols.empty() && !sourceBlock.symbols.begin()->second.has_content) {
      available_space += sourceBlock.length;
    } else {
      // Iterate over each symbol and sum up the length if the symbol has no content
      for (auto& symbol : sourceBlock.symbols)
      {
        if (!symbol.second.has_content) {
          available_space += symbol.second.length;
        }
      }
    }
  }

  return available_space;
}

auto LibFlute::FileStream::push_to_file(std::string content) -> std::size_t {
  ZoneScopedN("FileStream::push_to_file");
  std::size_t contentAdded = 0;

  if (content.size() == 0) {
    return 0;
  }

  const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);

  // Check if the _next_source_block_input is valid
  if (_next_source_block_input >= _source_blocks.size()) {
    // We have already pushed data to all the source blocks
    // spdlog::debug("[{}] No more space available for TOI {}", _purpose, _meta.toi);
    return 0;
  }

  // Iterate through source blocks starting from _next_source_block_input
  for (auto it = _source_blocks.lower_bound(_next_source_block_input); it != _source_blocks.end(); ++it) {
    auto& sourceBlock = it->second;
    auto& symbols = sourceBlock.symbols;

    create_empty_source_block_buffer(sourceBlock);

    // Iterate through the symbols starting from _next_symbol_input
    for (auto it_symbol = symbols.lower_bound(_next_symbol_input); it_symbol != symbols.end(); ++it_symbol) {
      auto& symbol = it_symbol->second;

      // Calculate how much content to copy for this symbol
      std::size_t sliceLength = std::min(content.size() - contentAdded, symbol.length);

      // Copy content slice to symbol data buffer
      std::copy_n(content.begin() + contentAdded, sliceLength, symbol.data);
      symbol.has_content = true; // The buffer of this symbol has content now

      // Update state variables
      contentAdded += sliceLength;
      _next_symbol_input = symbol.id + 1; // Move to the next symbol

      // If contentAdded exceeds content size or we've reached end of source block, exit loop
      if (contentAdded >= content.size() || _next_symbol_input >= symbols.rbegin()->first) {
        // If we are at the end of the source block, reset _next_symbol_input
        if (_next_symbol_input >= symbols.rbegin()->first) {
          _next_symbol_input = 0; // Reset symbol input for the next source block
          _next_source_block_input = sourceBlock.id + 1; // Move to the next source block
        } else {
          _next_symbol_input = symbol.id + 1; // Move to the next symbol
        }

        // We have reached the last symbol needed to copy the content
        // Check how much of the symbol length was not filled
        std::size_t remainingSpace = symbol.length - sliceLength;
        // If there is remaining space, we fill that with zeros
        // This is a security measure to ensure that we don't send any garbage data
        // Otherwise we might end up transmitting old (possibly confidential) data that was not overwritten.
        if (remainingSpace > 0) {
          // Fill the remaining space with zeros
          std::fill_n(symbol.data + sliceLength, remainingSpace, 0);
        }

        return contentAdded; // Return how much content was added
      }
    }

    // Move to the next source block if we've finished symbols for this one
    _next_source_block_input = sourceBlock.id + 1;
    _next_symbol_input = 0; // Reset symbol input for the next source block
  }

  return contentAdded;
}

auto LibFlute::FileStream::register_emit_message_callback(emit_message_callback_t cb) -> void {
    _emit_message_callback = cb;
}

auto LibFlute::FileStream::set_next_file(std::shared_ptr<FileStream> next_file) -> void {
    _next_file = next_file;
}

auto LibFlute::FileStream::set_previous_file(std::shared_ptr<FileStream> previous_file) -> void {
    _previous_file = previous_file;
}

auto LibFlute::FileStream::try_to_extract_messages(LibFlute::SourceBlock& current_source_block, LibFlute::SourceBlock::Symbol& current_symbol) -> void {
  ZoneScopedN("FileStream::try_to_extract_messages");

  std::string start_marker("START\r\n");

  // Messages are spread accross multiple symbols
  // The data in the symbols is in this format:
  // START \r\n message 1 length \r\n message 1 data \r\n START \r\n message 3 length \r\n message 2 data \r\n ...

  // We need to keep track of the message length and the message data
  // We can't assume that the current_symbol starts at the start the whole data buffer.

  // If the data is larger than the symbol length, then we now that the message is split accross multiple symbols
  // Thus, if our message does not contain a \r\n, we know that we can't extract the message yet and we need to wait for the next symbol (thus return)
  
  // Check if the current_symbol contains a \r\n
  // If it does not, we can't extract the message yet
  std::string current_str(current_symbol.data, current_symbol.length);
  if (current_str.find(start_marker) == std::string::npos) {
      spdlog::debug("[{}] Symbol {} for TOI {} does not contain a full message", _purpose, current_symbol.id, _meta.toi);
      // This quick check is an optimization to avoid unnecessary processing
      // This does not work with packet loss, so to be removed in the future
      return;
  }

  spdlog::debug("[{}] Extracting messages from symbol {} for TOI {}", _purpose, current_symbol.id, _meta.toi);

  // First we need to check if the symbol starts with START\r\n
  // If it does not, we need to iterate backwards over the previous symbols, sourceblocks and files to find the start of the message
  // If during this search, we come accross a symbol that does not contain data (has_content), then we can stop immediately and wait for that symbol to be filled

  // Check if the current_symbol starts with START\r\n
  LibFlute::SourceBlock::Symbol& start_symbol = current_symbol;
  // Find the first occurence of START\r\n
  auto start_pos = current_str.find(start_marker);
  if (start_pos == std::string::npos|| start_pos != 0) {
    // We need to find the start of the message
    // We need to iterate backwards over the previous symbols, sourceblocks and files to find the start of the message
    // If during this search, we come accross a symbol that does not contain data (has_content), then we can stop immediately and wait for that symbol to be filled
    LibFlute::FileStream::SymbolWrapper previous_symbol_wrapper = get_previous_symbol_in_stream(shared_from_this(), current_source_block.id, current_symbol.id);
    while (previous_symbol_wrapper.found) {
      // Check if the previous symbol contains data
      if (!previous_symbol_wrapper.symbol.has_content) {
        // We can't extract the message yet that started in in a previous symbol
        if (start_pos != std::string::npos) {
          // We found the start of the message inside current_symbol, so we can extract that message
          start_symbol = current_symbol;
          break;
        }
        // We cound't find the start of the message in a previous symbol and no message has started in the current symbol, so we can stop here
        return;
      }

      // Check if the previous symbol contains the start of the message
      std::string previous_str(previous_symbol_wrapper.symbol.data, previous_symbol_wrapper.symbol.length);
      auto previous_start_pos = previous_str.find("START\r\n");
      if (previous_start_pos != std::string::npos) {
        // We found the start of the message
        start_symbol = previous_symbol_wrapper.symbol;
        start_pos = previous_start_pos;
        break;
      }

      // Get the previous symbol
      previous_symbol_wrapper = get_previous_symbol_in_stream(previous_symbol_wrapper.file, previous_symbol_wrapper.source_block_id, previous_symbol_wrapper.symbol.id);
    }
  }

  // Now that we have found the start symbol and the start position, we will iterate over the start symbol and it's next symbols to extract that message
  
  // Get the string of the start symbol, starting from the start position + length of the start marker

  std::string start_str(start_symbol.data + start_pos + start_marker.length(), start_symbol.length - start_pos - start_marker.length());

  // Create a stream buffer to store the next data
  std::stringstream stream_buffer;
  // Read until the next \r\n. Note that it is possible that the message is split accross multiple symbols
  // For example, \r\n could be fully in the next symbol, or \r could be in the current symbol and \n in the next symbol
/*
  // So search for the next \r
  auto next_pos = start_str.find("\r");
  if (next_pos != std::string::npos) {
    // We found the next \r, so we can add everything until the next \r to the stream buffer
    stream_buffer << start_str.substr(0, next_pos);
    // Next step is the search for \n, ideally this is just the next character
    // But if next_pos is the last character in the string, we need to go to the next symbol
    if (next_pos == start_str.length() - 1) {
      // We need to go to the next symbol
      LibFlute::FileStream::SymbolWrapper next_symbol_wrapper = get_next_symbol_in_stream(start_symbol, start_symbol.id, start_symbol.id);
      if (!next_symbol_wrapper.found) {
        // We can't extract the message yet, so we stop here
        return;
      }
      start_symbol = next_symbol_wrapper.symbol;
      start_pos = 0;
      next_pos = 0;
      start_str = std::string(start_symbol.data, start_symbol.length);
    }
  } else {
    // We did not find the next \r, so we add everything to the stream buffer
    stream_buffer << start_str;
    // We will need to iterate over the next symbols to find the \r
    LibFlute::FileStream::SymbolWrapper next_symbol_wrapper = get_next_symbol_in_stream(start_symbol, start_symbol.id, start_symbol.id);
    if (!next_symbol_wrapper.found) {
      // We can't extract the message yet, so we stop here
      return;
    }
    start_symbol = next_symbol_wrapper.symbol;
    start_pos = 0;
    next_pos = 0;
    start_str = std::string(start_symbol.data, start_symbol.length);
  }

  // The next character should be \n
    if (start_str[next_pos + 1] != '\n') {
      // We did not find the \n, so we can't extract the message yet
      return;
    }




  
*/
}

auto LibFlute::FileStream::get_previous_symbol_in_stream(std::shared_ptr<LibFlute::FileStream> current_file, uint16_t current_source_block_id, uint16_t current_source_symbol_id) -> SymbolWrapper {
  ZoneScopedN("FileStream::get_previous_symbol_in_stream");
  if (current_source_symbol_id > 0) {
    // Return the previous symbol in the current source block
    LibFlute::SourceBlock::Symbol& new_symbol = _source_blocks[current_source_block_id].symbols[current_source_symbol_id - 1];

    return {
      .found = true,
      .source_block_id = current_source_block_id,
      .symbol = new_symbol,
      .file = current_file
    };
  } else if (current_source_block_id > 0) {
    // Return the last symbol in the previous source block
    LibFlute::SourceBlock& new_block = current_file->_previous_file->_source_blocks[current_source_block_id - 1];
    LibFlute::SourceBlock::Symbol& new_symbol = new_block.symbols[new_block.symbols.size() - 1];
    return {
      .found = true,
      .source_block_id = new_block.id,
      .symbol = new_symbol,
      .file = current_file
    };
  } else if (current_file->_previous_file) {
    auto new_block_id = current_file->_previous_file->_source_blocks.size() - 1;
    LibFlute::SourceBlock& new_block = current_file->_previous_file->_source_blocks[new_block_id];
    // Return the last symbol in the previous file
    LibFlute::SourceBlock::Symbol& new_symbol = new_block.symbols[new_block.symbols.size() - 1];
    return {
      .found = true,
      .source_block_id = new_block.id,
      .symbol = new_symbol,
      .file = _previous_file
    };
  }

  // We have reached the start of the stream and we can't find the start of the message
  spdlog::debug("[{}] Could not find the previous source symbol in this stream {}", _purpose, _meta.toi);
  return {
    .found = false,
    .source_block_id = 0,
    .file = nullptr
  };

}

auto LibFlute::FileStream::get_next_symbol_in_stream(std::shared_ptr<LibFlute::FileStream> current_file, uint16_t current_source_block_id, uint16_t current_source_symbol_id) -> SymbolWrapper {
  ZoneScopedN("FileStream::get_next_symbol_in_stream");
  if (current_source_symbol_id < current_file->_source_blocks[current_source_block_id].symbols.size() - 1) {
    // Return the next symbol in the current source block
    LibFlute::SourceBlock::Symbol& new_symbol = current_file->_source_blocks[current_source_block_id].symbols[current_source_symbol_id + 1];
    return {
      .found = true,
      .source_block_id = current_source_block_id,
      .symbol = new_symbol,
      .file = current_file
    };
  } else if (current_source_block_id < current_file->_source_blocks.size() - 1) {
    // Return the first symbol in the next source block
    LibFlute::SourceBlock& new_block = current_file->_source_blocks[current_source_block_id + 1];
    LibFlute::SourceBlock::Symbol& new_symbol = new_block.symbols[0];
    return {
      .found = true,
      .source_block_id = new_block.id,
      .symbol = new_symbol,
      .file = current_file
    };
  } else if (current_file->_next_file) {
    // Return the first symbol in the next file
    LibFlute::SourceBlock& new_block = current_file->_next_file->_source_blocks[0];
    LibFlute::SourceBlock::Symbol& new_symbol = new_block.symbols[0];
    return {
      .found = true,
      .source_block_id = new_block.id,
      .symbol = new_symbol,
      .file = current_file->_next_file
    };
  }

  // We have reached the end of the stream and we can't find the end of the message
  spdlog::debug("[{}] Could not find the next source symbol in this stream {}", _purpose, _meta.toi);
  return {
    .found = false,
    .source_block_id = 0,
    .file = nullptr
  };
}