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

#include "Component/Transmitter.h"
#include "Metric/Metrics.h"
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
static char doc[] = "FLUTE/ALC transmitter demo";  // NOLINT

static struct argp_option options[] = {  // NOLINT
    {"target", 'm', "IP", 0, "Target multicast address (default: 238.1.1.95)", 0},
    {"fec", 'f', "FEC Scheme", 0, "Choose a scheme for Forward Error Correction. Compact No Code = 0, Raptor = 1 (default is 0)", 0},
    {"port", 'p', "PORT", 0, "Target port (default: 40085)", 0},
    {"mtu", 't', "BYTES", 0, "Path MTU to size ALC packets for (default: 1500)", 0},
    {"ipsec-key", 'k', "KEY", 0, "To enable IPSec/ESP encryption of packets, provide a hex-encoded AES key here", 0},
    {"toi-start", 'o', "TOI", 0, "The TOI assigned to the first file (default: 1)", 0},
    {"instance-id-start", 'i', "IID", 0, "The Instance Id assigned to the first file (default: 1)", 0},
    {"rate-limit", 'r', "KBPS", 0, "Transmit rate limit (kbps), 0 = use default, default: 1000 (1 Mbps)", 0},
    {"deadline", 'd', "MS", 0, "Time after epoch by which the files have to be received. Disabled if 0.(default: 0)", 0},
    {"log-level", 'l', "LEVEL", 0,
     "Log verbosity: 0 = trace, 1 = debug, 2 = info, 3 = warn, 4 = error, 5 = "
     "critical, 6 = none. Default: 2.",
     0},
    {nullptr, 0, nullptr, 0, nullptr, 0}};

/**
 * Holds all options passed on the command line
 */
struct ft_arguments {
    const char *mcast_target = {};
    bool enable_ipsec = false;
    const char *aes_key = {};
    unsigned short mcast_port = 40085;
    unsigned short mtu = 1500;
    uint16_t toi_start = 1;
    uint32_t instance_id_start = 1;
    uint32_t rate_limit = 1000;
    uint64_t deadline = 0;
    unsigned log_level = 2; /**< log level */
    unsigned fec = 0; 
    char **files;
};

/**
 * Parses the command line options into the arguments struct.
 */
