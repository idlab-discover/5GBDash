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

#include "Component/Transmitter.h"
#include "Metric/Metrics.h"
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
            argp_usage(state);
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
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

/**
 *  Main entry point for the program.
 *
 * @param argc  Command line agument count
 * @param argv  Command line arguments
 * @return 0 on clean exit, -1 on failure
 */
auto main(int argc, char **argv) -> int {
    struct ft_arguments arguments;
    /* Default values */
    arguments.mcast_target = "238.1.1.95";
    arguments.toi_start = 1;
    arguments.instance_id_start = 1;
    arguments.deadline = 0;
    arguments.mtu = 1500;

    argp_parse(&argp, argc, argv, 0, nullptr, &arguments);

    // Set up logging
    spdlog::set_level(
        static_cast<spdlog::level::level_enum>(arguments.log_level));
    spdlog::set_pattern("[%H:%M:%S.%f][thr %t][%^%l%$] %v");

    auto exact_start_time = std::chrono::system_clock::now();
    spdlog::info("FLUTE transmitter demo starting up");

    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    metricsInstance.setLogFile("./server_multicast.metric.log");
    auto multicast_files_sent_gauge = metricsInstance.getOrCreateGauge("multicast_files_sent");
    auto transmission_time_gauge = metricsInstance.getOrCreateGauge("transmission_time_gauge");

    try {
        // We're responsible for buffer management, so create a vector of structs that
        // are going to hold the data buffers
        struct FsFile {
            std::string location;
            char *buffer;
            size_t len;
            uint32_t toi;
        };
        std::vector<FsFile> files;

        // read the file contents into the buffers
        for (int j = 0; arguments.files[j]; j++) {
            std::string location = arguments.files[j];

            // [IDLab] Only handle files that exists.
            if (!file_exists(location)) {
                spdlog::info("{} does not exists", location);

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
                    continue;
                }

            
                // If the second directory contains an '_', then take the part before the first '_'.
                std::string second_directory = directories[1];
                if (second_directory.find('_') == std::string::npos) {
                    continue;
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
                    continue;
                } else {
                    spdlog::info("{} does exists", location);
                }
                
            }

            std::ifstream file(location, std::ios::binary | std::ios::ate);
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            char *buffer = (char *)malloc(size);
            //TracyAlloc(buffer, size);
            file.read(buffer, size);
            // Add the file to the vector
            // Use the original location, not the location variable, because this might be modified above
            files.push_back(FsFile{arguments.files[j], buffer, (size_t)size});
        }

        // Create a Boost io_service
        boost::asio::io_service io;

        // Construct the transmitter class
        LibFlute::Transmitter transmitter(
            arguments.mcast_target,
            (short)arguments.mcast_port,
            16,
            arguments.mtu,
            arguments.rate_limit,
            LibFlute::FecScheme(arguments.fec),
            io,
            arguments.toi_start,
            arguments.instance_id_start);

        transmitter.set_stop_when_done(true); // [IDLab] Stop the transmitter when all files have been transmitted

        // Configure IPSEC ESP, if enabled
        if (arguments.enable_ipsec) {
            transmitter.enable_ipsec(1, arguments.aes_key);
        }

        // Register a completion callback
        transmitter.register_completion_callback(
            [&files, &multicast_files_sent_gauge,&transmitter](uint32_t toi) {
                multicast_files_sent_gauge->Increment();
                for (auto &file : files) {
                    if (file.toi == toi) {
                        spdlog::info("{} (TOI {}) has been transmitted",
                                     file.location, file.toi);
                        // free() the buffer here
                        //TracyFree(file.buffer);
                        munmap(file.buffer,file.len);
                    }
                }
            });

        // Queue all the files
        for (auto &file : files) {
            file.toi = transmitter.send(file.location,
                                        "application/octet-stream",
                                        transmitter.seconds_since_epoch() + 60,  // 1 minute from now
                                        arguments.deadline,
                                        file.buffer,
                                        file.len);
            spdlog::info("Queued {} ({} bytes) for transmission, TOI is {}",
                         file.location, file.len, file.toi);
        }

        // Create a work guard to keep the io_service running
        auto work_guard = boost::asio::make_work_guard(io);
        // Start the io_service, and thus sending data
        io.run();

        spdlog::debug("All files have been sent. Exiting...");
        auto exact_end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(exact_end_time - exact_start_time).count();
        transmission_time_gauge->Set(static_cast<double>(duration));

        /*
        spdlog::info("FLUTE transmitter demo shutting down");
        */

        auto next_instance_id = (transmitter.current_instance_id() + 1) & ((1 << 20) - 1);
        std::cout << "next_instance_id = " << next_instance_id << std::endl;

    } catch (const std::exception &ex) {
        std::cout << "Caught exception \"" << ex.what() << "\"\n";
        spdlog::error("Exiting on unhandled exception: {}", ex.what());
    } catch (const char* errorMessage) {
        std::cout << "Caught exception \"" << errorMessage << "\"\n";
        spdlog::error("Exiting on unhandled error: {}", errorMessage);
    } catch (...) {
        spdlog::error("Exiting on unhandled exception");
    }

exit:
    return 0;
}
