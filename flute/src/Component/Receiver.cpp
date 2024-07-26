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
#include "Component/Receiver.h"
#include "Utils/base64.h"

#include "public/tracy/Tracy.hpp"

// #include <iomanip>    // For std::setw and std::setfill
// #include <sstream>    // For std::stringstream

LibFlute::Receiver::Receiver(const std::string &iface, const std::string &address,
                             const std::string &retreival_url,
                             short port, uint64_t tsi,
                             boost::asio::io_service &io_service,
                             std::shared_ptr<LibFlute::FakeNetworkSocket> fake_network_socket)
    : _socket(io_service), _tsi(tsi), _mcast_address(address), _fetcher(retreival_url), _fake_network_socket(fake_network_socket)
{
  ZoneScopedN("Receiver::Receiver");

  // The bigger the buffer, the more symbols we can buffer and the larger the file is that we can handle,
  // but the more memory we use
  // Knowing that max_length is 2048, the max memory usage should still be quite small (~64 MBytes)
  // Actuall max length is 1452, so max memory usage is even smaller
  _unknown_alc_buffer.set_capacity(32768);

  // The bigger the buffer, the more symbols we can buffer and the larger the file is that we can handle,
  // but the more memory we use
  // Knowing that max_length is 2048, the max memory usage should still be quite small (~64 MBytes)
  // Actuall max length is 1452, so max memory usage is even smaller
  _alc_buffer.set_capacity(32768);

  boost::asio::ip::udp::endpoint listen_endpoint(
      boost::asio::ip::address::from_string(iface), port);
  _socket.open(listen_endpoint.protocol());
  _socket.set_option(boost::asio::ip::multicast::enable_loopback(true));
  _socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
  _socket.set_option(boost::asio::socket_base::receive_buffer_size(64 * 1024 * 1024));
  _socket.bind(listen_endpoint);

  // Join the multicast group.
  _socket.set_option(
      boost::asio::ip::multicast::join_group(
          boost::asio::ip::address::from_string(address)));

  if (_fake_network_socket) {
      _fetcher.set_fake_network_socket(_fake_network_socket);
  }

  _fetcher.register_alc_callback(
      [&](const char *alc_data, size_t alc_length)
      {
        if (alc_length == 0) {
          return;
        }

        FrameMarkStart("Receiver::fetcher_alc_callback");

        // Make a non const c string.
        char *data = (char *)malloc(alc_length + 1);
        //TracyAlloc(data, alc_length + 1);
        memcpy(data, alc_data, alc_length);

        try {
          // Create an ALC packet from the received data
          auto alc_ptr = std::make_shared<LibFlute::AlcPacket>(data, alc_length);
          alc_ptr->may_buffer_if_unknown = false;

          // Handle the received ALC
          handle_alc_step_three(alc_ptr);
        } catch (std::exception &ex)
        {
          spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", ex.what());
        }
        catch (const char *errorMessage)
        {
          spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", errorMessage);
        } catch (...)
        {
          spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: unknown error");
        }

        // Free the non const c string.
        if (data) {
          //TracyFree(data);
          free(data);
        }

        FrameMarkEnd("Receiver::fetcher_alc_callback");
      });

  _fetcher.register_fdt_callback(
    [&](const char *fdt_data, size_t fdt_length)
    {

      if (fdt_length == 0) {
        return;
      }

      // Make a non const c string.
      char *data = (char *)malloc(fdt_length + 1);
      //TracyAlloc(data, fdt_length + 1);
      memcpy(data, fdt_data, fdt_length);

      // Prevent other files and fdts from being handled while we are parsing the FDT
      std::unique_lock<LockableBase(std::mutex)> lock(_files_mutex);

      try {
        // Set an artificial instance id for the FDT
        // In case that we already have an FDT, then we use the instance id of that FDT
        // This allows future FDTs to be handled correctly
        uint32_t instance_id = _fdt ? _fdt->instance_id() : 0;
        // Make the FDT with a unique pointer. If there is already an FDT, then it will be replaced.
        _fdt = std::make_unique<LibFlute::FileDeliveryTable>(
            instance_id, data, fdt_length);
      } catch (std::exception &ex) {
        spdlog::warn("[RECEIVE] Failed to parse FDT: {}", ex.what());
        lock.unlock();
        return;
      } catch (const char *errorMessage) {
        spdlog::warn("[RECEIVE] Failed to parse FDT: {}", errorMessage);
        lock.unlock();
        return;
      } catch (...)
      {
        spdlog::warn("[RECEIVE] Failed to parse FDT: unknown error");
        lock.unlock();
        return;
      }

      // Free the non const c string.
      if (data) {
        //TracyFree(data);
        free(data);
      }

      // Final check if the FDT is resolved
      if (!_fdt)
      {
        spdlog::warn("[RECEIVE] Cannot resolve FDT for buffered ALCs: no FDT available");
        lock.unlock();
        return;
      }

      // The files lock is still needed when we handle the FDT
      handle_fdt_step_one();
      lock.unlock();
      // The second step in handling the FDT is not time critical, so we can do it outside of the files lock
      handle_fdt_step_two();
    });

    // Handle an empty reception, this will start the reception loop
    handle_receive_from(boost::system::error_code(), 0);
}

