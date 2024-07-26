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
#include "Component/Transmitter.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <bitset>
#include <iostream>
#include <string>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h> 

#include "Utils/IpSec.h"
#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"

#include "public/tracy/Tracy.hpp"

// #include <iomanip>    // For std::setw and std::setfill
// #include <sstream>    // For std::stringstream

LibFlute::Transmitter::Transmitter(const std::string &address,
                                   short port, uint64_t tsi, unsigned short mtu, uint32_t rate_limit, FecScheme fec_scheme,
                                   boost::asio::io_service &io_service, uint16_t toi, uint32_t instance_id)
    : _endpoint(boost::asio::ip::address::from_string(address), port),
      _socket(io_service, boost::asio::ip::udp::v4()),
      _fdt_timer(io_service),
      _send_timer(io_service),
      _io_service(io_service),
      _tsi(tsi),
      _mtu(mtu),
      _rate_limit(rate_limit),
      _mcast_address(address) {
    ZoneScopedN("Transmitter::Transmitter");
    _max_payload = mtu
                   - ( _endpoint.address().is_v6() ? 40 : 20) // IP header
                   - 8   // UDP header
                   - 32  // ALC Header with EXT_FDT and EXT_FTI
                   - 4;    // SBN and ESI for compact no-code or raptor FEC
    uint32_t max_source_block_length = 64; // Change this in Retriever.cpp as well

    unsigned int Al = 4; // RFC 5053: 4.2 Example Parameter Derivation Algorithm (change this in RaptorFEC.h and Retriever.cpp as well)
    switch(fec_scheme) {
        case FecScheme::Raptor:
            max_source_block_length = 842; // RFC 6681: 7.4 FEC Code Specification (change this in Retriever.cpp as well)
            if (_max_payload % Al) {
                _max_payload -= (_max_payload % Al); // Max payload must be divisible by Al.
            }
            break;
        default:
            break;
    }

    if (toi <= 1) {
        _toi = 1;
    } else {
        _toi = toi;
    }

    _socket = boost::asio::ip::udp::socket(io_service, _endpoint.protocol());
    // [IDLab] Set the TTL to 2
    _socket.set_option(boost::asio::ip::multicast::hops(2));
    
    //_socket.set_option(boost::asio::ip::multicast::enable_loopback(true));
    //_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));

    //_socket.set_option( boost::asio::socket_base::send_buffer_size( _max_payload ) );
    //_socket.set_option( boost::asio::socket_base::receive_buffer_size( _max_payload ) );

    _fec_oti = FecOti{fec_scheme, 0, _max_payload, max_source_block_length};
    _fdt = std::make_unique<FileDeliveryTable>(instance_id, _fec_oti);

    _fdt_timer.expires_from_now(boost::posix_time::seconds(_fdt_repeat_interval));
    _fdt_timer.async_wait(boost::bind(&Transmitter::fdt_send_tick, this));

    send_next_packet();
}

LibFlute::Transmitter::~Transmitter() {
    ZoneScopedN("Transmitter::~Transmitter");
    spdlog::debug("[TRANSMIT] Destroying Transmitter");
    _fdt_timer.cancel();
    _send_timer.cancel();
    clear_files();
    _socket.close();

}

auto LibFlute::Transmitter::enable_ipsec(uint32_t spi, const std::string &key) -> void {
    ZoneScopedN("Transmitter::enable_ipsec");
    LibFlute::IpSec::enable_esp(spi, _mcast_address, LibFlute::IpSec::Direction::Out, key);
}

