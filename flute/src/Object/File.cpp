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
#include "Object/File.h"
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

LibFlute::File::File(LibFlute::FileDeliveryTable::FileEntry entry)
  : LibFlute::FileBase::FileBase(std::move(entry))
  , _own_buffer{true}
{
  ZoneScopedN("File::File");
  spdlog::debug("[{}] Creating file (TOI {}, FEC {}, length {}) from file entry", _purpose, _meta.toi, _meta.fec_oti.encoding_id, _meta.content_length);
  // Allocate a data buffer
  // spdlog::debug("[{}] Allocating buffer for TOI {}", _purpose, _meta.toi);
  if (_meta.fec_transformer){
    _buffer = (char*) _meta.fec_transformer->allocate_file_buffer(_meta.fec_oti.transfer_length);
  } else {
    _buffer = (char*) malloc(_meta.fec_oti.transfer_length);
    //TracyAlloc(_buffer, _meta.fec_oti.transfer_length);
  }
  if (_buffer == nullptr) 
  {
    throw "Failed to allocate file buffer";
  }
  calculate_partitioning();
  create_blocks();
}

LibFlute::File::File(uint32_t toi,
    FecOti fec_oti,
    std::string content_location,
    std::string content_type,
    uint64_t expires,
    uint64_t should_be_complete_at,
    char* data,
    size_t length,
    bool copy_data,
    bool calculate_hash)
  : LibFlute::FileBase::FileBase(toi, fec_oti, content_location, content_type, expires, should_be_complete_at, data, length, copy_data, calculate_hash)
{
  ZoneScopedN("File::File");
  if (!data) {
    spdlog::error("[{}] File pointer is null", _purpose);
    throw "Invalid file";
  }
  /*
  if (toi != 0) {
    spdlog::debug("[{}] Content location is {}", _purpose, content_location);
  }
  */
  if (copy_data) {
    // spdlog::debug("[{}] Allocating buffer for TOI {}", _purpose, toi);
    _buffer = (char*)malloc(length);
    if (_buffer == nullptr)
    {
      throw "Failed to allocate file buffer";
    }
    memcpy(_buffer, data, length);
    _own_buffer = true;
  } else {
    _buffer = data;
  }


  if (calculate_hash) {
    unsigned char md5[EVP_MAX_MD_SIZE];
    calculate_md5(data, length, &md5[0]);
    _meta.content_md5 = base64_encode(md5, MD5_DIGEST_LENGTH);
  }

  calculate_partitioning();
  create_blocks();
}

LibFlute::File::~File()
{
  ZoneScopedN("File::~File");
  spdlog::debug("[{}] Destructing file for TOI {}", _purpose, _meta.toi);
  free_buffer();
}

auto LibFlute::File::free_buffer() -> void
{
  ZoneScopedN("File::free_buffer");
  if (_own_buffer && _buffer != nullptr)
  {
    // spdlog::debug("[{}] Freeing buffer for TOI {}", _purpose, _meta.toi);
    free(_buffer);
    _buffer = nullptr;
    _own_buffer = false;
  } else {
    // spdlog::debug("[{}] No buffer to free for TOI {}", _purpose, _meta.toi);
  }
}

auto LibFlute::File::put_symbol( const LibFlute::EncodingSymbol& symbol ) -> void
{
  ZoneScopedN("File::put_symbol");
  if(_complete) {
    spdlog::debug("[{}] Not handling symbol {}, SBN {} since file is already complete", _purpose,symbol.id(),symbol.source_block_number());
    return;
  }

  // Check if the buffer is not freed
  if (_buffer == nullptr) {
    // This might happen if the buffer was freed because another file with the same content location was received
    spdlog::error("[{}] Buffer is null", _purpose);
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

  if (symbol.id() > source_block.symbols.size()) {
    throw "Encoding Symbol ID too high";
  }

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

    const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);

    symbol.decode_to(target_symbol.data, target_symbol.length);
    target_symbol.complete = true;
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
    }
  }

  auto endTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
  auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
  double elapsedTimeMilliseconds = static_cast<double>(elapsedTime) / 1000.0; // us to ms

  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
  auto symbol_processing_time = metricsInstance.getOrCreateGauge("symbol_processing_time");
  symbol_processing_time->Set(elapsedTimeMilliseconds);

}

