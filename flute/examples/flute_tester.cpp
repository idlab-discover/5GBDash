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
#include <argp.h>
#include <cstdlib>
#include <ctime>
#include <sys/mman.h>

#include <boost/asio.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <libconfig.h++>
#include <string>
#include <thread>
#include <atomic>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "Component/Transmitter.h"
#include "Component/Retriever.h"
#include "Component/Receiver.h"
#include "Metric/Metrics.h"
#include "Metric/Gauge.h"
#include "Version.h"
#include "spdlog/async.h"
#include "spdlog/sinks/syslog_sink.h"
#include "spdlog/spdlog.h"

#include "public/tracy/Tracy.hpp"
#include "public/common/TracySystem.hpp"

#if defined(_MSC_VER)
    //  Microsoft 
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
    //  GCC
    #define EXPORT __attribute__((visibility("default")))
    #define IMPORT
#else
    //  do nothing and hope for the best?
    #define EXPORT
    #define IMPORT
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#ifdef VISIBILITY_ATTRIBUTE
#   define LIB_PUBLIC EXPORT
#else
#   define LIB_PUBLIC IMPORT
#endif

using libconfig::Config;
using libconfig::FileIOException;
using libconfig::ParseException;

static void print_version(FILE *stream, struct argp_state *state);
void (*argp_program_version_hook)(FILE *, struct argp_state *) = print_version;
const char *argp_program_bug_address = "Austrian Broadcasting Services <obeca@ors.at>";
static char doc[] = "FLUTE/ALC tester";  // NOLINT

static struct argp_option options[] = {  // NOLINT
    {"fec", 'f', "FEC Scheme", 0, "Choose a scheme for Forward Error Correction. Compact No Code = 0, Raptor = 1 (default is 0)", 0},
    {"mtu", 't', "BYTES", 0, "Path MTU to size ALC packets for (default: 1500)", 0},
    {"rate-limit", 'r', "KBPS", 0, "Transmit rate limit (kbps), 0 = use default, default: 1000 (1 Mbps)", 0},
    {"log-level", 'l', "LEVEL", 0,
     "Log verbosity: 0 = trace, 1 = debug, 2 = info, 3 = warn, 4 = error, 5 = "
     "critical, 6 = none. Default: 2.",
     0},
    {"loss-rate", 'o', "LOSS RATE", 0, "Set the loss rate for the fake network socket (0 - 100)", 0},
    {nullptr, 0, nullptr, 0, nullptr, 0}};

/**
 * Holds all options passed on the command line
 */
struct ft_arguments {
    const char *mcast_target = {};
    unsigned short mtu = 1500;
    uint32_t rate_limit = 1000;
    unsigned log_level = 2; /**< log level */
    unsigned fec = 0; 
    unsigned loss_rate = 0;
};

struct FsFile {
    std::string location;
    char *buffer;
    size_t len; // Length of the buffer in bytes
    uint32_t toi;
};

struct FsStream {
    uint32_t stream_id;
    std::string content_type;
    uint32_t max_source_block_length; // The number of +- MTU-sized symbols in a source block
    uint32_t file_length; // The length of the file in bytes
    std::vector<uint32_t> file_tois;
};

/**
 * Parses the command line options into the arguments struct.
 */