LibFlute::Receiver::~Receiver()
{
  spdlog::debug("[RECEIVE] Destroying Receiver");
  _socket.close();
}

auto LibFlute::Receiver::enable_ipsec(uint32_t spi, const std::string &key) -> void
{
  LibFlute::IpSec::enable_esp(spi, _mcast_address, LibFlute::IpSec::Direction::In, key);
}

auto LibFlute::Receiver::handle_receive_from(const boost::system::error_code &error,
                                             size_t bytes_recvd) -> void
{
  if (!_running) {
#ifdef SIMULATED_PKT_LOSS
    spdlog::warn("[RECEIVE] Stopping reception: total packets dropped {}", packets_dropped);
#endif // SIMULATED_PKT_LOSS
    return;
  }

  if (!error)
  {
    if (bytes_recvd > 0) {
      FrameMarkStart("Receiver::handle_receive_from");
      spdlog::trace("[RECEIVE] Received {} bytes", bytes_recvd);
      LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
      auto multicast_bytes_received = metricsInstance.getOrCreateGauge("multicast_bytes_received");
      multicast_bytes_received->Increment(bytes_recvd);
      /*
      auto len_to_display = bytes_recvd < 50 ? bytes_recvd : 50;
      std::stringstream hex_stream;
      hex_stream << std::hex << std::setfill('0');
      for (int i = 0; i < len_to_display; ++i) {
          auto c = _data[i];
          hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
          if (i % 2 == 1) {
              hex_stream << ' ';  // Add a space every two characters
          }

      }
      spdlog::info("First {} / {} bytes in hex:   {}", len_to_display, bytes_recvd, hex_stream.str());
      */
      handle_alc_step_one(_data, bytes_recvd, true);
      //spdlog::trace("Reception has been handled");
      FrameMarkEnd("Receiver::handle_receive_from");
    }

    if (_fake_network_socket != nullptr) {
      _fake_network_socket->async_receive_from(
          boost::asio::buffer(_data, max_length),
          [&](boost::system::error_code error, size_t bytes_recvd) {
            this->handle_receive_from(error, bytes_recvd);
          });
    } else {
      _socket.async_receive_from(
          boost::asio::buffer(_data, max_length), _sender_endpoint,
          boost::bind(&LibFlute::Receiver::handle_receive_from, this,
                      boost::asio::placeholders::error,
                      boost::asio::placeholders::bytes_transferred));
    }
  }
  else
  {
    spdlog::error("[RECEIVE] receive_from error: {}", error.message());
  }
}

auto LibFlute::Receiver::handle_alc_step_one(char* data, size_t bytes_recvd, bool buffer_if_unknown) -> void
{
  ZoneScopedN("Receiver::handle_alc_step_one");
  try
  {
    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    auto alcs_received = metricsInstance.getOrCreateGauge("alcs_received");
    alcs_received->Increment();

    // Create an ALC packet from the received data
    auto alc_ptr = std::make_shared<LibFlute::AlcPacket>(data, bytes_recvd);
    
    // Check if the TSI matches
    if (alc_ptr->tsi() != 0 && alc_ptr->tsi() != _tsi)
    {
      spdlog::warn("[RECEIVE] Discarding packet for unknown TSI {}", alc_ptr->tsi());
      return;
    }

    // The ALC packet is valid, so we can continue handling it
    handle_alc_step_two(alc_ptr, buffer_if_unknown && alc_ptr->toi() != 0);
  }
  catch (std::exception &ex)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", ex.what());
  }
  catch (const char *errorMessage)
  {
    spdlog::warn("Failed to decode ALC/FLUTE packet: {}", errorMessage);
  } catch (...)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: unknown error");
  }
}

auto LibFlute::Receiver::handle_alc_step_two(std::shared_ptr<AlcPacket> alc_ptr, bool buffer_if_unknown) -> void
{
  ZoneScopedN("Receiver::handle_alc_step_two");
  try
  {

    // Check if the ptr exists
    if (!alc_ptr)
    {
      spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: no ALC pointer");
      return;
    }

    // If the TOI is unknown during the next step, then we can buffer the ALC
    // This boolean enables or disables this behaviour
    alc_ptr->may_buffer_if_unknown = buffer_if_unknown;
    
    // Prevent other threads from accessing the buffer 
    std::unique_lock<LockableBase(std::mutex)> buffer_lock(_buffer_mutex);

    if (_alc_buffer.full()){
        spdlog::warn("[RECEIVE] ALC buffer full, dropping ALC packet");   
        // We unfortunately have to drop the packet, because we cannot buffer it.
        // Buffering the packet would drop the oldest packet, but that packet just makes another file symbol incomplete.
        // We will rely on the recovery mechanism (using Fetcher) to get the packet again in a later time.
        buffer_lock.unlock();
        return;
    }


    // Save the ALC in the buffer, so that we can handle step three later or from another thread, this has to be memory safe
    _alc_buffer.push_back(alc_ptr);
    // spdlog::debug("ALC for TOI {} buffered. New buffer size is {}", alc_ptr->toi(), _alc_buffer.size());
    buffer_lock.unlock();
    return;
  }
  catch (std::exception &ex)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", ex.what());
  }
  catch (const char *errorMessage)
  {
    spdlog::warn("Failed to decode ALC/FLUTE packet: {}", errorMessage);
  } catch (...)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: unknown error");
  }
}