auto LibFlute::File::check_file_completion(bool check_hash, bool extract_data) -> void
{
  // NOTE: content lock should be locked in the parent function.
  ZoneScopedN("File::check_file_completion");
  _complete = std::all_of(_source_blocks.begin(), _source_blocks.end(), [](const auto& block){ return block.second.complete; });

  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();

  if (!_complete) {
    return;
  }

  if(_meta.fec_transformer && extract_data){

    // Measure how long it takes to extract the file using FEC
    auto startTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());

    // Extract the file from the source blocks
    if (!_meta.fec_transformer->extract_file(_source_blocks)) {
      spdlog::error("[{}] Failed to extract file from source blocks", _purpose);
      _complete = false;
      return;
    }

    auto endTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
    auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    double elapsedTimeMilliseconds = static_cast<double>(elapsedTime) / 1000.0; // us to ms

    auto extract_file_time = metricsInstance.getOrCreateGauge("extract_file_time");
    extract_file_time->Set(elapsedTimeMilliseconds);
  }

  if (!check_hash || _meta.content_md5.empty()) {
    return;
  }

  // Measure how long it takes to check the MD5 sum
  auto startTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());

  //check MD5 sum
  unsigned char md5[EVP_MAX_MD_SIZE];
  calculate_md5(buffer(),length(),&md5[0]);

  auto content_md5 = base64_decode(_meta.content_md5);
  if (memcmp(md5, content_md5.c_str(), MD5_DIGEST_LENGTH) != 0) {
    spdlog::error("[{}] MD5 mismatch for TOI {}, discarding", _purpose, _meta.toi);
    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    auto file_hash_mismatches = metricsInstance.getOrCreateGauge("file_hash_mismatches");
    file_hash_mismatches->Increment();

    // MD5 mismatch, try again
    for (auto& block : _source_blocks) {
      for (auto& symbol : block.second.symbols) {
        symbol.second.complete = false;
      }
      block.second.complete = false;
      
      if (_meta.fec_transformer) {
        // Discard the decoder of the block
        _meta.fec_transformer->discard_decoder(block.first);
      }
    }
    _complete = false;

  }

  auto endTime = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now());
  auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
  double elapsedTimeMilliseconds = static_cast<double>(elapsedTime) / 1000.0; // us to ms
  auto check_md5_time = metricsInstance.getOrCreateGauge("check_md5_time");
  check_md5_time->Set(elapsedTimeMilliseconds);
}

auto LibFlute::File::calculate_partitioning() -> void
{
  ZoneScopedN("File::calculate_partitioning");
  // Calculate source block partitioning (RFC5052 9.1) 
  if (_meta.fec_transformer && _meta.fec_transformer->calculate_partitioning()){
    _nof_source_symbols = _meta.fec_transformer->nof_source_symbols;
    _nof_source_blocks = _meta.fec_transformer->nof_source_blocks;
    _large_source_block_length = _meta.fec_transformer->large_source_block_length;
    _small_source_block_length = _meta.fec_transformer->small_source_block_length;
    _nof_large_source_blocks = _meta.fec_transformer->nof_large_source_blocks;
    return;
  }
  // Calculate source block partitioning (RFC5052 9.1) 
  _nof_source_symbols = ceil((double)_meta.fec_oti.transfer_length / (double)_meta.fec_oti.encoding_symbol_length);
  _nof_source_blocks = ceil((double)_nof_source_symbols / (double)_meta.fec_oti.max_source_block_length);
  _large_source_block_length = ceil((double)_nof_source_symbols / (double)_nof_source_blocks);
  _small_source_block_length = floor((double)_nof_source_symbols / (double)_nof_source_blocks);
  _nof_large_source_blocks = _nof_source_symbols - _small_source_block_length * _nof_source_blocks;
}