static auto parse_opt(int key, char *arg, struct argp_state *state) -> error_t {
    auto arguments = static_cast<struct ft_arguments *>(state->input);
    switch (key) {
        case 'f':
            arguments->fec = static_cast<unsigned>(strtoul(arg, nullptr, 10));
            if ( (arguments->fec | 1) != 1 ) {
                spdlog::error("Invalid FEC scheme ! Please pick either 0 (Compact No Code) or 1 (Raptor)");
                return ARGP_ERR_UNKNOWN;
            }
            break;
        case 't':
            arguments->mtu = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case 'r':
            arguments->rate_limit = static_cast<uint32_t>(strtoul(arg, nullptr, 10));
            break;
        case 'l':
            arguments->log_level = static_cast<unsigned>(strtoul(arg, nullptr, 10));
            break;
        case 'o':
            arguments->loss_rate = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case ARGP_KEY_NO_ARGS:
            //argp_usage(state);
            break;
        case ARGP_KEY_ARG:
            state->next = state->argc;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static char args_doc[] = "[FILE...]";  // NOLINT
static struct argp argp = {options, parse_opt, args_doc, doc,
                           nullptr, nullptr, nullptr};

/**
 * Print the program version in MAJOR.MINOR.PATCH format.
 */
void print_version(FILE *stream, struct argp_state * /*state*/) {
    fprintf(stream, "%s.%s.%s\n", std::to_string(VERSION_MAJOR).c_str(),
            std::to_string(VERSION_MINOR).c_str(),
            std::to_string(VERSION_PATCH).c_str());
}

/**
 * Check if a file exists.
 * 
 * @param the file path to check
 * @return true if the file exists, false if not
*/
auto file_exists(const std::string& name) -> bool {
    ZoneScopedN("file_exists");
    struct stat buffer;   
    return (stat (name.c_str(), &buffer) == 0); 
}

struct Data {
    std::string file;
    uint64_t toi;
    unsigned fec;
    std::map<uint32_t,std::vector<uint32_t>> missing;
    bool valid;
};

/**
 * Proof of concept parser for converting incoming string to the required data object.
 * A proper json library or another input format would be better.
*/
auto convert(const std::string& json_string) -> Data {
    ZoneScopedN("convert");
    Data data{"", 0, 0, {}, false};
    try {
        if (json_string.empty()) {
            spdlog::error("Empty JSON string");
            return data;
        }

        std::stringstream ss;
        ss << json_string;

        boost::property_tree::ptree pt;
        boost::property_tree::read_json(ss, pt);

        if (pt.find("toi") != pt.not_found()) {
            data.toi = std::stoi(pt.get<std::string>("toi"));
        }
        if (pt.find("file") != pt.not_found()) {
            data.file = pt.get<std::string>("file");
        } else if (data.toi == 0) {
            data.file = "last.fdt";
        }
        if (pt.find("fec") != pt.not_found()) {
            data.fec = std::stoi(pt.get<std::string>("fec"));
        }

        // Iterate over all the blocks.
        if (pt.find("missing") != pt.not_found()) {
            boost::property_tree::ptree missing_pt = pt.get_child("missing");
            for (boost::property_tree::ptree::iterator block = missing_pt.begin(); block != missing_pt.end(); ++block) {
                // Iterate over all the missing symbols of the current block.
                std::vector<uint32_t> symbols;
                for (boost::property_tree::ptree::iterator symbol = block->second.begin(); symbol != block->second.end(); ++symbol) {
                    symbols.push_back(std::stoi(symbol->second.data()));
                }
                data.missing[std::stoi(block->first)] = symbols;
            }
        }

        data.valid = true;

    } catch (const std::exception& e) {
        spdlog::error("Error parsing JSON: {}", e.what());
        spdlog::error("String was {}", json_string);
    }

    return data;

}

class FluteTransmissionManager {
public:
    static auto getInstance() -> FluteTransmissionManager& {
        ZoneScopedN("FluteTransmissionManager::getInstance");
        static FluteTransmissionManager instance; // Guaranteed to be destroyed and instantiated on first use
        return instance;
    }

    FluteTransmissionManager(const FluteTransmissionManager&) = delete; // Delete copy constructor
    auto operator=(const FluteTransmissionManager&) -> FluteTransmissionManager& = delete; // Delete assignment operator

    ~FluteTransmissionManager() {
        ZoneScopedN("FluteTransmissionManager::~FluteTransmissionManager");
        clear_files();
    }

    auto setup(struct ft_arguments arguments) -> void {
        ZoneScopedN("FluteTransmissionManager::setup");
        // Lock the mutex
        std::unique_lock<LockableBase(std::mutex)> lock(transmitter_mutex);
        // Unique lock
  

        // Print the rate limit
        spdlog::info("[TRANSMIT] Rate limit is {} kbps", arguments.rate_limit);  

        // Construct the transmitter class
        transmitter = std::make_unique<LibFlute::Transmitter>(
            "238.1.1.95",
            (short) 40085,
            16,
            arguments.mtu,
            arguments.rate_limit,
            LibFlute::FecScheme(arguments.fec),
            io,
            1,1);

        transmitter->set_remove_after_transmission(false);

        // Register a completion callback
        transmitter->register_completion_callback(
            [this](uint32_t toi) {
                if (toi == 0) {
                    return;
                }
                metricsInstance.getOrCreateGauge("multicast_files_sent")->Increment();
                // Lock the mutex
                std::unique_lock<LockableBase(std::mutex)> completed_lock(transmitter_mutex);
                for (auto &file : files) {
                    if (file.toi == toi) {
                        spdlog::info("[TRANSMIT] {} (TOI {}) has been transmitted", file.location, file.toi);
                    }
                }
                completed_lock.unlock();
            });

        exact_start_time = std::chrono::system_clock::now();
        // Unlock the mutex, so that we can add files
        lock.unlock();
    }

    auto start() -> void {
        ZoneScopedN("FluteTransmissionManager::start");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        // Check if the thread is already running
        if (io_thread_running.load()) {
            spdlog::warn("[TRANSMIT] IO thread is already running. Cannot start again.");
            return;
        }


        io_thread_running.store(true);  // Set the flag to indicate that the thread is running

        // Start a new thread for io_service
        std::jthread ioThread([this]() {
            ZoneScopedN("FluteTransmissionManager::ioThread");
            // spdlog::info("[TRANSMIT] Transmission IO thread started");
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "IO thread: transmitter");
            // Create a work guard to keep the io_service running
            auto work_guard = boost::asio::make_work_guard(io);
            // Run the io_service
            io.run();
            io_thread_running.store(false);  // Set the flag to indicate that the thread has stopped
            // spdlog::info("[TRANSMIT] Transmission IO thread stopped");
        });

        // Detach the thread so it can run independently
        ioThread.detach();

        // A thread that calles remove_expired_files() every 1 second
        std::jthread remove_expired_files_thread([this]() {
            ZoneScopedN("FluteTransmissionManager::remove_expired_files_thread");
            metricsInstance.addThread(std::this_thread::get_id(), "remove_expired_files_thread");
            // spdlog::info("[TRANSMIT] remove_expired_files_thread started");
            while (io_thread_running.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                // Lock the mutex
                std::unique_lock<LockableBase(std::mutex)> remover_lock(transmitter_mutex);
                auto removed_file_tois = transmitter->remove_expired_files();
                // Iterate over this vector
                for (auto &toi : removed_file_tois) {
                    for (auto &file : files) {
                        if (file.toi == toi) {
                            // free() the buffer here
                            // munmap(file.buffer,file.len);
                            TracyFree(file.buffer);
                            free(file.buffer);
                            set_removed(file.toi, false);
                            spdlog::debug("[TRANSMIT] {} (TOI {}) has been removed from the queue (expired)", file.location, file.toi);
                        }
                    }
                    // Remove the file from the vector
                    files.erase(std::remove_if(files.begin(), files.end(),
                        [toi](const FsFile &file) {
                            return file.toi == toi;
                        }),
                        files.end());
                }
                remover_lock.unlock();
            }
        });


        // Detach the thread so it can run independently
        remove_expired_files_thread.detach();
    }

    auto stop() -> void {
        ZoneScopedN("FluteTransmissionManager::stop");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        // Stop the io_service
        io.stop();

        // Wait for the IO thread to finish (if not detached)
        // This is a simple example; you might need more sophisticated synchronization
        while (io_thread_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        spdlog::debug("[TRANSMIT] All files have been sent. Exiting...");
        auto exact_end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(exact_end_time - exact_start_time).count();
        auto transmission_time_gauge = metricsInstance.getOrCreateGauge("transmission_time_gauge"); 
        transmission_time_gauge->Set(static_cast<double>(duration));

        /*
        spdlog::info("[TRANSMIT] FLUTE transmitter demo shutting down");
        */

        auto next_instance_id = (transmitter->current_instance_id() + 1) & ((1 << 20) - 1);
        std::cout << "next_instance_id = " << next_instance_id << std::endl;
    }

    auto get_real_location(std::string file_location) -> std::string {
        ZoneScopedN("FluteTransmissionManager::get_real_location");

        // Check if the length of the file_location is 0
        if (file_location.length() < 1) {
            spdlog::error("[TRANSMIT] File location is empty");
            return {""};
        }

        // Check if the file exists
        if (file_exists(file_location)) {
            return file_location;
        }

        // [IDLab] The following is purely for our use case.
        // It is used to be able to send 'virtual' files.

        // Copy the string, because we're possibly going to modify it
        std::string location(file_location);

        // Split the location into directories
        // Location will then only contain the filename
        size_t pos = 0;
        std::string token;
        std::vector<std::string> directories;
        while ((pos = location.find('/')) != std::string::npos) {
            token = location.substr(0, pos);
            directories.push_back(token);
            location.erase(0, pos + 1);
        }

        if (directories.size() <= 1) {
            spdlog::info("[TRANSMIT] {} does not exists", location);
            return {""};
        }

    
        // If the second directory contains an '_', then take the part before the first '_'.
        std::string second_directory = directories[1];
        if (second_directory.find('_') == std::string::npos) {
            spdlog::info("[TRANSMIT] {} does not exists", location);
            return {""};
        }

        std::string second_directory_before_underscore = second_directory.substr(0, second_directory.find('_'));
        // Replace the second directory with the new one in the directories vector.
        directories[1] = second_directory_before_underscore;
        // Join tall elements in the directories vector to create the new location.
        // Join the strings using a loop
        std::string new_path;
        for (const std::string& s : directories) {
            new_path += s + "/";
        }
        // Save the path as the new location
        location = new_path.append(location);

        // Check if the new location exists
        if (file_exists(location)) {
            return location;
        }
        
        spdlog::info("[TRANSMIT] {} does not exists", location);
        return {""};
    }

    auto send_file(std::string file_location, u_int64_t deadline, std::string content_type = "application/octet-stream") -> int{
        ZoneScopedN("FluteTransmissionManager::send_file");
        try {
            // Get the real location
            std::string real_location = get_real_location(file_location);

            // Read the file into a buffer
            std::ifstream file(real_location, std::ios::binary | std::ios::ate);
            std::streamsize size = file.tellg(); // Size is in bytes
            file.seekg(0, std::ios::beg);

            char *buffer = (char *)malloc(size);
            TracyAlloc(buffer, size);
            file.read(buffer, size);
            // Use the original location, not the location variable, because this might be modified above
            FsFile fs_file{file_location, buffer, (size_t)size};

            /*
            // Print the first 40 bytes of the file in hex
            std::stringstream hex_stream;
            hex_stream << std::hex << std::setfill('0');
            for (int i = 0; i < 40; ++i) {
                hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(buffer[i]));
                if (i % 2 == 1) {
                    hex_stream << ' ';  // Add a space every two characters
                }
            }
            spdlog::info("[TRANSMIT] First 40 bytes of {}: {}", file_location, hex_stream.str());
            */

            // Expire time in seconds
            // 2 seconds more then the deadline, if no deadline is set, then 10 seconds
            u_int64_t expire_time_s = deadline == 0 ? transmitter->seconds_since_epoch() + 10 : (deadline / 1000) + 2;

            fs_file.toi = transmitter->send(fs_file.location,
                                        content_type,
                                        expire_time_s,
                                        deadline,
                                        fs_file.buffer,
                                        fs_file.len);
            spdlog::info("[TRANSMIT] Queued {} ({} bytes) for transmission, TOI is {}",
                        fs_file.location, fs_file.len, fs_file.toi);

            // Lock the mutex
            // spdlog::info("[TRANSMIT] Locking mutex");
            std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);
            // spdlog::info("[TRANSMIT] Mutex locked");

            files.push_back(fs_file);

            // spdlog::info("[TRANSMIT] Done");

            return fs_file.toi;
            

        } catch (const std::exception &ex) {
            std::cout << "Caught exception \"" << ex.what() << "\"\n";
            spdlog::error("[TRANSMIT] Exiting on unhandled exception: {}", ex.what());

            return -1;
        } catch (const char* errorMessage) {
            std::cout << "Caught exception \"" << errorMessage << "\"\n";
            spdlog::error("[TRANSMIT] Exiting on unhandled error: {}", errorMessage);

            return -1;
        } catch (...) {
            spdlog::error("[TRANSMIT] Exiting on unhandled exception");

            return -1;
        }

        return -1;
    }

    auto send_files(std::vector<std::string> &file_locations, u_int64_t deadline, std::string content_type = "application/octet-stream") -> int {
        ZoneScopedN("FluteTransmissionManager::send_files");
        // We can't lock the mutex here, because send_file() locks it itself
        int result = 0;
        // Call send_file for each file
        for (const std::string& file_location : file_locations) {
            if (send_file(file_location, deadline, content_type) > 0) {
                result++;
            }
        }

        return result;
    }

    auto send_to_stream(uint32_t stream_id, std::string content) -> int {
        ZoneScopedN("FluteTransmissionManager::send_to_stream");
        try {
            // Lock the mutex
            std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);
            // Check if the stream exists
            if (!streams.contains(stream_id)) {
                spdlog::error("[TRANSMIT] Stream {} does not exist", stream_id);
                return -1;
            }

            spdlog::debug("[TRANSMIT] Sending content to stream {}", stream_id);

            // Get the stream
            FsStream& stream = streams.at(stream_id);
            // spdlog::debug("[TRANSMIT] Stream {} has {} files", stream_id, stream.file_tois.size());

            // Append content with START \r\n size \r\n in the front and \r\n in the back
            content = "START\r\n" + std::to_string(content.length()) + "\r\n" + content + "\r\n";

            // Get the greatest toi from the files vector
            uint32_t greatest_toi = 0;
            for (auto &toi : stream.file_tois) {
                if (toi > greatest_toi) {
                    greatest_toi = toi;
                }
            }

            std::size_t pushed_length = 0;

            if (greatest_toi > 0) {
                // Get the file from the transmitter
                auto file = transmitter->get_file(greatest_toi);
                // Check if the file is not a nullptr
                if (file != nullptr && file->meta().toi == greatest_toi) {
                    // Cast file (FileBase) to FileStream
                    auto file_stream = std::dynamic_pointer_cast<LibFlute::FileStream>(file);

                    // Push the content to the file
                    //spdlog::debug("[TRANSMIT] Pushing content to file with TOI {}", greatest_toi);
                    pushed_length = file_stream->push_to_file(content);
                    // spdlog::debug("[TRANSMIT] Pushed {} bytes to file with TOI {}", pushed_length, greatest_toi);
                } else {
                    spdlog::trace("[TRANSMIT] File with TOI {} does not exist for stream {}", greatest_toi, stream_id);
                }
            }

            // While the pushed length is less than the content length
            while (pushed_length < content.length())
            {
                auto new_toi = transmitter->create_empty_file_for_stream(stream.stream_id,
                                            stream.content_type,
                                            0, // Currently no expiry
                                            0, // Currently no deadline
                                            stream.max_source_block_length,
                                            stream.file_length);

                if (new_toi <= 0) {
                    spdlog::error("[TRANSMIT] Failed to create new file for stream {}", stream.stream_id);
                    return -1;
                }

                //spdlog::debug("[TRANSMIT] Created new file with TOI {} for stream {}", new_toi, stream.stream_id);

                FsFile fs_file{.location = "", .buffer = nullptr, .len = (size_t)stream.file_length, .toi = new_toi};
                files.push_back(fs_file);
                stream.file_tois.push_back(new_toi);

                // Get the file from the transmitter
                auto file = transmitter->get_file(new_toi);
                // Check if the file is nullptr
                if (file == nullptr || file->meta().toi != new_toi) {
                    spdlog::error("[TRANSMIT] File with TOI {} does not exist", new_toi);
                    return -1;
                }
                // Cast file (FileBase) to FileStream
                auto file_stream = std::dynamic_pointer_cast<LibFlute::FileStream>(file);

                // Push the content to the file, starting from the pushed length
                //spdlog::debug("[TRANSMIT] Pushing content to file with TOI {}", new_toi);
                pushed_length += file_stream->push_to_file(content.substr(pushed_length));
                spdlog::info("[TRANSMIT] Pushed content to file with TOI {}", new_toi);
            }

            // spdlog::debug("[TRANSMIT] Pushed content to stream {}", stream_id);

            return pushed_length;
            

        } catch (const std::exception &ex) {
            std::cout << "Caught exception \"" << ex.what() << "\"\n";
            spdlog::error("[TRANSMIT] Exiting on unhandled exception: {}", ex.what());
            return -1;
        } catch (const char* errorMessage) {
            std::cout << "Caught exception \"" << errorMessage << "\"\n";
            spdlog::error("[TRANSMIT] Exiting on unhandled error: {}", errorMessage);

            return -1;
        } catch (...) {
            spdlog::error("[TRANSMIT] Exiting on unhandled exception");
            return -1;
        }

        return -1;
    }

    auto add_stream(uint32_t stream_id, std::string content_type, uint32_t max_source_block_length, uint32_t file_length) -> bool {
        ZoneScopedN("FluteTransmissionManager::add_stream");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        // Check if the stream already exists in the map
        if (streams.contains(stream_id)) {
            spdlog::error("[TRANSMIT] Stream {} already exists", stream_id);
            return false;
        }
        streams.emplace(stream_id, FsStream{stream_id, content_type, max_source_block_length, file_length});

        spdlog::info("[TRANSMIT] Stream {} added", stream_id);
        return true;
    }

    auto clear_files() -> int {
        ZoneScopedN("FluteTransmissionManager::clear_files");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        if (files.empty()) {
            return 0;
        }

        // Prevent the transmitter from sending any more files
        transmitter->clear_files();


        auto items_to_remove = files.size();

        for (auto &file : files) {
            // free() the buffer here
            TracyFree(file.buffer);
            //munmap(file.buffer,file.len);
            free(file.buffer);
            set_removed(file.toi, false);
            spdlog::debug("[TRANSMIT] {} (TOI {}) has been removed from the queue (cleared)",
                            file.location, file.toi);
        }


        // Clear the files vector
        files.clear();
        // Convert to int and return
        return static_cast<int>(items_to_remove);
    }

    auto set_rate_limit(uint32_t rate_limit) -> int {
        ZoneScopedN("FluteTransmissionManager::set_rate_limit");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        transmitter->set_rate_limit(rate_limit);

        return 0;
    }

    auto current_total_file_size() -> uint64_t {
        ZoneScopedN("FluteTransmissionManager::current_total_file_size");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        uint64_t total_file_size = 0;
        for (auto &file : files) {
            total_file_size += file.len;
        }
        return total_file_size;
    }

    auto set_thread_name(std::string thread_name) -> void {
        ZoneScopedN("FluteTransmissionManager::set_thread_name");
        metricsInstance.addThread(std::this_thread::get_id(), thread_name);
    }

    auto retrieve(const std::string& json_string, uint16_t mtu) -> std::string {
        try {
            Data data = convert(json_string);

            if (data.valid == false) {
                spdlog::error("Invalid JSON string");
                return {};
            }

            spdlog::info("[RETRIEVE] Partial request received for {} (TOI {})", data.file, data.toi);

            // Lock the mutex
            std::unique_lock<LockableBase(std::mutex)> remover_lock(transmitter_mutex);

            if (data.toi == 0 && data.missing.size() == 0) {
                auto retrieved_from_memory = transmitter->fdt_string();
                if (!retrieved_from_memory.empty()) {
                    // Unlock the mutex
                    remover_lock.unlock();
                    return retrieved_from_memory + "\r\n\r\n";
                }
            }

            // Construct the retriever class.
            LibFlute::Retriever retriever(16, mtu, LibFlute::FecScheme(data.fec));
            // We check if the file has already been removed here to prevent locking the transmitter temporarily.
            if (false && data.toi != 0 && !has_removed(data.toi, false)) {
                // Check is the transmitter has the file
                auto parsed_file = transmitter->get_file(data.toi);
                // If not nullptr, then
                if (parsed_file != nullptr && parsed_file->fec_oti().encoding_id == retriever.get_fec_scheme()) {
                    spdlog::info("[RETRIEVE] Retrieving file {} from memory", parsed_file->meta().content_location);
                    // Retrieve the file from memory
                    auto retrieved_from_memory = retriever.get_alcs_from_file(parsed_file, data.missing);

                    // Unlock the mutex
                    remover_lock.unlock();

                    if (retrieved_from_memory.empty()) {
                        spdlog::error("[RETRIEVE] Failed to retrieve file {} from memory", parsed_file->meta().content_location);
                        return {};
                    }

                    return retrieved_from_memory;
                }
            }

            // Unlock the mutex
            remover_lock.unlock();

            // Get the real location
            std::string real_location = get_real_location(data.file);

            spdlog::info("[RETRIEVE] Retrieving file {} from storage", real_location);
            // Read the file contents into the buffers.
            std::ifstream file(real_location, std::ios::binary | std::ios::ate);
            std::streamsize size = file.tellg();

            if (size == -1) {
                spdlog::error("[RETRIEVE] File {} not found", real_location);
                return {};
            } else if (size == 0) {
                spdlog::error("[RETRIEVE] File {} is empty", real_location);
                return {};
            }
            
            file.seekg(0, std::ios::beg);

            char *buffer = (char *)malloc(size);
            TracyAlloc(buffer, size);
            file.read(buffer, size);

            if (data.toi == 0 && data.missing.size() == 0) {
                // Create a string copy of the buffer
                std::string retrieved(buffer, size);
                // Free the buffer
                TracyFree(buffer);
                free(buffer);
                return retrieved + "\r\n\r\n";
            }

            // Retrieve the file from new buffer
            auto retrieved = retriever.get_alcs(data.file, // We use the original filename here
                            "application/octet-stream",
                            retriever.seconds_since_epoch() + 60,  // 1 minute from now
                            buffer,
                            (size_t)size,
                            data.toi,
                            data.missing);

            // Free the buffer
            TracyFree(buffer);
            free(buffer);

            return retrieved;

        } catch (const std::exception &ex) {
            spdlog::error("[RETRIEVE] Exiting on unhandled exception: {}", ex.what());
        } catch (const char* errorMessage) {
            std::cout << "Caught exception \"" << errorMessage << "\"\n";
            spdlog::error("[RETRIEVE] Exiting on unhandled error: {}", errorMessage);
        } catch (...) {
            spdlog::error("[RETRIEVE] Exiting on unhandled exception");
        }

        return {};

    }

    auto get_file(uint32_t toi) -> FsFile {
        ZoneScopedN("FluteTransmissionManager::get_file");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        for (auto &file : files) {
            if (file.toi == toi) {
                // Create a deep copy of the file
                return file;
            }
        }

        spdlog::error("[SENDER] File with TOI {} not found", toi);
        return FsFile{"", nullptr, 0, 0};
    }

    auto get_file_size(uint32_t toi) -> size_t {
        ZoneScopedN("FluteTransmissionManager::get_file_size");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        for (auto &file : files) {
            if (file.toi == toi) {
                return file.len;
            }
        }

        return 0;
    }

    auto set_network_socket(std::shared_ptr<LibFlute::FakeNetworkSocket> &network_socket) -> void {
        ZoneScopedN("FluteTransmissionManager::set_network_socket");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        transmitter->set_fake_network_socket(network_socket);
    }

    boost::asio::io_service io;

    auto set_removed(uint32_t toi, bool should_lock = true) -> void {
        ZoneScopedN("FluteTransmissionManager::set_removed");
        if (should_lock) {
            // Lock the mutex
            std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);
            // Check if the TOI is already in the vector
            if (std::find(removed_tois.begin(), removed_tois.end(), toi) != removed_tois.end()) {
                return;
            }
            // Add the TOI to the vector
            removed_tois.push_back(toi);
        }

        // Check if the TOI is already in the vector
        if (std::find(removed_tois.begin(), removed_tois.end(), toi) != removed_tois.end()) {
            return;
        }
        // Add the TOI to the vector
        removed_tois.push_back(toi);
    }

    auto has_removed(uint32_t toi, bool should_lock = true) -> bool {
        // Lock the mutex
        if (should_lock) {
            std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);
            return std::find(removed_tois.begin(), removed_tois.end(), toi) != removed_tois.end();
        }

        return std::find(removed_tois.begin(), removed_tois.end(), toi) != removed_tois.end();
    }
