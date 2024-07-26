
#include "Object/FileBase.h"

#ifdef RAPTOR_ENABLED
#include "Fec/RaptorFEC.h"
#endif

std::counting_semaphore<LibFlute::FileBase::_max_create_block_threads> LibFlute::FileBase::_create_blocks_semaphore{LibFlute::FileBase::_max_create_block_threads};

std::counting_semaphore<LibFlute::FileBase::_max_process_symbol_threads> LibFlute::FileBase::_process_symbol_semaphore{LibFlute::FileBase::_max_process_symbol_threads};

LibFlute::FileBase::FileBase(LibFlute::FileDeliveryTable::FileEntry entry)
    : _meta( std::move(entry) )
    , _purpose("RECEIVE")
    , _received_at( time(nullptr) )
    , _retrieval_deadline(_meta.should_be_complete_at) {}

LibFlute::FileBase::FileBase(uint32_t toi, 
    FecOti fec_oti,
    std::string content_location,
    std::string content_type,
    uint64_t expires,
    uint64_t should_be_complete_at,
    char* data,
    size_t length,
    bool copy_data,
    bool calculate_hash)
    : _retrieval_deadline(should_be_complete_at)
    , _purpose("TRANSMIT") {
        ZoneScopedN("FileBase::FileBase");

        spdlog::debug("[{}] Creating file (TOI {}, FEC {}, length {}) from data", _purpose, toi, fec_oti.encoding_id, length);

        _meta.toi = toi;
        _meta.stream_id = 0;
        _meta.fec_oti = fec_oti;
        _meta.content_location = std::move(content_location);
        _meta.content_type = std::move(content_type);
        _meta.expires = expires;
        _meta.should_be_complete_at = should_be_complete_at;
        _meta.content_length = length;

#ifdef RAPTOR_ENABLED
        LibFlute::RaptorFEC *r = nullptr;
#endif
        switch (_meta.fec_oti.encoding_id) {
            case FecScheme::CompactNoCode:
                _meta.fec_oti.transfer_length = length;
                _meta.fec_transformer = 0;
                break;
#ifdef RAPTOR_ENABLED
            case FecScheme::Raptor:
                try {
                    _meta.fec_oti.transfer_length = length;
                    r = new RaptorFEC(length, fec_oti.encoding_symbol_length, fec_oti.max_source_block_length);
                    _meta.fec_oti.encoding_symbol_length = r->T;
                    spdlog::debug("[{}] Raptor FEC Scheme 1, T = {}, K = {}, MSBL = {}", _purpose, r->T, r->K, _meta.fec_oti.max_source_block_length);
                    _meta.fec_oti.max_source_block_length = r->K; // The maximum source block length is the number of source symbols times the symbol length, for this file
                    _meta.fec_transformer = r; 
                } catch (...) {
                    // Failed to create RaptorFEC object, fall back to CompactNoCode
                    spdlog::warn("[{}] Failed to create RaptorFEC object, falling back to CompactNoCode (FEC 0)", _purpose);
                    _meta.fec_oti.encoding_id = FecScheme::CompactNoCode;
                    _meta.fec_oti.transfer_length = length;
                    _meta.fec_transformer = 0;
                    _meta.fec_oti.max_source_block_length = 64;
                    _meta.fec_oti.encoding_symbol_length = fec_oti.encoding_symbol_length;
                    if (r != nullptr) {
                        delete r; // Delete the RaptorFEC object if it was created
                    }
                }
                break;
#endif
            default:
                throw "FEC scheme not supported or not yet implemented";
                break;
        }
    }

LibFlute::FileBase::~FileBase(){
    ZoneScopedN("FileBase::~FileBase");
    // spdlog::debug("[{}] Destroying FileBase with TOI {}", _purpose, _meta.toi);
    if (_meta.toi == 0) {
        spdlog::debug("[{}] Instance Id for FDT that is being destroyed is {}", _purpose, _fdt_instance_id);
    }
    stop_receive_thread(false); // Don't join here, because we might destruct it from within the thread itself

    if (_meta.fec_transformer != 0) {
        delete _meta.fec_transformer;
    }
}

auto LibFlute::FileBase::complete() const -> bool {
    return _complete;
}

auto LibFlute::FileBase::length() const -> size_t {
    return _meta.fec_oti.transfer_length;
}