auto LibFlute::Receiver::handle_alc_buffer() -> bool {
  // Prevent other threads from accessing the buffer 
  std::unique_lock<LockableBase(std::mutex)> buffer_lock(_buffer_mutex);
  // Check if the buffer is empty
  if (_alc_buffer.empty()) {
    buffer_lock.unlock();
    return false;
  }

  ZoneScopedN("Receiver::handle_alc_buffer");
  // spdlog::info("Handling buffered ALC. New buffer size is {}", _alc_buffer.size() - 1);

  try
  {

    // Get the first ALC from the buffer
    auto alc_ptr = _alc_buffer.front();
    // Remove the ALC from the buffer
    _alc_buffer.pop_front();

    buffer_lock.unlock();

    // spdlog::info("[RECEIVE] Handling buffered ALC for TOI {} with size {}", alc_ptr->toi(), alc_ptr->size());

    handle_alc_step_three(alc_ptr);

    return true;

  }
  catch (std::exception &ex)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", ex.what());
  }
  catch (const char *errorMessage)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", errorMessage);
  }
  catch (...)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: unknown error");
  }

  // Check if the buffer_lock is still locked, if so, then unlock it
  if (buffer_lock.owns_lock()) {
    buffer_lock.unlock();
  }

  return false;
}

auto LibFlute::Receiver::handle_alc_step_three(std::shared_ptr<AlcPacket> alc_ptr) -> void
{
  ZoneScopedN("Receiver::handle_alc_step_three");
  // Check if the ptr exists
  if (!alc_ptr)
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: no ALC pointer");
    return;
  }

  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();

  std::unique_lock<LockableBase(std::mutex)> files_lock(_files_mutex);

  // Check if the ALC belongs to a FDT.
  if (alc_ptr->toi() == 0)
  {


    // spdlog::info("[RECEIVE] ALC:\n{}", std::string(alc_ptr->data(), alc_ptr->size()));

    // Add FDT to files, but only if the instance id within the ALC is different from the FDT which we are currently using and there is no FDT currently within files
    // This prevents handling re-transmitted FDTs. We only want to handle the FDT once.
    // Example, the FDT with instance id 1 is send 3 times, but we only receive the second and third transmission,
    // then we only want to handle the second transmission.
    if ((!_fdt || _fdt->instance_id() != alc_ptr->fdt_instance_id()) && _files.find(alc_ptr->toi()) == _files.end())
    {
      FileDeliveryTable::FileEntry fe{0, 0, "", static_cast<uint32_t>(alc_ptr->fec_oti().transfer_length), "", "", 0, 0, alc_ptr->fec_oti(), 0};
      // We use File for the FDT, because this has the most complete implementation of the FileBase interface
      std::shared_ptr<LibFlute::FileBase> file = std::make_shared<LibFlute::File>(fe);
      _files.emplace(alc_ptr->toi(), file);
    } else {
      spdlog::debug("[RECEIVE] Discarding packet: already handled FDT with instance id {}", alc_ptr->fdt_instance_id());
      files_lock.unlock();
      return;
    }
  /*} else {
    spdlog::info("[RECEIVE] ALC for TOI {}", alc_ptr->toi());
    spdlog::info("[RECEIVE] ALC:\n{}", std::string(alc_ptr->data(), alc_ptr->size()));
  */}

  // Check if the ALC does not belong to a file that we know
  if (_files.find(alc_ptr->toi()) == _files.end() )
  {
    // The file is not known, so we discard or buffer the ALC (if the ALC does not belong to an FDT)

    if (alc_ptr->may_buffer_if_unknown && alc_ptr->toi() != 0)
    {
      // Push the ALC to the buffer, we will handle it once we receive a new FDT
      // This buffer is circular, if it is full, then the oldest ALC will be removed first
      _unknown_alc_buffer.push_back(alc_ptr);

      // Log the buffer size
      auto alcs_buffer_size = metricsInstance.getOrCreateGauge("alcs_buffer_size");
      alcs_buffer_size->Set(_unknown_alc_buffer.size());
      auto alcs_buffered = metricsInstance.getOrCreateGauge("alcs_buffered");
      alcs_buffered->Increment();
      spdlog::trace("[RECEIVE] Added discared packet to temp buffer with TOI {}", alc_ptr->toi());
    } else {
      // End of the line, we don't know the file and we don't want to buffer it, so we discard it
      auto alcs_ignored = metricsInstance.getOrCreateGauge("alcs_ignored");
      alcs_ignored->Increment();
      spdlog::trace("[RECEIVE] Discarding packet: unknown file with TOI {}", alc_ptr->toi());
    }

    // Stop the handling of the ALC
    files_lock.unlock();
    return;
  }

  // The file is known, so we can continue handling the ALC

  // Check if the ALC belongs to an FDT
  if (alc_ptr->toi() == 0)
  {
    auto file = _files[alc_ptr->toi()];

    files_lock.unlock();
    // This is a special case, because we need to handle the FDT before we can handle any other ALC
    // Without this, we would need to add the other ALCs to the unknown buffer and then try to resolve the FDT
    // Extra motivation: handling the FDT is very fast, because it does not support FEC. Only other ALCs in FLUTE might use FEC.
    handle_alc_step_four(file, alc_ptr);
    return;
  }

  // Hand the ALC of to the file
  // The file has a buffer that is processed by a separate thread (unique to that file), so we can continue handling ALCs from other files, without blocking this thread.
  _files[alc_ptr->toi()]->push_alc_to_receive_buffer(alc_ptr);

  files_lock.unlock();

}