private:
    // We're responsible for buffer management, so create a vector of structs that
    // are going to hold the data buffers
    std::vector<FsFile> files;
    std::map<uint32_t, FsStream> streams;
    std::chrono::time_point<std::chrono::system_clock> exact_start_time;
    LibFlute::Metric::Metrics& metricsInstance;
    std::unique_ptr<LibFlute::Transmitter> transmitter;
    std::atomic<bool> io_thread_running{false};  // Flag to track the running status of the thread
    // A mutex to prevent the transmitter from being accessed from multiple threads concurrently
    TracyLockable(std::mutex, transmitter_mutex);
    std::vector<uint32_t> removed_tois;

    FluteTransmissionManager(): metricsInstance(LibFlute::Metric::Metrics::getInstance()) {
        spdlog::info("FLUTE transmitter manager has loaded");  
        auto alc_percentage_retrieved = metricsInstance.getOrCreateGauge("alc_percentage_retrieved");
    }
};


class FluteReceptionManager {
public:
    static auto getInstance() -> FluteReceptionManager& {
        ZoneScopedN("FluteReceptionManager::getInstance");
        static FluteReceptionManager instance; // Guaranteed to be destroyed and instantiated on first use
        return instance;
    }

    FluteReceptionManager(const FluteReceptionManager&) = delete; // Delete copy constructor
    auto operator=(const FluteReceptionManager&) -> FluteReceptionManager& = delete; // Delete assignment operator