auto LibFlute::FileBase::fec_oti() -> FecOti& {
    return _meta.fec_oti;
}

auto LibFlute::FileBase::meta() -> LibFlute::FileDeliveryTable::FileEntry& {
    return _meta;
}

auto LibFlute::FileBase::received_at() const -> unsigned long {
    return _received_at;
}

auto LibFlute::FileBase::mark_complete() -> void {
    _complete = true;
}

auto LibFlute::FileBase::set_fdt_instance_id( uint16_t id) -> void {
    _fdt_instance_id = id;
}

auto LibFlute::FileBase::fdt_instance_id() -> uint16_t {
    return _fdt_instance_id;
}

auto LibFlute::FileBase::register_missing_callback(missing_callback_t cb) -> void {
    _missing_cb = cb;
}

auto LibFlute::FileBase::register_receiver_callback(receiver_callback_t cb) -> void {
    _receiver_cb = cb;
}

auto LibFlute::FileBase::get_source_blocks() -> std::map<uint16_t, LibFlute::SourceBlock> {
    return _source_blocks;
}

auto LibFlute::FileBase::retrieve_missing_parts() -> void {
    // check if the file is ignored
    if (_ignore_reception) {
        _meta.should_be_complete_at = 0;
        return;
    }
    emit_missing_symbols();
    _meta.should_be_complete_at = 0;
}

auto LibFlute::FileBase::push_alc_to_receive_buffer(const std::shared_ptr<AlcPacket>& alc) -> void {
    ZoneScopedN("FileBase::push_alc_to_receive_buffer");
    ZoneText(_meta.content_location.c_str(), _meta.content_location.size());
    // check if the filebase is ignored
    if (_ignore_reception) {
        // spdlog::debug("[{}] Ignoring reception of filebase {}", _purpose, _meta.content_location);
        return;
    }
    // Check if the thread is running, if not, then return
    if (!_receive_thread.joinable()) {
        return;
    }
    std::lock_guard<LockableBase(std::mutex)> bufferLock(_receive_buffer_mutex);
    _alc_buffer.push_back(alc);
}

auto LibFlute::FileBase::process_receive_buffer() -> void {
    // Set unique lock to allow for concurrent access
    std::unique_lock<LockableBase(std::mutex)> bufferLock(_receive_buffer_mutex);
    try {
        // Check if the buffer is empty
        if (_alc_buffer.empty()) {
            bufferLock.unlock();
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            return;
        }

        ZoneScopedN("FileBase::process_receive_buffer");

        // spdlog::debug("[{}] Processing receive buffer, current size is {}", _purpose, _alc_buffer.size());

        // Get the first element of the buffer and pop it
        auto alc = _alc_buffer.front();
        _alc_buffer.erase(_alc_buffer.begin());
        bufferLock.unlock();

        if (_receiver_cb != nullptr) {
            _receiver_cb(alc);
        }
    } catch (const std::exception& e) {
        spdlog::error("[{}] Exception in process_receive_buffer: {}", _purpose, e.what());
        // Unlock the buffer if it is still locked
        if (bufferLock.owns_lock()) {
            // spdlog::debug("[{}] Unlocking file alc buffer", _purpose);
            bufferLock.unlock();
        }
    } catch (const char* errorMessage) {
        spdlog::error("[{}] Exception in process_receive_buffer: {}", _purpose, errorMessage);
        // Unlock the buffer if it is still locked
        if (bufferLock.owns_lock()) {
            // spdlog::debug("[{}] Unlocking file alc buffer", _purpose);
            bufferLock.unlock();
        }
    } catch (...) {
        spdlog::error("[{}] Unknown exception in process_receive_buffer", _purpose);
        // Unlock the buffer if it is still locked
        if (bufferLock.owns_lock()) {
            // spdlog::debug("[{}] Unlocking file alc buffer", _purpose);
            bufferLock.unlock();
        }
    }
}

auto LibFlute::FileBase::start_receive_thread() -> void {
    ZoneScopedN("FileBase::start_receive_thread");
    // Start the processing thread
    _receive_thread = std::jthread([&]() {
        LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
        // content_location is used as a unique identifier for the file
        metricsInstance.addThread(std::this_thread::get_id(), "Receive thread for " + _meta.content_location + " (TOI " + std::to_string(_meta.toi) + ")");
        // Set stop flag to false
        _stop_receive_thread = false;

        while (!_stop_receive_thread) {
            process_receive_buffer();
        }

        spdlog::debug("[{}] Stopped receive thread for TOI {}", _purpose, _meta.toi);
    });
}