auto LibFlute::Receiver::handle_alc_step_four(std::shared_ptr<LibFlute::FileBase> file, std::shared_ptr<AlcPacket> alc_ptr) -> void {

  ZoneScopedN("Receiver::handle_alc_step_four");
  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();

  //spdlog::debug("Handling buffered ALC for TOI {} with size {}", alc_ptr->toi(), alc_ptr->size());

  // Check if the file is already complete
  if (file->complete())
  {
    auto alcs_ignored = metricsInstance.getOrCreateGauge("alcs_ignored");
    alcs_ignored->Increment();
    spdlog::trace("[RECEIVE] Discarding packet: already completed file with TOI {}", alc_ptr->toi());
    // Quickly check if there are any buffered ALCs that belong to the completed file, we can remove them to prevent unnecessary handling.
    pop_toi_from_buffer_fronts(alc_ptr->toi());
    return;
  }

  // Our file has not been completed yet,
  // Get the encoding symbols from the payload
  auto encoding_symbols = LibFlute::EncodingSymbol::from_payload(
      alc_ptr->data(), // Payload
      alc_ptr->size(), // Size of the payload
      file->fec_oti(),
      alc_ptr->content_encoding());
  
  auto symbols_received = metricsInstance.getOrCreateGauge("symbols_received");
  symbols_received->Increment(encoding_symbols.size());

  if (encoding_symbols.empty())
  {
    spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: no encoding symbols found");
    return;
  }

  // Do the (possibly) time consuming part of handling the ALC
  // Add the symbols to the file
  for (const auto &symbol : encoding_symbols)
  {
    /*
    // Print the first 40 bytes of the symbol in hex
    char symbol_data[40];
    symbol.decode_to(symbol_data, 40);
    std::stringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    for (int i = 0; i < 40; ++i) {
        hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(symbol_data[i]));
        if (i % 2 == 1) {
          hex_stream << ' ';  // Add a space every two characters
        }
    }
    spdlog::debug("[RECEIVE] First 40 bytes of symbol in hex: {}", hex_stream.str());
    */
    try {
      // spdlog::trace("[RECEIVE] Saving TOI {} SBN {} ID {}, size {}", alc_ptr->toi(), symbol.source_block_number(), symbol.id(), symbol.len());
      file->put_symbol(symbol);
    } catch (const char *errorMessage) {
      spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: {}", errorMessage);
    } catch (...)
    {
      spdlog::warn("[RECEIVE] Failed to decode ALC/FLUTE packet: unknown error");
    }

  }

  // Check if the file is complete now
  if (!file->complete())
  {
    // If the file is not complete yet, then we are done.
    return;
  }

  spdlog::debug("[RECEIVE] File with TOI {} completed", alc_ptr->toi());

  // The file is complete, we will do some operations on the files map or on _fdt, so we need to lock it
  std::unique_lock<LockableBase(std::mutex)> files_lock(_files_mutex);

  //spdlog::debug("[RECEIVE] Lock acquired for file with TOI {}", alc_ptr->toi());


  // Check if the ALC belongs to an FDT
  if (alc_ptr->toi() == 0)
  {
    auto fdt_received = metricsInstance.getOrCreateGauge("fdt_received");
    fdt_received->Increment();

    try {
      // Cast file from FileBase to File
      // auto f = std::dynamic_pointer_cast<LibFlute::File>(file);
      // Parse the completed FDT
      _fdt = std::make_unique<LibFlute::FileDeliveryTable>(
          alc_ptr->fdt_instance_id(), file->buffer(), file->length());
    } catch (const char *errorMessage) {
      _fdt = nullptr;
      _files.erase(alc_ptr->toi());
      files_lock.unlock();
      spdlog::warn("[RECEIVE] Failed to parse FDT: {}", errorMessage);
      return;
    } catch (...)
    {
      _fdt = nullptr;
      _files.erase(alc_ptr->toi());
      files_lock.unlock();
      spdlog::warn("[RECEIVE] Failed to parse FDT: unknown error");
      return;
    }

    _files.erase(alc_ptr->toi());

    // The files lock is still needed when we handle the FDT
    handle_fdt_step_one();
    files_lock.unlock();
    // The second step in handling the FDT is not time critical, so we can do it outside of the files lock
    handle_fdt_step_two();

    return;
  }

  // From this point on, we are only handling files, not FDTs

  // Check if there is any other file with the same content location
  /*
  for (auto it = _files.begin(); it != _files.end();)
  {
    if (it->first != 0 && it->second.get() != file.get() && it->second->meta().content_location == file->meta().content_location)
    {
      // If so, then we will 'clear' that file.
      spdlog::debug("[RECEIVE] Removing file with TOI {}", it->first);
      // First, we remove the deadline, this prevents any future recovery for this file.
      it->second.get()->meta().should_be_complete_at = 0;

      // Note that in the main time, new packets for the file will be added again to the buffer. These are immediately discareded if the thread is not running.
      it->second.get()->stop_receive_thread(false);

      // Next, we remove it's FEC transformer, if it has one.
      if (it->second.get()->meta().fec_transformer){
        delete it->second.get()->meta().fec_transformer;
        it->second.get()->meta().fec_transformer = 0;
      }

      // Don't erase the file just yet, otherwise our buffer of discared ALCs might try to handle the completed file
      // _files.erase(alc_ptr->toi());
      // Instead, we just free the buffer.
      it->second.get()->free_buffer();
      // The file will be removed from the list of files when the file is expired.

      // Now, go to the next file.
      ++it;
    }
    else
    {
      ++it;
    }
  }
  */
  
  // We can unlock the files map now.
  files_lock.unlock();

  // We only call the completion callback for files that are not part of a stream
  if (_completion_cb && file->meta().stream_id == 0) {
    // Call the completion callback
    _completion_cb(file);
  }

  // Time to 'clear' the file
  // First we remove it's FEC transformer, if it has one
  if (file->meta().fec_transformer){
    delete file->meta().fec_transformer;
    file->meta().fec_transformer = 0;
  }

  // Secondly, prevent the file from being recovered in the future.
  // The file is complete, so it shouldn't be recovered anyway, but it is better to be sure
  file->meta().should_be_complete_at = 0;

  // Stop the file's receive thread
  file->stop_receive_thread(false);

  // Don't erase the file just yet, otherwise our buffer of discared ALCs might try to handle the completed file
  // _files.erase(alc_ptr->toi());
  // Instead, we just free the buffer.
  file->free_buffer();
  // The file will be removed from the list of files when the file is expired.

  // Quickly check if there are any buffered ALCs that belong to the completed file, we can remove them to prevent unnecessary handling.
  pop_toi_from_buffer_fronts(alc_ptr->toi());

  // TODO: remove toi from any stream inside _stream_tois
}