    ~FluteReceptionManager() {
        ZoneScopedN("FluteReceptionManager::~FluteReceptionManager");
    }

    auto setup(std::shared_ptr<LibFlute::FakeNetworkSocket> network_socket = nullptr) -> void {
        ZoneScopedN("FluteReceptionManager::setup");
        // Lock the mutex
        std::unique_lock<LockableBase(std::mutex)> lock(receiver_mutex);
        // Unique lock

        receiver = std::make_unique<LibFlute::Receiver>(
            "0.0.0.0",
            "239.0.0.1",
            "fake_network_socket",
            (short) 40085,
            16,
            io,
            network_socket);

        
        multicast_files_received_gauge = metricsInstance.getOrCreateGauge("multicast_files_received");
        multicast_reception_time = metricsInstance.getOrCreateGauge("multicast_reception_time");
        multicast_reception_time_before_deadline = metricsInstance.getOrCreateGauge("multicast_reception_time_before_deadline");
        multicast_reception_time_after_deadline = metricsInstance.getOrCreateGauge("multicast_reception_time_after_deadline");

        receiver->register_completion_callback(
            [&](std::shared_ptr<LibFlute::FileBase> file) {  // NOLINT
                time_t current_time = time(nullptr);
                // Get the relative file path;
                std::string path = file->meta().content_location.c_str();
                
                FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
                auto fsFile = fluteTransmissionManager.get_file(file->meta().toi);
                // Check if the toi is 0
                if (fsFile.toi == 0) {
                    spdlog::error("[RECEIVE] TOI {}: not found", file->meta().toi);
                    set_received(file->meta().toi);
                    return;
                }
                // Compare the file sizes
                if (fsFile.len != file->meta().content_length) {
                    spdlog::error("[RECEIVE] TOI {}: File sizes do not match. Expected {}, got {}", fsFile.toi, fsFile.len, file->meta().content_length);
                    set_received(file->meta().toi);
                    return;
                }
                // Compare the file buffers
                if (memcmp(fsFile.buffer, file->buffer(), fsFile.len) != 0) {
                    spdlog::error("[RECEIVE] TOI {}: File buffers do not match", fsFile.toi);
                    set_received(file->meta().toi);
                    return;
                }


                spdlog::info("[RECEIVE] {} (TOI {}) has been received",
                             file->meta().content_location, file->meta().toi);              

                multicast_files_received_gauge->Increment();
                multicast_reception_time->Set(static_cast<double>(current_time - file->received_at()));

                multicast_reception_time_before_deadline->Set(static_cast<double>(file->time_before_deadline()));
                multicast_reception_time_after_deadline->Set(static_cast<double>(file->time_after_deadline()));

                set_received(file->meta().toi);

            });

        
        receiver->register_removal_callback(
            [&](std::shared_ptr<LibFlute::FileBase> file) {
                spdlog::info("[RECEIVE] TOI {} has been removed", file->meta().toi);
                // Force add the TOI to the received list, this will stop awaiting for the TOI if it hasn't arrived.
                set_received(file->meta().toi);
            });

        receiver->register_emit_message_callback(
            [&](uint32_t stream_id, std::string message) {
                spdlog::info("[RECEIVE] STREAM {}: We have received the message: {}", stream_id, message);
            });

        lock.unlock();
    }

