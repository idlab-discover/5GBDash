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
#include <ctime>

#include <boost/asio.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <libconfig.h++>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>


#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h> 

#include "Metric/Metrics.h"
#include "Object/File.h"
#include "Component/Receiver.h"
#include "Version.h"
#include "spdlog/async.h"
#include "spdlog/sinks/syslog_sink.h"
#include "spdlog/spdlog.h"

#include "public/tracy/Tracy.hpp"

using libconfig::Config;
using libconfig::FileIOException;
using libconfig::ParseException;

static void print_version(FILE *stream, struct argp_state *state);
void (*argp_program_version_hook)(FILE *, struct argp_state *) = print_version;
const char *argp_program_bug_address = "Austrian Broadcasting Services <obeca@ors.at>";
static char doc[] = "FLUTE/ALC receiver demo";  // NOLINT

static struct argp_option options[] = {  // NOLINT
    {"interface", 'i', "IF", 0, "IP address of the interface to bind flute receivers to (default: 0.0.0.0)", 0},
    {"target", 'm', "IP", 0, "Multicast address to receive on (default: 238.1.1.95)", 0},
    {"retreival_url", 'r', "URL", 0, "Url used to retrieve lost packets. Disabled if empty (default: '')", 0},
    {"fdt_retrieval_interval", 'f', "INTERVAL", 0, "Interval in ms to retrieve FDTs when receiving unrecognised ALCs (default: 1000)", 0},
    {"alc_retrieval_interval", 'a', "INTERVAL", 0, "Interval in ms to retrieve missing ALCs (default: 100)", 0},
    {"port", 'p', "PORT", 0, "Multicast port (default: 40085)", 0},
    {"directory", 'd', "DIRECTORY", 0, "Directory to store files (default: ./)", 0},
    {"ipsec-key", 'k', "KEY", 0, "To enable IPSec/ESP decryption of packets, provide a hex-encoded AES key here", 0},
    {"log-level", 'l', "LEVEL", 0,
     "Log verbosity: 0 = trace, 1 = debug, 2 = info, 3 = warn, 4 = error, 5 = "
     "critical, 6 = none. Default: 2.",
     0},
    {"video-ids", 'v', "IDS", 0, "Comma separated list of video ids to receive", 0},
    {nullptr, 0, nullptr, 0, nullptr, 0}};

/**
 * Holds all options passed on the command line
 */
struct ft_arguments {
    const char *flute_interface = {}; /**< file path of the config file. */
    const char *mcast_target = {};
    const char *retreival_url = {};
    unsigned short fdt_retrieval_interval = 1000;
    unsigned short alc_retrieval_interval = 100;
    bool enable_ipsec = false;
    const char *aes_key = {};
    unsigned short mcast_port = 40085;
    unsigned log_level = 2; /**< log level */
    std::string video_ids;
    char **files;
    std::string directory = "./";
};

/**
 * Parses the command line options into the arguments struct.
 */