auto LibFlute::Receiver::handle_fdt_step_one() -> void
{
  ZoneScopedN("Receiver::handle_fdt_step_one");

  const std::lock_guard<LockableBase(std::mutex)> lock(_spawn_files_mutex);

  // Automatically receive all files in the FDT
  for (const auto &file_entry : _fdt->file_entries())
  {
    // Check if the file is already in the list of files, if not then add it
    if (_files.find(file_entry.toi) == _files.end())
    {
      /*
      // Create a shared pointer copy of the file entry
      auto file_entry_shared_ptr = std::make_shared<LibFlute::FileDeliveryTable::FileEntry>(file_entry);
      // Spawn a new file from a new thread using spawn_file
      auto spawn_file_thread = std::jthread([this, file_entry]() {
        ZoneScopedN("Receiver::spawn_file_thread");
        LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
        metricsInstance.addThread(std::this_thread::get_id(), "spawn_file_thread");
        spawn_file(file_entry);
      });

      // Add the thread to the _file_spawn_threads vector
      _file_spawn_threads.push_back(std::move(spawn_file_thread)); 
      */
      try {
        spawn_file(file_entry);
      } catch (std::exception &ex) {
        spdlog::warn("[RECEIVE] Failed to spawn file: {}", ex.what());
      } catch (const char *errorMessage) {
        spdlog::warn("[RECEIVE] Failed to spawn file: {}", errorMessage);
      } catch (...)
      {
        spdlog::warn("[RECEIVE] Failed to spawn file: unknown error");
      } 
    }
  }
}