    auto start() -> void {
        ZoneScopedN("FluteReceptionManager::start");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(receiver_mutex);

        // Check if the thread is already running
        if (io_thread_running.load()) {
            spdlog::warn("[RECEIVE] IO thread is already running. Cannot start again.");
            return;
        }


        io_thread_running.store(true);  // Set the flag to indicate that the thread is running

        // Start a new thread for io_service
        std::jthread ioThread([this]() {
            ZoneScopedN("FluteReceptionManager::ioThread1");
            spdlog::info("[RECEIVE] Reception IO thread 1 started");
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "IO thread 1: receiver");
            while (io_thread_running.load()) {
                try {
                    // Create a work guard to keep the io_service running
                    auto work_guard = boost::asio::make_work_guard(io);
                    io.run();
                    io_thread_running.store(false);  // Set the flag to indicate that the thread has stopped
                    // spdlog::info("[RECEIVE] Reception IO thread 1 stopped");
                } catch (const std::exception &ex) {
                    spdlog::error("[RECEIVE] Exception in reception IO thread: {}", ex.what());
                    io.reset();
                } catch (...) {
                    spdlog::error("[RECEIVE] Unknown exception in reception IO thread");
                    io.reset();
                }
            }
        });

        // Detach the thread so it can run independently
        ioThread.detach();

/*
        // Start a new thread for io_service
        std::jthread ioThread2([this]() {
            ZoneScopedN("FluteReceptionManager::ioThread2");
            spdlog::info("[RECEIVE] Reception IO thread 2 started");
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "IO thread 2: receiver");
            // Run the io_service
            io.run();
            io_thread_running.store(false);  // Set the flag to indicate that the thread has stopped
            spdlog::info("[RECEIVE] Reception IO thread 2 stopped");
        });

        // Detach the thread so it can run independently
        ioThread2.detach();
*/
        // A thread that calles resolve_fdt_for_buffered_alcs() every 1 second
        std::jthread fetch_missing_fdt_thread([this]() {
            ZoneScopedN("FluteReceptionManager::fetch_missing_fdt_thread");
            metricsInstance.addThread(std::this_thread::get_id(), "fetch_missing_fdt_thread");
            spdlog::info("[RECEIVE] fetch_missing_fdt_thread started");
            while (io_thread_running.load()) {
                receiver->resolve_fdt_for_buffered_alcs();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        });


        // Detach the thread so it can run independently
        fetch_missing_fdt_thread.detach();

        std::jthread fetch_missing_alc_thread([this]() {
            ZoneScopedN("FluteReceptionManager::fetch_missing_alc_thread");
            metricsInstance.addThread(std::this_thread::get_id(), "fetchMissingAlcThread");
            while (io_thread_running.load()) {
                ZoneScopedN("FluteReceptionManager::fetch_missing_alc_thread::while_loop");
                auto files = receiver->file_list();
                uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();

                for (const auto &file : files) {
                    if (file->meta().should_be_complete_at > 0 && now > file->meta().should_be_complete_at) {
                        file->meta().should_be_complete_at = 0; // Don't try to retrieve the missing parts again
                        if (!file->complete()) {
                            file->retrieve_missing_parts();
                        } else {
                             auto alc_percentage_to_retrieve = metricsInstance.getOrCreateGauge("alc_percentage_to_retrieve");
                             alc_percentage_to_retrieve->Set(0); // We are not missing anything
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        // Detach the thread so it can run independently
        fetch_missing_alc_thread.detach();

        std::jthread handle_ALC_buffer_thread([this]() {
            ZoneScopedN("FluteReceptionManager::handle_ALC_buffer_thread");
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "handleALCBufferThread");
            while (io_thread_running.load()) {
                // If the buffer is not empty, handle the first element
                if (receiver->handle_alc_buffer()) {
                    // Wait for 1 nanosecond if the buffer is not empty
                    std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                } else {
                    // Wait for 1 microsecond if the buffer is empty
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            // spdlog::info("[RECEIVE] handleALCBufferThread stopped");
        });

        // Detach the thread so it can run independently
        handle_ALC_buffer_thread.detach();
    }

    auto stop() -> void {
        ZoneScopedN("FluteReceptionManager::stop");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(receiver_mutex);

        // Stop the io_service
        io.stop();

        // Wait for the IO thread to finish (if not detached)
        // This is a simple example; you might need more sophisticated synchronization
        while (io_thread_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    auto set_received(uint32_t toi) -> void{
        ZoneScopedN("FluteReceptionManager::set_received");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(receiver_mutex);
        // Check if the TOI is already in the vector
        if (std::find(received_tois.begin(), received_tois.end(), toi) != received_tois.end()) {
            return;
        }
        // Add the TOI to the vector
        received_tois.push_back(toi);
    }

    auto has_received(uint32_t toi) -> bool {
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(receiver_mutex);
        return std::find(received_tois.begin(), received_tois.end(), toi) != received_tois.end();
    }

    boost::asio::io_service io;
private:

    LibFlute::Metric::Metrics& metricsInstance;
    std::shared_ptr<LibFlute::Metric::Gauge> multicast_files_received_gauge;
    std::shared_ptr<LibFlute::Metric::Gauge> multicast_reception_time;
    std::shared_ptr<LibFlute::Metric::Gauge> multicast_reception_time_before_deadline;
    std::shared_ptr<LibFlute::Metric::Gauge> multicast_reception_time_after_deadline;
    std::unique_ptr<LibFlute::Receiver> receiver;
    std::atomic<bool> io_thread_running{false};  // Flag to track the running status of the thread
    // A mutex to prevent the transmitter from being accessed from multiple threads concurrently
    TracyLockable(std::mutex, receiver_mutex);
    std::vector<uint32_t> received_tois;

    FluteReceptionManager(): metricsInstance(LibFlute::Metric::Metrics::getInstance()) {
        spdlog::info("FLUTE reception manager has loaded"); 
    }
};


class StorageManager {
public:
    static auto getInstance() -> StorageManager& {
        ZoneScopedN("StorageManager::getInstance");
        static StorageManager instance; // Guaranteed to be destroyed and instantiated on first use
        return instance;
    }

    StorageManager(const StorageManager&) = delete; // Delete copy constructor
    auto operator=(const StorageManager&) -> StorageManager& = delete; // Delete assignment operator

    ~StorageManager() {
        ZoneScopedN("StorageManager::~StorageManager");
    }

    void setup(int argc, char **argv) {
        ZoneScopedN("StorageManager::setup");
        // Lock the mutex
        std::unique_lock<LockableBase(std::mutex)> lock(storage_mutex);
        // Unique lock

        /* Default values */
        _arguments.mtu = 1500;
        /* Load arguments from program input */
        argp_parse(&argp, argc, argv, 0, nullptr, &_arguments);
        spdlog::set_level(static_cast<spdlog::level::level_enum>(_arguments.log_level));

        lock.unlock();
    }

    struct ft_arguments get_arguments() {
        ZoneScopedN("StorageManager::get_arguments");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(storage_mutex);
        return _arguments;
    }

    auto create_network_socket(size_t sender_capacity, size_t network_capacity, size_t receiver_capacity, boost::asio::io_service& sender_io_service, boost::asio::io_service& receiver_io_service) -> std::shared_ptr<LibFlute::FakeNetworkSocket> {
        ZoneScopedN("StorageManager::create_network_socket");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(storage_mutex);
        _network_socket = std::make_shared<LibFlute::FakeNetworkSocket>(sender_capacity, network_capacity, receiver_capacity, sender_io_service, receiver_io_service);
        return _network_socket;
    }

    void set_latest_bandwidth(unsigned long bandwidth) {
        ZoneScopedN("StorageManager::set_latest_bandwidth");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(storage_mutex);
        latest_bandwidth = bandwidth;
    }

    auto get_latest_bandwidth() -> unsigned long {
        ZoneScopedN("StorageManager::get_latest_bandwidth");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(storage_mutex);
        return latest_bandwidth;
    }

private:
    // A mutex to prevent the transmitter from being accessed from multiple threads concurrently
    TracyLockable(std::mutex, storage_mutex);
    
    struct ft_arguments _arguments;
    std::shared_ptr<LibFlute::FakeNetworkSocket> _network_socket;
    unsigned long latest_bandwidth = 0;

    StorageManager() {
        // Set up logging
        spdlog::set_pattern("[%H:%M:%S.%f][thr %t][%^%l%$] %v");

        spdlog::info("Storage manager has loaded"); 
    }
};

extern "C" LIB_PUBLIC void start() {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.start();
    FluteReceptionManager& fluteReceptionManager = FluteReceptionManager::getInstance();
    fluteReceptionManager.start();
}

extern "C" LIB_PUBLIC void stop() {
    spdlog::info("Stopping FLUTE");
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.stop();
    FluteReceptionManager& fluteReceptionManager = FluteReceptionManager::getInstance();
    fluteReceptionManager.stop();
}

extern "C" LIB_PUBLIC void set_thread_name(const char* thread_name) {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.set_thread_name(std::move(std::string(thread_name)));
}

extern "C" LIB_PUBLIC auto latest_bandwidth() -> uint64_t {
    StorageManager& storageManager = StorageManager::getInstance();
    return storageManager.get_latest_bandwidth();
}

/**
 * @param file_location A file to send
 * @return 0 on successfull queueing of the file, -1 on failure
 */
extern "C" LIB_PUBLIC auto send_file(const char *file_location, u_int64_t deadline, const char * content_type, bool await = false) -> int{
    FrameMarkStart("send_file");
    ZoneScopedN("send_file");
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();

    // Capture the current time in ms
    auto start = std::chrono::high_resolution_clock::now();

    auto toi = fluteTransmissionManager.send_file(std::move(std::string(file_location)),  deadline, std::move(std::string(content_type)));

    if (!await || toi <= 0) {
        FrameMarkEnd("send_file");
        return toi;
    }

    auto file_size = fluteTransmissionManager.get_file_size(toi);

    FluteReceptionManager& fluteReceptionManager = FluteReceptionManager::getInstance();

    while (!fluteReceptionManager.has_received(toi) && !fluteTransmissionManager.has_removed(toi)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Capture the current time in us
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    spdlog::info("From server storage to complete reception: {} us", duration);
    // Calculate the bandwidth
    auto bandwidth = static_cast<unsigned long>(ceil((file_size * 8) / 1000.0 / ((duration > 0 ? duration  : 1) / 1000000.0)));
    spdlog::info("Bandwidth: {} kbps", bandwidth);
    spdlog::info("File size: {} kbytes", file_size / 1000.0);

    StorageManager& storageManager = StorageManager::getInstance();
    storageManager.set_latest_bandwidth(bandwidth);

    FrameMarkEnd("send_file");
    return toi;
}

/**
 * @param file_locations A vector of files to send
 * @return 0 on successfull queueing of the files, -x on failure, where x is the number of files that failed to be queued
*/
extern "C" LIB_PUBLIC auto send_files(char **file_locations, u_int64_t deadline, const char * content_type) -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    std::vector<std::string> file_locations_vector;
    for (int i = 0; file_locations[i]; i++) {
        file_locations_vector.emplace_back(file_locations[i]);
    }
    return fluteTransmissionManager.send_files(file_locations_vector, deadline, std::move(std::string(content_type)));
}

extern "C" LIB_PUBLIC auto clear_files() -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.clear_files();
}

extern "C" LIB_PUBLIC auto add_stream(uint32_t stream_id, const char *content_type, uint32_t max_source_block_length, uint32_t file_length) -> bool {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.add_stream(stream_id, std::move(std::string(content_type)), max_source_block_length, file_length);
}

extern "C" LIB_PUBLIC auto send_to_stream(uint32_t stream_id, const char *content, uint32_t length) -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.send_to_stream(stream_id, std::move(std::string(content, length)));
}

extern "C" LIB_PUBLIC auto set_rate_limit(uint32_t rate_limit) -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.set_rate_limit(rate_limit);
}

extern "C" LIB_PUBLIC auto current_total_file_size() -> uint64_t {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.current_total_file_size();
}


/**
 * Counts the number of symbols listed in the json string
 * 
 * @param json_string This string holds a json of the Data stucture.
 * @returns The number of symbols.
*/
extern "C" LIB_PUBLIC auto symbol_count(const char *json_c_string) -> uint64_t {
    std::string json_string(json_c_string);
    Data data = convert(json_string);
    auto symbols = 0;
    for (auto& block : data.missing) {
        for (const auto &symbol : block.second) {
            ++symbols;
        }

    }
    return symbols;
}

/**
 * Returns an estimated length of the final result.
 * It works by multiplying the number of symbols with the maximum size of an ALC packet.
 * 
 * @param json_string This string holds a json of the Data stucture.
 * @returns The estimated length.
*/
extern "C" LIB_PUBLIC auto length(const char *json_c_string) -> uint64_t {
    // Each symbol is wrapped in it's own ALC packet.
    return symbol_count(json_c_string) * (2048 + strlen("ALC "));
}

extern "C" LIB_PUBLIC auto retrieve(const char *json_c_string, char* result) -> size_t {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    StorageManager& storageManager = StorageManager::getInstance();

    std::string json_string(json_c_string);
    auto result_str = fluteTransmissionManager.retrieve(json_string, storageManager.get_arguments().mtu);

    // Copy the result to the result buffer
    memcpy(result, result_str.c_str(), result_str.length());
    return result_str.length();
}

extern "C" LIB_PUBLIC void setup(int argc, char **argv) {
    // Load the arguments
    StorageManager& storageManager = StorageManager::getInstance();
    storageManager.setup(argc, argv);

    switch (__cplusplus)
    {
        case 202101L:
            spdlog::info("Using C++23");
            break;
        case 202002L:
            spdlog::info("Using C++20");
            break;
        case 201703L:
            spdlog::info("Using C++17");
            break;
        case 201402L:
            spdlog::info("Using C++14");
            break;
        case 201103L:
            spdlog::info("Using C++11");
            break;
        case 199711L:
            spdlog::info("Using C++98");
            break;
        default:
            spdlog::info("Using C++: {}", __cplusplus);
            break;
    }

    // Create the transmission and reception managers
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    FluteReceptionManager& fluteReceptionManager = FluteReceptionManager::getInstance();

    spdlog::info("The managers have been created.");

    // Create a fake network socket
    auto network_socket = storageManager.create_network_socket(40000, 40000, 40000, fluteTransmissionManager.io, fluteReceptionManager.io);

    spdlog::info("The network socket has been created.");

    // Setup the transmission and reception managers
    fluteTransmissionManager.setup(storageManager.get_arguments());
    fluteTransmissionManager.set_network_socket(network_socket);
    fluteReceptionManager.setup(network_socket);

    // Set the loss rate
    network_socket->set_loss_rate(storageManager.get_arguments().loss_rate / 100.0);
    // Start the network socket threads
    network_socket->start_threads();

    // Set the retrieve function
    network_socket->set_retrieve_function([&](const std::string &json) -> std::string {
        spdlog::trace("[RETRIEVE] Retrieving missing data: {}", json);
        FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
        StorageManager& storageManager = StorageManager::getInstance();
        return fluteTransmissionManager.retrieve(json, storageManager.get_arguments().mtu);
    });
    

    // Log the version
    spdlog::info("FLUTE version {}.{}.{} was setup",
        std::to_string(VERSION_MAJOR).c_str(),
        std::to_string(VERSION_MINOR).c_str(),
        std::to_string(VERSION_PATCH).c_str());
}