static auto parse_opt(int key, char *arg, struct argp_state *state) -> error_t {
    auto arguments = static_cast<struct ft_arguments *>(state->input);
    switch (key) {
        case 'm':
            arguments->mcast_target = arg;
            break;
        case 'f':
            arguments->fec = static_cast<unsigned>(strtoul(arg, nullptr, 10));
            if ( (arguments->fec | 1) != 1 ) {
                spdlog::error("Invalid FEC scheme ! Please pick either 0 (Compact No Code) or 1 (Raptor)");
                return ARGP_ERR_UNKNOWN;
            }
            break;
        case 'k':
            arguments->aes_key = arg;
            arguments->enable_ipsec = true;
            break;
        case 'p':
            arguments->mcast_port = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case 't':
            arguments->mtu = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case 'o':
            arguments->toi_start = static_cast<uint16_t>(strtoul(arg, nullptr, 10));
            break;
        case 'i':
            arguments->instance_id_start = static_cast<uint32_t>(strtoul(arg, nullptr, 10));
            break;
        case 'r':
            arguments->rate_limit = static_cast<uint32_t>(strtoul(arg, nullptr, 10));
            break;
        case 'd':
            arguments->deadline = static_cast<uint64_t>(strtoul(arg, nullptr, 10));
            break;
        case 'l':
            arguments->log_level = static_cast<unsigned>(strtoul(arg, nullptr, 10));
            break;
        case ARGP_KEY_NO_ARGS:
            //argp_usage(state);
            arguments->files = nullptr;
            break;
        case ARGP_KEY_ARG:
            arguments->files = &state->argv[state->next - 1];
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
 * [IDLab] Check if a file exists.
 * 
 * @param the file path to check
 * @return true if the file exists, false if not
*/
auto file_exists(const std::string& name) -> bool {
    ZoneScopedN("file_exists");
    struct stat buffer;   
    return (stat (name.c_str(), &buffer) == 0); 
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

    void setup(int argc, char** argv) {
        ZoneScopedN("FluteTransmissionManager::setup");
        // Lock the mutex
        std::unique_lock<LockableBase(std::mutex)> lock(transmitter_mutex);
        // Unique lock

        /* Default values */
        arguments.mcast_target = "238.1.1.95";
        arguments.toi_start = 1;
        arguments.instance_id_start = 1;
        arguments.deadline = 0;
        arguments.mtu = 1500;

        argp_parse(&argp, argc, argv, 0, nullptr, &arguments);

        spdlog::set_level(
            static_cast<spdlog::level::level_enum>(arguments.log_level));   

        // Print the rate limit
        spdlog::info("Rate limit is {} kbps", arguments.rate_limit);  

        // Construct the transmitter class
        transmitter = std::make_unique<LibFlute::Transmitter>(
            arguments.mcast_target,
            (short)arguments.mcast_port,
            16,
            arguments.mtu,
            arguments.rate_limit,
            LibFlute::FecScheme(arguments.fec),
            io,
            arguments.toi_start,
            arguments.instance_id_start);

        // Configure IPSEC ESP, if enabled
        if (arguments.enable_ipsec) {
            transmitter->enable_ipsec(1, arguments.aes_key);
        }

        // Register a completion callback
        transmitter->register_completion_callback(
            [this](uint32_t toi) {
                if (toi == 0) {
                    return;
                }

                metricsInstance.getOrCreateGauge("multicast_files_sent")->Increment();
                // Lock the mutex
                std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);
                for (auto &file : files) {
                    if (file.toi == toi) {
                        spdlog::info("{} (TOI {}) has been transmitted", file.location, file.toi);
                        // free() the buffer here
                        // munmap(file.buffer,file.len);
                        delete[] file.buffer;

                        // Get the current time in ms since epoch
                        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()
                            ).count();

                        // if file.transmission_start_time is greater then 0, then we can calculate the transmission time
                        if (file.transmission_start_time > 0) {
                            auto difference = now - file.transmission_start_time;
                            metricsInstance.getOrCreateGauge("multicast_transmission_time")->Set(static_cast<double>(difference));
                        }
                    }
                }
                // Remove the file from the vector
                files.erase(std::remove_if(files.begin(), files.end(),
                    [toi](const FsFile &file) {
                        return file.toi == toi;
                    }),
                    files.end());
            });

        exact_start_time = std::chrono::system_clock::now();
        spdlog::info("FLUTE transmitter demo lib is ready");  

        // Unlock the mutex, so that we can add files
        lock.unlock();

        auto file_count = 0;
        if (arguments.files != nullptr) {  
            for (int j = 0; arguments.files[j]; j++) {
                send_file(arguments.files[j], arguments.deadline);
                file_count++;
            }
        }

        if (file_count > 0) {
            spdlog::info("All initial files have been queued for transmission");
        } else {
            spdlog::info("No initial files have been queued for transmission");
        }
    }

    void start() {
        ZoneScopedN("FluteTransmissionManager::start");
        // Lock the mutex
        std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);

        // Check if the thread is already running
        if (io_thread_running.load()) {
            spdlog::warn("IO thread is already running. Cannot start again.");
            return;
        }

        io_thread_running.store(true);  // Set the flag to indicate that the thread is running

        // Start a new thread for io_service
        std::jthread ioThread([this]() {
            ZoneScopedN("FluteTransmissionManager::ioThread");
            spdlog::info("IO thread started");
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "IO thread");
            // Create a work guard to keep the io_service running
            auto work_guard = boost::asio::make_work_guard(io);
            // Run the io_service
            io.run();
            io_thread_running.store(false);  // Set the flag to indicate that the thread has stopped
            spdlog::info("IO thread stopped");
        });

        // Detach the thread so it can run independently
        ioThread.detach();
    }

    void stop() {
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

        spdlog::debug("All files have been sent. Exiting...");
        auto exact_end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(exact_end_time - exact_start_time).count();
        auto transmission_time_gauge = metricsInstance.getOrCreateGauge("transmission_time_gauge"); 
        transmission_time_gauge->Set(static_cast<double>(duration));

        /*
        spdlog::info("FLUTE transmitter demo shutting down");
        */

        auto next_instance_id = (transmitter->current_instance_id() + 1) & ((1 << 20) - 1);
        std::cout << "next_instance_id = " << next_instance_id << std::endl;
    }

    auto send_file(std::string file_location, u_int64_t deadline) -> int{
        ZoneScopedN("FluteTransmissionManager::send_file");
        try {
            // spdlog::info("Queuing a file");
            std::string location(file_location); // Copy the string, because we're possibly going to modify it

            // [IDLab] Only handle files that exists.
            if (!file_exists(location)) {
                // [IDLab] The following is purely for our use case.
                // It is used to be able to send 'virtual' files.

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
                    spdlog::info("{} does not exists", location);
                    return -1;
                }

            
                // If the second directory contains an '_', then take the part before the first '_'.
                std::string second_directory = directories[1];
                if (second_directory.find('_') == std::string::npos) {
                    spdlog::info("{} does not exists", location);
                    return -1;
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
                if (!file_exists(location)) {
                    spdlog::info("{} does not exists", location);
                    return -1;
                }
                
            }

            std::ifstream file(location, std::ios::binary | std::ios::ate);
            std::streamsize size = file.tellg(); // Size is in bytes
            file.seekg(0, std::ios::beg);

            // Allocate memory to read the file using new
            char* buffer = nullptr;
            try {
                buffer = new char[size];
            } catch (std::bad_alloc& e) {
                spdlog::error("Memory allocation failed for file: {} with size: {}", location, size);
                return -1;
            }
            //TracyAlloc(buffer, size);
            if (!file.read(buffer, size)) {
                spdlog::error("Failed to read file: {}", location);
                delete[] buffer;
                return -1;
            }

            // Get the current time in ms since epoch
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

            // Use the original location, not the location variable, because this might be modified above
            FsFile fs_file{file_location, buffer, (size_t)size, now};

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
            spdlog::info("First 40 bytes of {}: {}", file_location, hex_stream.str());
            */

            fs_file.toi = transmitter->send(fs_file.location,
                                        "application/octet-stream",
                                        transmitter->seconds_since_epoch() + 60,  // 1 minute from now
                                        deadline,
                                        fs_file.buffer,
                                        fs_file.len);
            spdlog::info("Queued {} ({} bytes) for transmission, TOI is {}",
                        fs_file.location, fs_file.len, fs_file.toi);

            // Lock the mutex
            // spdlog::info(("Locking mutex"));
            std::lock_guard<LockableBase(std::mutex)> lock(transmitter_mutex);
            // spdlog::info(("Mutex locked"));

            files.push_back(fs_file);

            // spdlog::info("Done");
            

        } catch (const std::exception &ex) {
            std::cout << "Caught exception \"" << ex.what() << "\"\n";
            spdlog::error("Exiting on unhandled exception: {}", ex.what());

            return -1;
        } catch (const char* errorMessage) {
            std::cout << "Caught exception \"" << errorMessage << "\"\n";
            spdlog::error("Exiting on unhandled error: {}", errorMessage);

            return -1;
        } catch (...) {
            spdlog::error("Exiting on unhandled exception");

            return -1;
        }

        return 0;
    }

    auto send_files(std::vector<std::string> &file_locations, u_int64_t deadline) -> int {
        ZoneScopedN("FluteTransmissionManager::send_files");
        // We can't lock the mutex here, because send_file() locks it itself
        int result = 0;
        // Call send_file for each file
        for (const std::string& file_location : file_locations) {
            result += send_file(file_location, deadline);
        }

        return result;
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
            spdlog::info("{} (TOI {}) has been removed from the queue", file.location, file.toi);
            // free() the buffer here
            // TracyFree(file.buffer);
            // munmap(file.buffer,file.len);
            delete[] file.buffer;
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
private:
    struct ft_arguments arguments;
    // We're responsible for buffer management, so create a vector of structs that
    // are going to hold the data buffers
    struct FsFile {
        std::string location;
        char *buffer;
        size_t len; // Length of the buffer in bytes
        uint64_t transmission_start_time; // [ms]
        uint32_t toi;
    };
    std::vector<FsFile> files;
    std::chrono::time_point<std::chrono::system_clock> exact_start_time;
    LibFlute::Metric::Metrics& metricsInstance;
    boost::asio::io_service io;
    std::unique_ptr<LibFlute::Transmitter> transmitter;
    std::atomic<bool> io_thread_running{false};  // Flag to track the running status of the thread
    // A mutex to prevent the transmitter from being accessed from multiple threads concurrently
    TracyLockable(std::mutex, transmitter_mutex);

    FluteTransmissionManager(): metricsInstance(LibFlute::Metric::Metrics::getInstance()) {
        metricsInstance.setLogFile("./server_multicast.metric.log");
        // Set up logging
        spdlog::set_pattern("[%H:%M:%S.%f][thr %t][%^%l%$] %v");
        spdlog::info("FLUTE transmitter manager has loaded");  
    }
};