auto LibFlute::Receiver::spawn_file(const LibFlute::FileDeliveryTable::FileEntry &file_entry) -> void
{
  ZoneScopedN("Receiver::spawn_file");
  spdlog::debug("[RECEIVE] Starting reception for file with TOI {}: {} ({}), size {}, FEC {}", file_entry.toi,
                    file_entry.content_location, file_entry.content_type, file_entry.content_length, file_entry.fec_oti.encoding_id);

  // Check if file_entry has a stream id greater than 0
  auto is_stream = file_entry.stream_id > 0;

  // Create the file, this can be time consuming when using FEC.
  std::shared_ptr<LibFlute::FileBase> file;
  if (is_stream) {
    file = std::make_shared<LibFlute::FileStream>(file_entry);
  } else {
    file = std::make_shared<LibFlute::File>(file_entry);
  }

  // Add a callback that tries to fix a file that misses some parts.
  file->register_missing_callback(
    [&](LibFlute::FileBase &incomplete_file, std::shared_ptr<std::map<uint16_t, std::vector<uint16_t>>> missing_symbols) { // NOLINT
      ZoneScopedN("Receiver::fetch missing_callback");

      // Check if the vector of missing symbols is empty
      if (missing_symbols->empty())
      {
        // The vector of missing symbols is empty, so we don't need to do anything
        return;
      }

      auto missing_symbols_found_in_buffer = 0;

      // If the file has a fec transformer, then iterate over over our missing symbols map and remove the FEC repair symbols
      if (false && incomplete_file.meta().fec_transformer && incomplete_file.meta().fec_oti.encoding_id == FecScheme::Raptor)
      {
        // Iterate over the missing symbols map
        for (auto it = missing_symbols->begin(); it != missing_symbols->end();)
        {
          // Check if the source block is missing any symbols
          if (it->second.empty())
          {
  
            // It is not missing any symbols, so we can remove the source block number from the missing symbols map
            it = missing_symbols->erase(it);
            continue;
          }

          auto nsymbols = incomplete_file.meta().fec_transformer->get_source_block_length(it->first);
          
          // Iterate over the symbols, if their id is greater than (not equal) to nsymbols, then remove them from the missing symbols map
          // We assume that our symbols are always correct, so we only need to receive as much symbols as the source symbol count + 1
          for (auto it2 = it->second.begin(); it2 != it->second.end();)
          {
            if (*it2 > nsymbols)
            {
              it2 = it->second.erase(it2);
              ++missing_symbols_found_in_buffer;
              continue;
            }
            ++it2;
          }

          // Check if the vector of missing symbols is empty
          if (it->second.empty())
          {
            // The vector of missing symbols is empty, so we can remove the source block number from the missing symbols map
            it = missing_symbols->erase(it);
            continue;
          }

          ++it;
        }

        spdlog::debug("[RECEIVE] Not fetching {} missing symbols because they are FEC repair symbols", missing_symbols->size());
      }

      missing_symbols_found_in_buffer = 0;

      auto encoding_id = incomplete_file.meta().fec_oti.encoding_id;

      std::unique_lock<LockableBase(std::mutex)> buffer_lock(_buffer_mutex);
      
      // Iterate over the buffered ALCs and try to find the missing symbols
      for (auto it_alc = _alc_buffer.begin(); it_alc != _alc_buffer.end(); ++it_alc) {
        if (it_alc->get()->toi() != incomplete_file.meta().toi)
        {
          // The ALC does not belong to the completed file, so we can skip this one.
          continue;
        }

        // Get the encoding symbols from the payload
        auto buffered_symbols = EncodingSymbol::from_payload(
          it_alc->get()->data(), // Payload
          it_alc->get()->size(), // Size of the payload
          incomplete_file.meta().fec_oti,
          it_alc->get()->content_encoding());

        // Check if the ALC contains any encoding symbols
        if (buffered_symbols.empty())
        {
          // The ALC does not contain any encoding symbols, so we skip this one
          continue;
        }

        // Iterate over the buffered symbols and try to find the missing symbols
        for (const auto &buffered_symbol : buffered_symbols)
        {
          // Check if the source block number exists in the missing symbols map
          auto it = missing_symbols->find(buffered_symbol.source_block_number());
          if (it == missing_symbols->end())
          {
            // The source block number does not exist in the missing symbols map, so we skip this one
            continue;
          }

          // Check if the encoding symbol id exists in the missing symbols map
          auto it2 = std::find(it->second.begin(), it->second.end(), buffered_symbol.id());
          if (it2 == it->second.end())
          {
            // The encoding symbol id does not exist in the missing symbols map, so we skip this one
            continue;
          }

          // The encoding symbol id exists in the missing symbols map, so we can remove it
          it->second.erase(it2);
          ++missing_symbols_found_in_buffer;
        }
      }

      if (missing_symbols_found_in_buffer > 0) {
        spdlog::debug("[RECEIVE] Found {} missing symbols in shared buffer. Buffer size is: {}", missing_symbols_found_in_buffer, _alc_buffer.size());
      }
      buffer_lock.unlock();

      // Check if the vector of missing symbols is empty
      if (missing_symbols->empty())
      {
        // The vector of missing symbols is empty, so we don't need to do anything
        return;
      }

      // Now retreive all the symbols from file->get_buffered_symbols() and try to find the missing symbols
      auto buffered_symbols = std::vector<EncodingSymbol>();
      incomplete_file.get_buffered_symbols(buffered_symbols);

      // Iterate over the buffered symbols and try to find the missing symbols
      missing_symbols_found_in_buffer = 0;
      for (const auto &buffered_symbol : buffered_symbols)
      {
        // Check if the source block number exists in the missing symbols map
        auto it = missing_symbols->find(buffered_symbol.source_block_number());
        if (it == missing_symbols->end())
        {
          // The source block number does not exist in the missing symbols map, so we skip this one
          continue;
        }

        // Check if the encoding symbol id exists in the missing symbols map
        auto it2 = std::find(it->second.begin(), it->second.end(), buffered_symbol.id());
        if (it2 == it->second.end())
        {
          // The encoding symbol id does not exist in the missing symbols map, so we skip this one
          continue;
        }

        // The encoding symbol id exists in the missing symbols map, so we can remove it
        it->second.erase(it2);
        ++missing_symbols_found_in_buffer;
      }
      if (missing_symbols_found_in_buffer > 0) {
        spdlog::debug("[RECEIVE] Found {} missing symbols in file received ALCs buffer. Buffer size is: {}", missing_symbols_found_in_buffer, buffered_symbols.size());
      }

      _fetcher.fetch_alcs(incomplete_file.meta().toi, encoding_id, incomplete_file.meta().content_location, missing_symbols);
    });

  file->register_receiver_callback(
    [&](std::shared_ptr<AlcPacket> a) { // NOLINT
      // The shared pointer looses a count here in the compiler, let's retrieve it to solve the issue
      std::unique_lock<LockableBase(std::mutex)> files_lock2(_files_mutex);
      auto file_shared_ptr = _files[a->toi()];
      files_lock2.unlock();
      handle_alc_step_four(file_shared_ptr, a);
    });

  if (is_stream) {
    auto f_stream = std::dynamic_pointer_cast<LibFlute::FileStream>(file);
    f_stream->register_emit_message_callback(
      [&](uint32_t stream_id, std::string message) {
        if (_emit_message_cb) {
          _emit_message_cb(stream_id, message);
        }
      });
    
    // First create a vector in the map if it does not exists yet
    if (_stream_tois.find(file_entry.stream_id) == _stream_tois.end()) {
      _stream_tois[file_entry.stream_id] = std::vector<uint64_t>();
    }
    // Add the TOI to the vector
    _stream_tois[file_entry.stream_id].push_back(file_entry.toi);

    // Search for the previous tois in _stream_tois map, which holds the TOIs of the previous files in the stream at the value, the key is the stream id
    // The previous TOI is the greatest value that is at least 1 smaller than the current TOI
    uint64_t previous_toi = 0;
    for (const auto &toi : _stream_tois[file_entry.stream_id]) {
      if (toi < file_entry.toi && toi > previous_toi) {
        previous_toi = toi;
      }
    }
    // Check if the previous TOI is not 0
    if (previous_toi != 0) {
      // Check if the previous TOI is in the files map
      if (_files.find(previous_toi) != _files.end()) {
        // Get the previous file from the files map
        auto previous_file = _files[previous_toi];
        // Check if the previous file is complete
        auto f_previous_stream = std::dynamic_pointer_cast<LibFlute::FileStream>(previous_file);
        f_previous_stream->set_next_file(f_stream);
        f_stream->set_previous_file(f_previous_stream);
      }
    }
  

  }

  bool may_receive = true;
  // Check if the file contains any video id from the list of video ids (shared ptr), the video id should occur like ...'/video_id/'...
  if (_video_ids_ptr && !_video_ids_ptr->empty())
  {
    may_receive = false;
    for (const auto &video_id : *_video_ids_ptr)
    {
      // Create a new string like this '/video_id/'
      std::string video_id_string = "/" + video_id + "/";
      if (file->meta().content_location.find(video_id_string) != std::string::npos)
      {
        may_receive = true;
        break;
      }
    }
  }
  

  // Each file has it's own thread that handles the received ALCs
  if (may_receive) {
    file->start_receive_thread();
  } else {
    file->ignore_reception();
  }

  // Lock the files map, so we can add the file
  //const std::lock_guard<LockableBase(std::mutex)> files_lock(_files_mutex);
  _files.emplace(file_entry.toi, file);
}