static auto parse_opt(int key, char *arg, struct argp_state *state) -> error_t {
    auto arguments = static_cast<struct ft_arguments *>(state->input);
    switch (key) {
        case 'r':
            arguments->retreival_url = arg;
            break;
        case 'f':
            arguments->fdt_retrieval_interval = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case 'a':
            arguments->alc_retrieval_interval = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case 'm':
            arguments->mcast_target = arg;
            break;
        case 'i':
            arguments->flute_interface = arg;
            break;
        case 'k':
            arguments->aes_key = arg;
            arguments->enable_ipsec = true;
            break;
        case 'p':
            arguments->mcast_port = static_cast<unsigned short>(strtoul(arg, nullptr, 10));
            break;
        case 'l':
            arguments->log_level = static_cast<unsigned>(strtoul(arg, nullptr, 10));
            break;
        case 'd':
            arguments->directory = std::string(arg);
            break;
            break;
        case 'v':
            arguments->video_ids = std::string(arg);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = {options, parse_opt, nullptr, doc,
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
 * https://stackoverflow.com/questions/71658440/c17-create-directories-automatically-given-a-file-path#answer-71658518
*/
auto CreateDirectoryRecursive(std::string const & dirName, std::error_code & err) -> bool
{
    err.clear();
    if (!std::filesystem::create_directories(dirName, err))
    {
        if (std::filesystem::exists(dirName))
        {
            // The folder already exists:
            err.clear();
            return true;    
        }
        return false;
    }
    return true;
}

/**
 *  Main entry point for the program.
 *
 * @param argc  Command line agument count
 * @param argv  Command line arguments
 * @return 0 on clean exit, -1 on failure
 */
auto main(int argc, char **argv) -> int {
    ZoneScopedN("main");
    struct ft_arguments arguments;
    /* Default values */
    arguments.mcast_target = "238.1.1.95";
    arguments.flute_interface = "0.0.0.0";
    arguments.retreival_url = "";
    arguments.video_ids = "";

    // Parse the arguments
    argp_parse(&argp, argc, argv, 0, nullptr, &arguments);

    // Set up logging
    spdlog::set_level(
        static_cast<spdlog::level::level_enum>(arguments.log_level));
    spdlog::set_pattern("[%H:%M:%S.%f][thr %t][%^%l%$] %v");

    spdlog::info("FLUTE receiver demo starting up");

    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    metricsInstance.setLogFile("./proxy_multicast_" + arguments.directory + ".metric.log");
    auto multicast_files_received_gauge = metricsInstance.getOrCreateGauge("multicast_files_received");
    auto multicast_reception_time = metricsInstance.getOrCreateGauge("multicast_reception_time");
    auto multicast_reception_time_before_deadline = metricsInstance.getOrCreateGauge("multicast_reception_time_before_deadline");
    auto multicast_reception_time_after_deadline = metricsInstance.getOrCreateGauge("multicast_reception_time_after_deadline");

    // Define a flag to control the loop.
    // If no retreival_url is set, then the flag is true
    std::atomic<bool> stopFlagFetcher(strlen(arguments.retreival_url) == 0);
    //std::jthread ioThread;
    std::jthread fetchMissingAlcThread;
    std::jthread fetchMissingFdtThread;
    // Define a flag to control the loop.
    std::atomic<bool> stopFlag(false);
    std::jthread handleALCBufferThread;
    std::jthread removeExpiredFilesThread;

    // Convert video_ids to a vector of strings (shared_ptr)
    std::shared_ptr<std::vector<std::string>> video_ids = std::make_shared<std::vector<std::string>>();
    if (arguments.video_ids.length() > 0) {
        std::stringstream ss(arguments.video_ids);
        std::string item;
        while (std::getline(ss, item, ',')) {
            video_ids->push_back(item);
        }
    }


    try {
        // Create a Boost io_service
        boost::asio::io_service io;

        // Create the receiver
        LibFlute::Receiver receiver(
            arguments.flute_interface,
            arguments.mcast_target,
            arguments.retreival_url,
            (short)arguments.mcast_port,
            16,
            io);

        // If there are video ids, set them
        if (video_ids->size() > 0) {
            receiver.set_video_ids_ptr(video_ids);
        }

        // Configure IPSEC, if enabled
        if (arguments.enable_ipsec) {
            receiver.enable_ipsec(1, arguments.aes_key);
        }

        receiver.register_completion_callback(
            [&](std::shared_ptr<LibFlute::FileBase> file) {  // NOLINT
                time_t current_time = time(nullptr);
                spdlog::info("{} (TOI {}) has been received",
                             file->meta().content_location, file->meta().toi);

                // Get the relative file path;
                std::string path = file->meta().content_location.c_str();
                // Create the directories if needed
                std::size_t first_slash = path.find_first_of("/\\");
                std::size_t last_slash = path.find_last_of("/\\");
                std::string file_location = arguments.directory + "/" + path.substr(first_slash, last_slash - first_slash);
                std::error_code err;
                if (!CreateDirectoryRecursive(file_location, err)) {
                    spdlog::error("CreateDirectoryRecursive FAILED, err: {}", err.message());
                }
                // Append the filename
                file_location += path.substr(last_slash);

                // Open the file as a file descriptor
                int fd = open(file_location.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if (fd < 0) {
                    spdlog::error("Failed to open file for writing: {}", file_location);
                    return;
                }
                // Lock the file, so no other process can read or write to it
                bool is_locked = flock(fd, LOCK_EX | LOCK_NB) == 0; // Non-blocking lock
                if (!is_locked) {
                    spdlog::error("[TRANSMIT] Failed to lock file {} for writing", file_location);
                    close(fd);
                    return;
                }

                // Get a filestream from the file descriptor
                FILE * file_stream = fdopen(fd, "wb");
                // Check if the filestream is valid
                if (!file_stream) {
                    spdlog::error("Failed to open file stream for writing: {}", file_location);
                    close(fd);
                    return;
                }
                // Write the buffer to the file in storage.
                fwrite(file->buffer(), 1, file->length(), file_stream);
                // Flush the filestream to write the data to the file
                fflush(file_stream);
                // Unlock the file
                flock(fd, LOCK_UN);
                // This also closes the underlying file descriptor
                fclose(file_stream);

                /*
                // Print the first 40 bytes of the file in hex
                std::stringstream hex_stream;
                hex_stream << std::hex << std::setfill('0');
                for (int i = 0; i < 40; ++i) {
                    hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(file->buffer()[i]));
                    if (i % 2 == 1) {
                        hex_stream << ' ';  // Add a space every two characters
                    }
                }
                spdlog::info("First 40 bytes of {}: {}", file_location, hex_stream.str());
                */

                multicast_files_received_gauge->Increment();
                multicast_reception_time->Set(static_cast<double>(current_time - file->received_at()));

                multicast_reception_time_before_deadline->Set(static_cast<double>(file->time_before_deadline()));
                multicast_reception_time_after_deadline->Set(static_cast<double>(file->time_after_deadline()));

            });

        fetchMissingFdtThread = std::jthread([&]() {
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "fetchMissingFdtThread");

            while (!stopFlagFetcher) {
                receiver.resolve_fdt_for_buffered_alcs();
                std::this_thread::sleep_for(std::chrono::milliseconds(arguments.fdt_retrieval_interval));
            }
        });

        fetchMissingAlcThread = std::jthread([&]() {
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "fetchMissingAlcThread");
            while (!stopFlagFetcher) {
                auto files = receiver.file_list();
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
                std::this_thread::sleep_for(std::chrono::milliseconds(arguments.alc_retrieval_interval));
            }
        });

        handleALCBufferThread = std::jthread([&]() {
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "handleALCBufferThread");
            while (!stopFlag) {
                // If the buffer is not empty, handle the first element
                if (receiver.handle_alc_buffer()) {
                    // Wait for 1 nanosecond if the buffer is not empty
                    std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                } else {
                    // Wait for 1 microsecond if the buffer is empty
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                }
            }
            spdlog::info("handleALCBufferThread stopped");
        });

        removeExpiredFilesThread = std::jthread([&]() {
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "removeExpiredFilesThread");
            while (!stopFlag) {
                // Every 1 second, we remove files of which the reception started 60 seconds ago.
                receiver.remove_expired_files(60);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });

        // Start a new thread for io_service
        /*ioThread = std::jthread([&]() {
            ZoneScopedN("ioThread");
            spdlog::info("IO thread started");
            // Track the CPU usage of this thread
            metricsInstance.addThread(std::this_thread::get_id(), "IO thread");
            // Create a work guard to keep the io_service running
            auto work_guard = boost::asio::make_work_guard(io);
            // Run the io_service
            io.run();
            spdlog::info("IO thread stopped");
        });*/

        // Create a work guard to keep the io_service running
        auto work_guard = boost::asio::make_work_guard(io);
        io.run();
    } catch (std::exception &ex) {
        spdlog::error("Exiting on unhandled exception: {}", ex.what());
    } catch (const char* errorMessage) {
        spdlog::error("Exiting on unhandled error: {}", errorMessage);
    } catch (...) {
        spdlog::error("Exiting on unhandled exception");
    }

exit:
    // Set the flags to stop the loop in the threads
    stopFlagFetcher = true;
    stopFlag = true;

    // Join ioThread and wait for it to finish
    /*if (ioThread.joinable()) {
        ioThread.join();
    }*/

    // Join fetchMissingFdtThread and wait for it to finish
    if (fetchMissingFdtThread.joinable()) {
        fetchMissingFdtThread.join();
    }
    // Join fetchMissingAlcThread and wait for it to finish
    if (fetchMissingAlcThread.joinable()) {
        fetchMissingAlcThread.join();
    }

    // Join handleALCBufferThread and wait for it to finish
    if (handleALCBufferThread.joinable()) {
        handleALCBufferThread.join();
    }

    // Join removeExpiredFilesThread and wait for it to finish
    if (removeExpiredFilesThread.joinable()) {
        removeExpiredFilesThread.join();
    }

    return 0;
}