extern "C" LIB_PUBLIC void setup(int argc, char **argv) {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.setup(argc, argv);
}

extern "C" LIB_PUBLIC void start() {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.start();
}

extern "C" LIB_PUBLIC void stop() {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.stop();
}

extern "C" LIB_PUBLIC void set_thread_name(const char* thread_name) {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    fluteTransmissionManager.set_thread_name(std::move(std::string(thread_name)));
}

/**
 * @param file_location A file to send
 * @param deadline The deadline for the files to be received. After this time, the missing parts will be fetched over unicast. 0 = disabled
 * @return 0 on successfull queueing of the file, -1 on failure
 */
extern "C" LIB_PUBLIC auto send_file(const char *file_location, u_int64_t deadline) -> int{
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.send_file(std::move(std::string(file_location)),  deadline);
}

/**
 * @param file_locations A vector of files to send
 * @param deadline The deadline for the files to be received. After this time, the missing parts will be fetched over unicast. 0 = disabled
 * @return 0 on successfull queueing of the files, -x on failure, where x is the number of files that failed to be queued
*/
extern "C" LIB_PUBLIC auto send_files(char **file_locations, u_int64_t deadline) -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    std::vector<std::string> file_locations_vector;
    for (int i = 0; file_locations[i]; i++) {
        file_locations_vector.emplace_back(file_locations[i]);
    }
    return fluteTransmissionManager.send_files(file_locations_vector, deadline);
}

extern "C" LIB_PUBLIC auto clear_files() -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.clear_files();
}

extern "C" LIB_PUBLIC auto set_rate_limit(uint32_t rate_limit) -> int {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.set_rate_limit(rate_limit);
}

extern "C" LIB_PUBLIC auto current_total_file_size() -> uint64_t {
    FluteTransmissionManager& fluteTransmissionManager = FluteTransmissionManager::getInstance();
    return fluteTransmissionManager.current_total_file_size();
}