auto LibFlute::Receiver::handle_fdt_step_two() -> void
{
  ZoneScopedN("Receiver::handle_fdt_step_two");
  LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
  std::unique_lock<LockableBase(std::mutex)> spawn_files_lock(_spawn_files_mutex);
  if (!_file_spawn_threads.empty()) {
    spdlog::debug("[RECEIVE] Waiting for {} file spawn threads to finish", _file_spawn_threads.size());
    // Iterate over the _file_spawn_threads vector and wait for all the threads to finish
    for (auto &thread : _file_spawn_threads)
    {
      if (thread.joinable()) {
        thread.join();
        spdlog::debug("[RECEIVE] File spawn thread finished");
      }
    }
    // Clear all the files from the _file_spawn_threads vector
    _file_spawn_threads.clear();
  }
  spawn_files_lock.unlock();

  // Now that we have parsed an FDT, we can try to handle buffered ALCs
  // The unknown buffer uses the files mutex, not the buffer mutex
  // We need to lock it, so it is thread safe
  const std::lock_guard<LockableBase(std::mutex)> file_lock(_files_mutex);
  if (!_unknown_alc_buffer.empty()) {
    spdlog::trace("[RECEIVE] Re-handling ALCs that were previously unknown");
  }
  while (!_unknown_alc_buffer.empty())
  {
    auto f_alc = _unknown_alc_buffer.front();
    // Remove the ALC from the unknown buffer
    _unknown_alc_buffer.pop_front();
    // Check if f_alc does not belong to an FDT
    // This should not happen, but we check it anyway to be sure
    // Otherwise we might cause a deadlock!
    if (f_alc->toi() != 0)
    {
      // Second parameter: We just handle them now and discard them if they are still unknown. We don't want to buffer them again, because we will probably never receive the FDT for them.
      handle_alc_step_two(f_alc, false);
    }
  }

  // Log the buffer size
  auto alcs_buffer_size = metricsInstance.getOrCreateGauge("alcs_buffer_size");
  alcs_buffer_size->Set(_unknown_alc_buffer.size());

  spdlog::debug("[RECEIVE] FDT handling finished");
}