auto LibFlute::FileBase::stop_receive_thread(bool should_join) -> void  {
    ZoneScopedN("FileBase::stop_receive_thread");
    if (!_stop_receive_thread) {
        spdlog::debug("[{}] Stopping receive thread for TOI {}", _purpose, _meta.toi);
    }
    _stop_receive_thread = true;

    if (should_join && _receive_thread.joinable()) {
        _receive_thread.join();
        // spdlog::debug("[{}] Receive thread joined for TOI {}", _purpose, _meta.toi);
    }
}

auto LibFlute::FileBase::get_buffered_symbols(std::vector<EncodingSymbol>& symbols) -> void {
    ZoneScopedN("FileBase::get_buffered_symbols");
    // Set unique lock to allow for concurrent access
    const std::lock_guard<LockableBase(std::mutex)> bufferLock(_receive_buffer_mutex);

    // Check if the buffer is empty
    if (_alc_buffer.empty()) {
        return;
    }

    spdlog::debug("[{}] Getting buffered symbols, current size is {}", _purpose, _alc_buffer.size());

    // Iterate over the buffer
    for (auto alc : _alc_buffer) {
        // Our filebase has not been completed yet,
        // Get the encoding symbols from the payload
        auto encoding_symbols = LibFlute::EncodingSymbol::from_payload(
            alc->data(), // Payload
            alc->size(), // Size of the payload
            fec_oti(),
            alc->content_encoding());

        for(const auto& symbol : encoding_symbols) {
            symbols.push_back(symbol);
        }
    }
}

auto LibFlute::FileBase::ignore_reception() -> void {
    _ignore_reception = true;
}

auto LibFlute::FileBase::time_after_deadline() -> uint64_t  {
    if ((_retrieval_deadline == 0) || (_retrieval_deadline == UINT64_MAX)) {
        return 0;
    }

    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (now > _retrieval_deadline) {
        return now - _retrieval_deadline;
    } else {
        return 0;
    }
}

auto LibFlute::FileBase::time_before_deadline() -> uint64_t {
    if ((_retrieval_deadline == 0) || (_retrieval_deadline == UINT64_MAX)) {
        return 0;
    }

    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (now < _retrieval_deadline) {
        return _retrieval_deadline - now;
    } else {
        return 0;
    }
}

auto LibFlute::FileBase::buffer() const -> char* {
    throw "Not implemented, should be implemented in derived class";
}

auto LibFlute::FileBase::free_buffer() -> void {
    throw "Not implemented, should be implemented in derived class";
}

auto LibFlute::FileBase::put_symbol(const EncodingSymbol& symbol) -> void {
    throw "Not implemented, should be implemented in derived class";
}

auto LibFlute::FileBase::check_source_block_completion( LibFlute::SourceBlock& block ) -> void
{
  // NOTE: content lock should be locked in the parent function.
  ZoneScopedN("File::check_source_block_completion");
  if (_meta.fec_transformer) {
    block.complete = _meta.fec_transformer->check_source_block_completion(block);
    return;
  }

  block.complete = std::all_of(block.symbols.begin(), block.symbols.end(), [](const auto& symbol){ return symbol.second.complete; });
}

auto LibFlute::FileBase::check_file_completion(bool check_hash, bool extract_data) -> void
{
  // NOTE: content lock should be locked in the parent function.
  throw "Not implemented, should be implemented in derived class";
}