auto LibFlute::File::create_blocks() -> void
{
  ZoneScopedN("File::create_blocks");
  // Create the required source blocks and encoding symbols

  // Check if the deadline has passed
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()
                  ).count();
  if (_meta.should_be_complete_at > 0 && _meta.should_be_complete_at < now - 20 /* Give our application some slack */) {
    spdlog::info("[{}] Deadline for file has passed with TOI {}, we won't create the file", _purpose, _meta.toi);
    throw std::runtime_error("Deadline for file has passed");
    return;
  } else if (_meta.expires > 0 && _meta.expires * 1000 < now) { // expires is in seconds, convert to milliseconds
    spdlog::info("[{}] File has expired with TOI {}, we won't create the file", _purpose, _meta.toi);
    throw std::runtime_error("File has expired");
    return;
  }

  if (_meta.fec_transformer){
    if (_buffer == nullptr) {
      spdlog::error("[{}] Buffer is null", _purpose);
      throw "Buffer is null";
    }

    _create_blocks_semaphore.acquire();
    int bytes_read = 0;
    try {
      _source_blocks = _meta.fec_transformer->create_blocks(_buffer, &bytes_read);
    } catch (const std::exception& e) {
      _create_blocks_semaphore.release();
      spdlog::error("[{}] Exception in create_blocks: {}", _purpose, e.what());
      throw e;
    } catch (...) {
      _create_blocks_semaphore.release();
      spdlog::error("[{}] Unknown exception in create_blocks", _purpose);
      throw;
    }
    if (_source_blocks.size() <= 0) {

      _create_blocks_semaphore.release();
      spdlog::error("[{}] FEC Transformer failed to create source blocks", _purpose);
      throw "FEC Transformer failed to create source blocks";
    }
    _create_blocks_semaphore.release();
    return;
  }
  auto buffer_ptr = _buffer;
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
      if (buffer_ptr) {
        assert(buffer_ptr + symbol_length <= _buffer + _meta.fec_oti.transfer_length);
      }

      LibFlute::SourceBlock::Symbol symbol{
        .id = symbol_id,
        .data = buffer_ptr,
        .length = symbol_length,
        .complete = false};

      block.symbols[ symbol_id ] = symbol;
      
      remaining_size -= symbol_length;
      total_buffer_size += symbol_length;
      if (buffer_ptr) {
        buffer_ptr += symbol_length;
      }
      
      if (remaining_size <= 0) break;
    }
    block.length = total_buffer_size;
    _source_blocks[block_id++] = block;
  }
}

/**
  *  Calculate the md5 message digest
  *
  *  @param input byte array whose md5 message digest shall be calculated
  *  @param length size of the input array
  *  @param result buffer to store the output of the md5 calculation. Make sure it is EVP_MAX_MD_SIZE bytes large
  * 
  *  @return length of the calculated md5 sum (should be 16 bytes for md5)
  */
auto LibFlute::File::calculate_md5(char *input, int length, unsigned char *result) -> int
{
  ZoneScopedN("File::calculate_md5");
  EVP_MD_CTX*   context = EVP_MD_CTX_new();
  const EVP_MD* md = EVP_md5();
  unsigned int  md_len;

  EVP_DigestInit_ex2(context, md, NULL);
  EVP_DigestUpdate(context, input, length);
  EVP_DigestFinal_ex(context, result, &md_len);
  EVP_MD_CTX_free(context);

  char buf[EVP_MAX_MD_SIZE * 2] = {0};
  for (unsigned int i = 0 ; i < md_len ; ++i){
    sprintf(&buf[i*2],  "%02x", result[i]);
  }
  spdlog::debug("[{}] MD5 Digest is {}", _purpose, buf);

  return md_len;
}