auto LibFlute::Receiver::pop_toi_from_buffer_fronts(uint64_t toi) -> void {
  // Quickly check if there are any buffered ALCs that belong to the completed file, we can remove them to prevent unnecessary handling.
  ZoneScopedN("Receiver::pop_toi_from_buffer_fronts");
  //spdlog::trace("[RECEIVE] Removing buffered ALCs for TOI {}", toi);
  std::lock_guard<LockableBase(std::mutex)> buffer_lock(_buffer_mutex);

  auto ignored_counter = 0;
  while (!_alc_buffer.empty())
  {
    // Get the first ALC from the buffer
    auto f_alc = _alc_buffer.front();
    if (f_alc->toi() != toi)
    {
      // The ALC does not belong to the completed file, so we can stop
      break;
    }
    // Remove the ALC from the unknown buffer
    _alc_buffer.pop_front();
    ++ignored_counter;
  }

  // We do the same for the unknown buffer, although this should not be necessary
  // The unknown buffer uses the files mutex, not the buffer mutex
  // We need to lock it, so it is thread safe
  std::unique_lock<LockableBase(std::mutex)> file_lock(_files_mutex);
  while (!_unknown_alc_buffer.empty())
  {
    // Get the first ALC from the buffer
    auto f_alc = _unknown_alc_buffer.front();
    if (f_alc->toi() != toi)
    {
      // The ALC does not belong to the completed file, so we can stop
      break;
    }
    // Remove the ALC from the unknown buffer
    _unknown_alc_buffer.pop_front();
    ++ignored_counter;
  }
  if (ignored_counter > 0) {
    spdlog::debug("[RECEIVE] Removed {} buffered ALCs for TOI {}", ignored_counter, toi);
  } else {
    spdlog::trace("[RECEIVE] No buffered ALCs for TOI {}", toi);
  }
}

auto LibFlute::Receiver::resolve_fdt_for_buffered_alcs() -> void
{
  ZoneScopedN("Receiver::resolve_fdt_for_buffered_alcs");
  // The unknown buffer uses the files mutex, not the buffer mutex
  // We need to lock it, so it is thread safe
  std::unique_lock<LockableBase(std::mutex)> file_lock(_files_mutex);
  // Check if there are any buffered ALCs
  if (_unknown_alc_buffer.empty())
  {
    // No buffered ALCs, so we don't need to resolve the FDT
    file_lock.unlock();
    return;
  }

  file_lock.unlock();

  _fetcher.fetch_fdt();
}

auto LibFlute::Receiver::file_list() -> std::vector<std::shared_ptr<LibFlute::FileBase>>
{
  const std::lock_guard<LockableBase(std::mutex)> files_lock(_files_mutex);
  std::vector<std::shared_ptr<LibFlute::FileBase>> files;
  for (auto &f : _files)
  {
    files.push_back(f.second);
  }
  return files;
}

/**
 * Remove all files that have not been received after max_age seconds
 */
auto LibFlute::Receiver::remove_expired_files(unsigned max_age) -> void
{
  ZoneScopedN("Receiver::remove_expired_files");
  const std::lock_guard<LockableBase(std::mutex)> files_lock(_files_mutex);
  for (auto it = _files.cbegin(); it != _files.cend();)
  {
    auto age = time(nullptr) - it->second->received_at();
    if (it->second->meta().content_location != "bootstrap.multipart" && age > max_age)
    {
      if (it->second.get()->meta().fec_transformer){
        delete it->second.get()->meta().fec_transformer;
        it->second.get()->meta().fec_transformer = 0;
      }

      it->second.get()->stop_receive_thread(true);

      if (_removal_cb) {
        _removal_cb(it->second);
      }

      it = _files.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

auto LibFlute::Receiver::remove_file_with_content_location(const std::string &cl) -> void
{
  ZoneScopedN("Receiver::remove_file_with_content_location");
  const std::lock_guard<LockableBase(std::mutex)> files_lock(_files_mutex);
  for (auto it = _files.cbegin(); it != _files.cend();)
  {
    if (it->second->meta().content_location == cl)
    {
      if (it->second.get()->meta().fec_transformer){
        delete it->second.get()->meta().fec_transformer;
        it->second.get()->meta().fec_transformer = 0;
      }

      it->second.get()->stop_receive_thread(true);

      if (_removal_cb) {
        _removal_cb(it->second);
      }

      it = _files.erase(it);
    }
    else
    {
      ++it;
    }
  }
}