auto LibFlute::FileBase::get_next_symbols(size_t max_size) -> std::vector<EncodingSymbol> 
{
    ZoneScopedN("FileBase::get_next_symbols");
    const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);

    int nof_symbols = std::floor((float)(max_size) / (float)_meta.fec_oti.encoding_symbol_length);
    auto cnt = 0;
    std::vector<EncodingSymbol> symbols;

    for (auto& block : _source_blocks) {
        // Check if we have enough symbols
        if (cnt >= nof_symbols) break;

        // Check if the block is complete (all symbols have been transmitted)
        if (!block.second.complete) {
            // Check if the first symbol has data
            if (block.second.symbols.size() == 0 || block.second.symbols.begin()->second.data == nullptr) {
            spdlog::trace("[{}] Skipping block {} since it has no data (TOI {})", _purpose, block.first, _meta.toi);
            continue;
            }

            // Iterate over all symbols in the block
            for (auto& symbol : block.second.symbols) {
                // Check if we have enough symbols
                if (cnt >= nof_symbols) break;

                // Check if the symbol is not complete and not queued
                if (!symbol.second.complete && !symbol.second.queued) {
                    // Check if the symbol has content
                    if (!symbol.second.has_content) {
                        // This symbol has no content. We assume that the next symbols in the block also have no content (has_content is only false when the file is a stream, which has to be filled in order.)
                        // To stop the parent loop, we set the cnt to nof_symbols
                        cnt = nof_symbols;
                        break;
                    }
                    // Add the symbol to the list
                    symbols.emplace_back(symbol.first, block.first, symbol.second.data, symbol.second.length, _meta.fec_oti.encoding_id);
                    // Mark the symbol as queued
                    symbol.second.queued = true;
                    cnt++;
                }
            }
        }
    }
    // spdlog::trace("[{}] Queueing {} symbols for TOI", _purpose, symbols.size(), _meta.toi);
    return symbols;
}

auto LibFlute::FileBase::mark_completed(const std::vector<EncodingSymbol>& symbols, bool success) -> void
{
    ZoneScopedN("FileBase::mark_completed");
    // spdlog::debug("[{}] Marking {} symbols as completed", _purpose, symbols.size());
    const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);
    for (auto& symbol : symbols) {
    auto block = _source_blocks.find(symbol.source_block_number());
        if (block != _source_blocks.end()) {
            auto sym = block->second.symbols.find(symbol.id());
            if (sym != block->second.symbols.end()) {
                sym->second.queued = false;
                sym->second.complete = success;
            }
            check_source_block_completion(block->second);
            check_file_completion();
        }
    }
}

auto LibFlute::FileBase::get_content_buffer_lock() -> const std::unique_lock<LockableBase(std::mutex)> {
    return std::unique_lock<LockableBase(std::mutex)>(_content_buffer_mutex);
}


auto LibFlute::FileBase::emit_missing_symbols() -> void
{
  ZoneScopedN("FileBase::emit_missing_symbols");
  if (_missing_cb == nullptr) {
    spdlog::debug("[{}] Some symbols are missing", _purpose);
    return;
  }


  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
  auto emit_missing_symbols_gauge = metricsInstance.getOrCreateGauge("emit_missing_symbols");
  emit_missing_symbols_gauge->Increment();

  // Map to store an array of incomplete symbols per block
  std::shared_ptr<std::map<uint16_t, std::vector<uint16_t>>> missing_symbols = std::make_shared<std::map<uint16_t, std::vector<uint16_t>>>();


  const std::lock_guard<LockableBase(std::mutex)> bufferLock(_content_buffer_mutex);

  uint64_t total_symbol_count = 0;
  uint64_t count = 0;
  // Search for all the missing symbols.
  for (auto block_it = _source_blocks.begin(); block_it != _source_blocks.end(); ++block_it) {
    auto current_block = block_it->second;
    total_symbol_count += current_block.symbols.size(); // Keep track of how many symbols there are in total for this file
    if (!current_block.complete) { // Is the block incomplete?
      std::vector<uint16_t> missing_symbols_of_block; // Array to store incomplete symbols
      for (auto symbol_it = current_block.symbols.begin(); symbol_it != current_block.symbols.end(); ++symbol_it) {
        if (!symbol_it->second.complete){ // Is the symbol incomplete?
          missing_symbols_of_block.push_back(symbol_it->first); // Add the incomplete symbol to the array
          ++count;
        }
      }
      if (missing_symbols_of_block.size() > 0) {
        // Add the array of incomplete symbols to the symbol_map with the block_it->first as the key
        (*missing_symbols)[block_it->first] = missing_symbols_of_block;
      }
    }
  }


  auto missing_symbols_gauge = metricsInstance.getOrCreateGauge("missing_symbols_gauge");
  missing_symbols_gauge->Increment(count);

  // Calculate the percentage of missing symbols
  double percentage_missing = (double)count / (double)total_symbol_count * 100.0;
  auto alc_percentage_to_retrieve = metricsInstance.getOrCreateGauge("alc_percentage_to_retrieve");
  alc_percentage_to_retrieve->Set(percentage_missing);

  _missing_cb(*this, missing_symbols);
}