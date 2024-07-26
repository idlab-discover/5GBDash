// libflute - FLUTE/ALC library
//
// Copyright (C) 2023 Casper Haems (IDLab, Ghent University, in collaboration with imec)
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

#include <boost/asio.hpp>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <libconfig.h++>
#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "Component/Retriever.h"
#include "Metric/Metrics.h"
#include "Version.h"
#include "spdlog/async.h"
#include "spdlog/sinks/syslog_sink.h"
#include "spdlog/spdlog.h"

#include "public/tracy/Tracy.hpp"


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

struct Data {
    std::string file;
    uint64_t toi;
    unsigned fec;
    std::map<uint32_t,std::vector<uint32_t>> missing;
};

/**
 * Check if a file exists.
 * 
 * @param the file path to check
 * @return true if the file exists, false if not
*/
auto file_exists(const std::string& name) -> bool {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

/**
 *  Function to split a string into vector of strings using a delimiter
 *  https://stackoverflow.com/questions/14265581/parse-split-a-string-in-c-using-string-delimiter-standard-c#answer-46931770
 *
*/
auto split(const std::string& str, char delimiter) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::istringstream tokenStream(str);
    std::string token;
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

/**
 * Proof of concept parser for converting incoming string to the required data object.
 * A proper json library or another input format would be better.
*/
auto convert(const std::string& json_string) -> Data {
    Data data;
    try {
        std::stringstream ss;
        ss << json_string;

        boost::property_tree::ptree pt;
        boost::property_tree::read_json(ss, pt);

        data.toi = std::stoi(pt.get<std::string>("toi"));
        data.file = pt.get<std::string>("file");
        data.fec = std::stoi(pt.get<std::string>("fec"));

        // Iterate over all the blocks.
        boost::property_tree::ptree missing_pt = pt.get_child("missing");
        for (boost::property_tree::ptree::iterator block = missing_pt.begin(); block != missing_pt.end(); ++block) {
            // Iterate over all the missing symbols of the current block.
            std::vector<uint32_t> symbols;
            for (boost::property_tree::ptree::iterator symbol = block->second.begin(); symbol != block->second.end(); ++symbol) {
                symbols.push_back(std::stoi(symbol->second.data()));
            }
            data.missing[std::stoi(block->first)] = symbols;
        }

    } catch (const std::exception& e) {
        spdlog::error("Error parsing JSON: {}", e.what());
        spdlog::error("String was {}", json_string);
    }

    return data;

}

extern "C" LIB_PUBLIC void setup(
    uint16_t log_level = 2) {
    // Set up logging
    spdlog::set_level(
        static_cast<spdlog::level::level_enum>(log_level));
    spdlog::set_pattern("[%H:%M:%S.%f][thr %t][%^%l%$] %v");
    spdlog::info("FLUTE retriever demo starting up");

    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    metricsInstance.setLogFile("./server_http.metric.log");
    auto alc_percentage_retrieved = metricsInstance.getOrCreateGauge("alc_percentage_retrieved");
}

extern "C" LIB_PUBLIC auto retrieve(const char *json_string, uint16_t mtu, char* result) -> size_t {

    try {

        Data data = convert(json_string);
        spdlog::info("(TOI {}) Partial request received for {}",data.toi, data.file);

        std::string location = data.file;

        // Only handle files that exists.
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
                return 0;
            }

        
            // If the second directory contains an '_', then take the part before the first '_'.
            std::string second_directory = directories[1];
            if (second_directory.find('_') == std::string::npos) {
                spdlog::info("{} does not exists", location);
                return 0;
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
                return 0;
            }
        }

        // Construct the retriever class.
        LibFlute::Retriever retriever(16, mtu, LibFlute::FecScheme(data.fec));

        // Read the file contents into the buffers.
        std::ifstream file(location, std::ios::binary | std::ios::ate);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Allocate memory to read the file using new
        char* buffer = nullptr;
        try {
            buffer = new char[size];
        } catch (std::bad_alloc& e) {
            spdlog::error("Memory allocation failed for file: {} with size: {}", location, size);
            return 0;
        }
        //TracyAlloc(buffer, size);
        if (!file.read(buffer, size)) {
            spdlog::error("Failed to read file: {}", location);
            delete[] buffer;
            return 0;
        }

        auto retrieved = retriever.get_alcs(data.file, // We use the original filename here
                        "application/octet-stream",
                        retriever.seconds_since_epoch() + 60,  // 1 minute from now
                        buffer,
                        (size_t)size,
                        data.toi,
                        data.missing);

        // Free the buffer
        //TracyFree(buffer);
        // free(buffer);
        delete[] buffer;

        memcpy(result, retrieved.c_str(), retrieved.size());
        return retrieved.size();
    } catch (const std::exception &ex) {
        spdlog::error("Exiting on unhandled exception: {}", ex.what());
    } catch (const char* errorMessage) {
        std::cout << "Caught exception \"" << errorMessage << "\"\n";
        spdlog::error("Exiting on unhandled error: {}", errorMessage);
    } catch (...) {
        spdlog::error("Exiting on unhandled exception");
    }

    return 0;
}

/**
 * Counts the number of symbols listed in the json string
 * 
 * @param json_string This string holds a json of the Data stucture.
 * @returns The number of symbols.
*/
extern "C" LIB_PUBLIC auto symbol_count(const char *json_string) -> uint64_t {
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
extern "C" LIB_PUBLIC auto length(const char *json_string) -> uint64_t {
     // Each symbol is wrapped in it's own ALC packet.
    return symbol_count(json_string) * (2048 + strlen("ALC "));
}