auto LibFlute::Transmitter::seconds_since_epoch() -> uint64_t {
    ZoneScopedN("Transmitter::seconds_since_epoch");
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

auto LibFlute::Transmitter::send_fdt(bool should_lock) -> void {
    ZoneScopedN("Transmitter::send_fdt");
    if (_fdt->file_count() == 0) {
        // spdlog::debug("[TRANSMIT] No files to send in FDT");
        // Act like the FDT was sent, so we don't send it again too soon
        _last_fdt_sent = std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
        return;
    }
    _fdt->set_expires(seconds_since_epoch() + _fdt_repeat_interval * 2);
    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    auto multicast_fdt_sent_gauge = metricsInstance.getOrCreateGauge("multicast_fdt_sent");
    multicast_fdt_sent_gauge->Increment();
    auto fdt = _fdt->to_string();
    auto fdt_fec_oti = _fec_oti; // Copy the FEC OTI and modify it to send the FDT in "plaintext" (no FEC = compact no-code FEC)
    fdt_fec_oti.encoding_id = FecScheme::CompactNoCode;
    fdt_fec_oti.encoding_symbol_length = _mtu
        - ( _endpoint.address().is_v6() ? 40 : 20) // IP header
        - 8   // UDP header
        - 32  // ALC Header with EXT_FDT and EXT_FTI
        - 4;    // SBN and ESI for compact no-code or raptor FEC
    fdt_fec_oti.max_source_block_length = 64;
    // We use File instead of FileStream because this is the most complete implementation of the FileBase interface
    auto file = std::make_shared<File>(
        0,
        fdt_fec_oti,
        "",
        "",
        seconds_since_epoch() + _fdt_repeat_interval * 2,
        0, // No deadline mechanism for the FDT itself yet
        (char *)fdt.c_str(),
        fdt.length(),
        true,
        false); // Do not calculate the hash, the receiver does not need it
    file->set_fdt_instance_id(_fdt->instance_id());
    if (should_lock) {
        // spdlog::info("[TRANSMIT] Acquiring lock: send_fdt");
        std::unique_lock<LockableBase(std::mutex)> lock2(_files_mutex);
        _files.insert_or_assign(0, file);
        // Save last time that the FDT was sent
        _last_fdt_sent = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
            .count();

        // Write FDT string to file
        std::string file_location = "last.fdt";
        // Open the file as a file descriptor
        int fd = open(file_location.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
            spdlog::error("Failed to open FDT file for writing: {}", file_location);
            lock2.unlock();
            return;
        }
        // Lock the file, so no other process can read or write to it
        bool is_locked = flock(fd, LOCK_EX | LOCK_NB) == 0; // Non-blocking lock
        if (!is_locked) {
            spdlog::error("[TRANSMIT] Failed to lock FDT file {} for writing", file_location);
            close(fd);
            lock2.unlock();
            return;
        }

        // Get a filestream from the file descriptor
        FILE * file_stream = fdopen(fd, "wb");
        // Check if the filestream is valid
        if (!file_stream) {
            spdlog::error("Failed to open FDT file stream for writing: {}", file_location);
            close(fd);
            lock2.unlock();
            return;
        }
        // Write the buffer to the file in storage.
        fwrite(fdt.c_str(), 1, fdt.length(), file_stream);
        // Flush the filestream to write the data to the file
        fflush(file_stream);
        // Unlock the file
        flock(fd, LOCK_UN);
        // This also closes the underlying file descriptor
        fclose(file_stream);
        lock2.unlock();
    } else {
        _files.insert_or_assign(0, file); // TODO: This is possibly not safe, this might screw us when this is called within the iterator loop (over _files)
        // Save last time that the FDT was sent
        _last_fdt_sent = std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();

        // Write FDT string to file
        std::string file_location = "last.fdt";
        // Open the file as a file descriptor
        int fd = open(file_location.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
            spdlog::error("Failed to open FDT file for writing: {}", file_location);
            return;
        }
        // Lock the file, so no other process can read or write to it
        bool is_locked = flock(fd, LOCK_EX | LOCK_NB) == 0; // Non-blocking lock
        if (!is_locked) {
            spdlog::error("[TRANSMIT] Failed to lock FDT file {} for writing", file_location);
            close(fd);
            return;
        }

        // Get a filestream from the file descriptor
        FILE * file_stream = fdopen(fd, "wb");
        // Check if the filestream is valid
        if (!file_stream) {
            spdlog::error("Failed to open FDT file stream for writing: {}", file_location);
            close(fd);
            return;
        }
        // Write the buffer to the file in storage.
        fwrite(fdt.c_str(), 1, fdt.length(), file_stream);
        // Flush the filestream to write the data to the file
        fflush(file_stream);
        // Unlock the file
        flock(fd, LOCK_UN);
        // This also closes the underlying file descriptor
        fclose(file_stream);
    }
}

auto LibFlute::Transmitter::send(
    const std::string &content_location,
    const std::string &content_type,
    uint32_t expires,
    uint64_t deadline,
    char *data,
    size_t length) -> uint16_t {
    ZoneScopedN("Transmitter::send");
    ZoneText(content_location.c_str(), content_location.length());
    // spdlog::info("[TRANSMIT] Acquiring lock: send");
    std::unique_lock<LockableBase(std::mutex)> lock(_files_mutex);
    auto toi = _toi;
    _toi++;
    if (_toi == 0)
        _toi = 1;  // clamp to >= 1 in case it wraps around
    lock.unlock();

    std::shared_ptr<FileBase> file;
    try {
        file = std::make_shared<File>(
            toi,
            _fec_oti,
            content_location,
            content_type,
            expires,
            deadline,
            data,
            length,
            false, // Do not copy the data, we don't need it,
            true // Calculate the hash, the receiver will need it
            );
    } catch (const char *e) {
        spdlog::error("[TRANSMIT] Failed to create File object for file {} : {}", content_location, e);
        return -1;
    }

    // spdlog::info("[TRANSMIT] Acquiring lock: send2");
    std::lock_guard<LockableBase(std::mutex)> lock2(_files_mutex);
    // spdlog::info("[TRANSMIT] Lock acquired");

    _fdt->add(file->meta());
    // We should send the FDT if there are no files in transmission, or if the file we are adding is the first file
    auto should_send_fdt = _files.size() == 0;
    if (!should_send_fdt) {
        // If there are no uncompleted files, we should send the FDT
        should_send_fdt = true;
        for (auto &file_m : _files) {
            if (file_m.first != 0 && !file_m.second->complete()) {
                should_send_fdt = false;
                break;
            }
        };
    }

    if (should_send_fdt) {
        // There are currently no files in transmission, so we need to send the FDT with the new file
        send_fdt(false);
    } else {
        // We don't need to send the FDT because when the next file transmission is done, we will automatically an FDT
        // That FDT will contain the old file info as well as the new file info
        // By not sending the FDT now, we avoid sending the FDT too often
        spdlog::debug("[TRANSMIT] Not sending FDT, already {} files in transmission", _files.size());
    }

    _files.insert({toi, file});

    // spdlog::info("[TRANSMIT] Lock released");

    return toi;
}

auto LibFlute::Transmitter::create_empty_file_for_stream(
    uint32_t stream_id, 
    const std::string& content_type,
    uint32_t expires,
    uint64_t deadline,
    uint32_t max_source_block_length,
    uint32_t file_length) -> uint16_t {
    ZoneScopedN("Transmitter::create_empty_file_for_stream");

    // Check that stream_id is at least one
    if (stream_id == 0) {
        spdlog::error("[TRANSMIT] Stream id zero is reserved.");
        return -1;
    }

    // Check that file_length is at least one
    if (file_length == 0) {
        spdlog::error("[TRANSMIT] File length must be at least one byte");
        return -1;
    }

    // Check that max_source_block_length is at least one
    if (max_source_block_length == 0) {
        spdlog::error("[TRANSMIT] Max source block length must be at least one source symbol");
        return -1;
    }


    // spdlog::info("[TRANSMIT] Acquiring lock: create_empty_file_for_stream");
    std::unique_lock<LockableBase(std::mutex)> lock(_files_mutex);
    auto toi = _toi;
    _toi++;
    if (_toi == 0)
        _toi = 1;  // clamp to >= 1 in case it wraps around
    lock.unlock();

    // Create a copy of _fec_oti and modify it to use the given max_source_block_length
    FecOti modified_fec_oti = _fec_oti;
    modified_fec_oti.max_source_block_length = max_source_block_length;

    std::shared_ptr<FileBase> file;
    try {
        file = std::make_shared<FileStream>(
            toi,
            modified_fec_oti,
            "",
            content_type,
            expires,
            deadline,
            nullptr,
            file_length,
            false, // Do not copy the data, we don't need it,
            true // Calculate the hash, the receiver will need it
            );
            // Set the stream id
            file->meta().stream_id = stream_id;
    } catch (const char *e) {
        spdlog::error("[TRANSMIT] Failed to create FileStream object for stream {} : {}", stream_id, e);
        return -1;
    }

    // spdlog::info("[TRANSMIT] Acquiring lock: create_empty_file_for_stream2");
    std::lock_guard<LockableBase(std::mutex)> lock2(_files_mutex);
    // spdlog::info("[TRANSMIT] Lock acquired");

    _fdt->add(file->meta());
    // We should send the FDT if there are no files in transmission, or if the file we are adding is the first file
    auto should_send_fdt = _files.size() == 0;
    if (!should_send_fdt) {
        // If there are no uncompleted files, we should send the FDT
        should_send_fdt = true;
        for (auto &file_m : _files) {
            if (file_m.first != 0 && !file_m.second->complete()) {
                should_send_fdt = false;
                break;
            }
        };
    }
    should_send_fdt = true;

    if (should_send_fdt) {
        // There are currently no files in transmission, so we need to send the FDT with the new file
        send_fdt(false);
    } else {
        // We don't need to send the FDT because when the next file transmission is done, we will automatically an FDT
        // That FDT will contain the old file info as well as the new file info
        // By not sending the FDT now, we avoid sending the FDT too often
        spdlog::debug("[TRANSMIT] Not sending FDT, already {} files in transmission", _files.size());
    }

    _files.insert({toi, file});
    // spdlog::info("[TRANSMIT] Lock released");
    return toi;
}

auto LibFlute::Transmitter::fdt_send_tick() -> void {
    ZoneScopedN("Transmitter::fdt_send_tick");
    auto time_now = std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
    auto repeat_interval_ms = _fdt_repeat_interval * 1000;
    auto time_since_last_fdt_sent = time_now - _last_fdt_sent;

    // Only send the FDT if the repeat interval has passed since the last FDT was sent
    if (time_since_last_fdt_sent > repeat_interval_ms) {
        // spdlog::debug("[TRANSMIT] Time since last FDT sent: {} ms, repeat interval: {} ms", time_since_last_fdt_sent, repeat_interval_ms);
        // spdlog::info("[TRANSMIT] Acquiring lock: fdt_send_tick");
        std::unique_lock<LockableBase(std::mutex)> lock(_files_mutex);
        // Transmit if there are non-FDT files in transmission
        // We don't want to resend the FDT if it is already in transmission.
        auto should_send_fdt = _files.size() > 1 || (_files.size() == 1 && _files.begin()->first != 0);
        lock.unlock();
        if (should_send_fdt) {
            send_fdt(true);
        } else {
             // Save last time that the FDT could have been sent
            _last_fdt_sent = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    }

    // Substract the time since last FDT sent from the repeat interval
    auto ms = repeat_interval_ms > time_since_last_fdt_sent ? repeat_interval_ms - time_since_last_fdt_sent : 100;
    _fdt_timer.expires_from_now(boost::posix_time::milliseconds(ms));
    _fdt_timer.async_wait(boost::bind(&Transmitter::fdt_send_tick, this));
}

auto LibFlute::Transmitter::file_transmitted(uint32_t toi, bool should_lock) -> void {
    ZoneScopedN("Transmitter::file_transmitted");
    if (toi == 0) {
        spdlog::debug("[TRANSMIT] FDT (TOI 0) has been transmitted");
        return;
    }
    // Retransmit the FDT
    // We send the fdt agian with the transmitted toi included so a receiver that did not receive an fdt with the toi
    // can try to handle it if it has kept the transmitted ALC packets (belonging to the toi) in a buffer
    send_fdt(should_lock);

    // Now that we have send the fdt with the transmitted toi included, we can remove the file from the list
    if (should_lock) {
        // spdlog::info("[TRANSMIT] Acquiring lock: file_transmitted");
        std::unique_lock<LockableBase(std::mutex)> lock(_files_mutex);
        if (_remove_after_transmission) {
            _files.erase(toi);
        }
        _fdt->remove(toi);
        lock.unlock();
    } else {
        if (_remove_after_transmission) {
            _files.erase(toi);
        }
        _fdt->remove(toi);
    }

    // Call the completion callback.
    if (_completion_cb) {
        spdlog::debug("[TRANSMIT] Calling completion callback for TOI {}", toi);
        // If we wanted to call this in a separate jthread, we would do it like this:
        std::jthread completion_thread(_completion_cb, toi);
        // We detatch instead of join, because we don't want to wait for the completion callback to finish
        // We don't want to wait for the completion callback to finish, because this might cause a deadlock if the owner of the transmitter also uses locks.
        completion_thread.detach();
    } else {
        spdlog::info("[TRANSMIT] TOI {} has been transmitted", toi);
    }
}

std::shared_ptr<LibFlute::FileBase> LibFlute::Transmitter::get_file(uint32_t toi) {
    ZoneScopedN("Transmitter::get_file");
    // spdlog::debug("[TRANSMIT] Looking for file with TOI {}", toi);
    // spdlog::info("[TRANSMIT] Acquiring lock: get_file");
    std::lock_guard<LockableBase(std::mutex)> lock(_files_mutex);
    auto file = _files.find(toi);
    if (file != _files.end()) {
        // spdlog::debug("[TRANSMIT] Found file with TOI {}", toi);
        return file->second;
    }
    // spdlog::debug("[TRANSMIT] File with TOI {} not found", toi);
    return nullptr;
}

std::vector<uint16_t> LibFlute::Transmitter::remove_expired_files() {
    ZoneScopedN("Transmitter::remove_expired_files");
    std::vector<uint16_t> expired_tois;
    uint64_t now = seconds_since_epoch();

    // spdlog::info("[TRANSMIT] Acquiring lock: remove_expired_files");
    std::lock_guard<LockableBase(std::mutex)> lock(_files_mutex);
    for (auto it = _files.begin(); it != _files.end(); ) {
        // Ensure the second part is valid
        if (!it->second) {
            spdlog::error("Null pointer detected in _files at key: {}", it->first);
            it = _files.erase(it);
            continue; // Skip this entry
        }
        
        if (it->second->complete()){
            // Check if the files has expired
            if (it->second->meta().expires > 0 && now > it->second->meta().expires) {
                expired_tois.push_back(it->first);
                _fdt->remove(it->first);
                it = _files.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }

    return expired_tois;

}

auto LibFlute::Transmitter::send_next_packet() -> void {
    FrameMarkNamed("Transmitter::send_next_packet");
    ZoneScopedN("Transmitter::send_next_packet");
    uint32_t bytes_queued = 0;

    // spdlog::info("[TRANSMIT] Acquiring lock: send_next_packet");
    std::lock_guard<LockableBase(std::mutex)> lock(_files_mutex);
    if (_files.size()) {
        for (auto it = _files.begin(); it != _files.end();) {

            // Ensure the second part is valid
            if (!it->second) {
                spdlog::error("Null pointer detected in _files at key: {}", it->first);
                it = _files.erase(it); // Remove the invalid entry and get the next iterator
                continue; // Skip this removed entry
            }

            auto file = it->second;

            //spdlog::debug("[TRANSMIT] Files size {}, current TOI {}", _files.size(), file->meta().toi);

            // We don't need to send files that are already complete
            if (file->complete()) {
                ++it;
                continue;
            }

            // Check if the file deadline has passed
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
            if (file->meta().should_be_complete_at > 0 && now > file->meta().should_be_complete_at) {
                spdlog::info("[TRANSMIT] File {} (TOI {}) deadline has passed, forcefully marking as complete", file->meta().content_location, file->meta().toi);
                file->mark_complete();
                ++it; // We increase first, because file_transmitted might remove the file from the list, which would invalidate the iterator at the current file item
                file_transmitted(file->meta().toi, false);
                continue;
            }

            auto symbols = file->get_next_symbols(_max_payload);

            // Check if there are any symbols to send
            if (symbols.empty()) {
                ++it;
                continue;
            }

            if (file->meta().toi == 0) {
                ZoneText("FDT", 3); // the file is an FDT
            } else {
                ZoneText(file->meta().content_location.c_str(), file->meta().content_location.length());
            }
            /*
            for (const auto &symbol : symbols) {
                spdlog::trace("[TRANSMIT] Sending TOI {} SBN {} ID {}, size {}", file->meta().toi, symbol.source_block_number(), symbol.id(), symbol.len());
            }
            */
            auto packet = std::make_shared<AlcPacket>(_tsi, file->meta().toi, file->fec_oti(), symbols, _max_payload, file->fdt_instance_id());
            bytes_queued += packet->size();

            boost::asio::ip::multicast::hops mopt;
            _socket.get_option(mopt);
            spdlog::trace("[TRANSMIT] Queued ALC packet of {} bytes, containing {} symbols with TTL is {}, for TOI {}", packet->size(), symbols.size(), mopt.value(), file->meta().toi);


            // Capture the start time before calling async_send_to
            auto start_time = std::chrono::high_resolution_clock::now();

            auto selected_socket_function = [&](const boost::asio::mutable_buffer& buffer, const boost::asio::ip::udp::endpoint& remote_endpoint, std::function<void( boost::system::error_code, std::size_t)> handler) {
                if (_fake_network_socket != nullptr) {
                    _fake_network_socket->async_send_to(buffer, handler);
                } else {
                    _socket.async_send_to(buffer, remote_endpoint, handler);
                }
            };

            /*
            auto len_to_display = packet->size() < 50 ? packet->size() : 50;
            std::stringstream hex_stream;
            std::stringstream ascii_stream;
            std::stringstream binary_stream;
            hex_stream << std::hex << std::setfill('0');
            for (int i = 0; i < len_to_display; ++i) {
                auto c = packet->data()[i];
                hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
                if (i < len_to_display / 4) {
                    binary_stream << std::bitset<8>(static_cast<unsigned char>(c)); // Add binary stream
                }
                if (c >= 32 && c <= 126) {
                    ascii_stream << c;
                } else {
                    ascii_stream << '?';
                }
                if (i % 2 == 1) {
                    hex_stream << ' ';  // Add a space every two characters
                    ascii_stream << ' ' << ' ' << ' ';
                    binary_stream << ' ';
                } else if (i < len_to_display / 4) {
                    binary_stream << '.';
                }
            }

            spdlog::info("[TRANSMIT] First {} / {} bytes in hex:   {}", len_to_display, packet->size(), hex_stream.str());
            spdlog::info("[TRANSMIT] First {} / {} bytes in ascii: {}", len_to_display, packet->size(), ascii_stream.str());
            spdlog::info("[TRANSMIT] First {}  / {} bytes in bin:   {}", len_to_display / 8, packet->size(), binary_stream.str());
            */


            selected_socket_function(
                boost::asio::buffer(packet->data(), packet->size()),
                _endpoint,
                [file, symbols, packet, start_time, this]
                (const boost::system::error_code &error, std::size_t bytes_transferred) {
                    ZoneScopedN("Transmitter::send_next_packet::async_send_to");
                    // Check bytes_transferred to see if all bytes were sent
                    if (bytes_transferred != packet->size()) {
                        spdlog::error("[TRANSMIT] async_send_to: only {} of {} bytes sent", bytes_transferred, packet->size());
                    }

                    // Check for errors
                    if (error) {
                        spdlog::error("[TRANSMIT] async_send_to error: {}", error.message());
                        return;
                    }

                    // The callback is called from the io_service thread, so we need to lock the files mutex again
                    // spdlog::info("[TRANSMIT] Acquiring lock: send_next_packet::async_send_to");
                    std::unique_lock<LockableBase(std::mutex)> lock_nested(_files_mutex);
                    // spdlog::info("[TRANSMIT] Lock acquired: send_next_packet::async_send_to");

                    auto toi = file->meta().toi;
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

                    // spdlog::trace("[TRANSMIT] ALC packet of {} bytes, containing {} symbols, for TOI {} , sent in {} ns", packet->size(), symbols.size(), file->meta().toi, elapsed_time.count());
                    file->mark_completed(symbols, true);
                    auto trigger_completion_cb = file->complete();

                    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
                    metricsInstance.getOrCreateGauge("multicast_symbols_sent")->Increment(symbols.size());
                    metricsInstance.getOrCreateGauge("multicast_packets_sent")->Increment();

                    if (trigger_completion_cb) {
                        file_transmitted(toi, false);
                    }
                    
                    lock_nested.unlock();
                    //spdlog::info("[TRANSMIT] Lock released: send_next_packet::async_send_to");
                });


            break;
        }
    }
    // Capture the start time before calling async_send_to
    
    if (!bytes_queued) {
        // [IDLab] Stop the transmitter if only the FDT remains in the list of files
        if (_stop_when_done && _files.size() == 1) {
            spdlog::debug("[TRANSMIT] All files transmitted, stopping service...");
            _io_service.stop();
        }
        _send_timer.expires_from_now(boost::posix_time::milliseconds(1));
        _send_timer.async_wait(boost::bind(&Transmitter::send_next_packet, this));
    } else {
        if (_rate_limit == 0) {
            _io_service.post(boost::bind(&Transmitter::send_next_packet, this));
        } else {
            auto send_duration = static_cast<int>(ceil(((bytes_queued * 8.0) / (double)_rate_limit / 1000.0) * 1000.0 * 1000.0));
            // spdlog::trace("[TRANSMIT] Rate limiter: queued {} bytes, limit {} kbps, next send in {} us", bytes_queued, _rate_limit, send_duration);
            if (send_duration > 0) {
                _send_timer.expires_from_now(boost::posix_time::microseconds(send_duration));
                _send_timer.async_wait(boost::bind(&Transmitter::send_next_packet, this));
            } else {
                _io_service.post(boost::bind(&Transmitter::send_next_packet, this));
            }
        }
    }
    // spdlog::info("[TRANSMIT] Lock released: send_next_packet");
}

auto LibFlute::Transmitter::fdt_string() -> std::string {
    ZoneScopedN("Transmitter::fdt_string");
    if (_fdt == nullptr)
        return {};
    if (_fdt->file_count() == 0)
        return {};
    return _fdt->to_string();
}

auto LibFlute::Transmitter::clear_files() -> void {
    ZoneScopedN("Transmitter::clear_files");
    // spdlog::info("[TRANSMIT] Acquiring lock: clear_files");
    const std::lock_guard<LockableBase(std::mutex)> lock(_files_mutex);
    try {
        for (auto it = _files.begin(); it != _files.end(); ) {
            if (it->first != 0) { // All files except the FDT
                _fdt->remove(it->first); // Remove from FDT
                it = _files.erase(it); // Remove from files
            } else {
                ++it;
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("[TRANSMIT] Error clearing files: {}", e.what());
    }
}