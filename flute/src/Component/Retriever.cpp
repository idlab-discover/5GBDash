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
#include "Component/Retriever.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>

#include "Object/File.h"
#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"

#include "public/tracy/Tracy.hpp"

LibFlute::Retriever::Retriever(uint64_t tsi, unsigned short mtu, FecScheme fec_scheme)
    : _mtu(mtu),
      _tsi(tsi) {
    ZoneScopedN("Retriever::Retriever");
    _max_payload = mtu -
                   20 -  // IPv4 header // TODO: fix IPv6 support (look at Transmitter.cpp)
                   8 -   // UDP header
                   32 -  // ALC Header with EXT_FDT and EXT_FTI
                   4;    // SBN and ESI for compact no-code or raptor FEC
    uint32_t max_source_block_length = 64; // Change this in Transmitter.cpp as well

    unsigned int Al = 4; // RFC 5053: 4.2 Example Parameter Derivation Algorithm (change this in RaptorFEC.h and Transmitter.cpp as well)
    switch(fec_scheme) {
        case FecScheme::Raptor:
            max_source_block_length = 842; // RFC 6681: 7.4 FEC Code Specification (change this in Transmitter.cpp as well)
            if (_max_payload % Al) {
                _max_payload -= (_max_payload % Al); // Max payload must be divisible by Al.
            }
            break;
        default:
            break;
    }

    _fec_oti = FecOti{fec_scheme, 0, _max_payload, max_source_block_length};
}

LibFlute::Retriever::~Retriever() {
    ZoneScopedN("Retriever::~Retriever");
};

auto LibFlute::Retriever::seconds_since_epoch() -> uint64_t {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

auto LibFlute::Retriever::get_alcs(
    const std::string &content_location,
    const std::string &content_type,
    uint32_t expires,
    char *data,
    size_t length,
    uint64_t toi,
    std::map<uint32_t,std::vector<uint32_t>> search_map) -> std::string {
    ZoneScopedN("Retriever::get_alcs");

    std::shared_ptr<LibFlute::FileBase> file;
    try {
        file = std::make_shared<LibFlute::File>(
            toi,
            _fec_oti,
            content_location,
            content_type,
            expires,
            0, // Can be ignored, only used by the sender
            data,
            length,
            false, // Do not copy the data, we don't need it,
            false // Do not calculate the hash, we don't need it
            );
    } catch (const char *e) {
        spdlog::error("[RETRIEVE] Failed to create File object for file {} : {}", content_location, e);
        return "";
    }

    return get_alcs_from_file(file, search_map);
}

auto LibFlute::Retriever::get_alcs_from_file(
    std::shared_ptr<LibFlute::FileBase> file,
    std::map<uint32_t,std::vector<uint32_t>> search_map) -> std::string {
    
    std::stringstream string_stream;

    // Counter, for total amount of symbols
    uint32_t total_symbol_amount = 0;

    std::vector<LibFlute::EncodingSymbol> encoding_symbols;

    auto content_lock = file->get_content_buffer_lock();

    for (auto& block : file->get_source_blocks()) {
        // Increment total_symbol_amount
        total_symbol_amount += block.second.symbols.size();
        if (search_map.find(block.first) != search_map.end()) {
            auto searched_symbols = search_map[block.first];
            for (const auto &symbol : block.second.symbols) {
                if (std::find(searched_symbols.begin(), searched_symbols.end(), symbol.first) != searched_symbols.end()) {

                    //spdlog::trace("[RETRIEVE] Retreiving TOI {} SBN {} ID {}", file->meta().toi, block.first, symbol.first);

                    // Some safety checks to make sure the symbol is valid
                    if (symbol.second.data != nullptr && symbol.second.length > 0 && symbol.second.has_content) {
                        // Encode the symbol and place it in the encoding_symbols vector
                        encoding_symbols.emplace_back(
                            symbol.first,
                            block.first,
                            symbol.second.data,
                            symbol.second.length,
                            file->fec_oti().encoding_id);
                    }
                }
            }
        }
    }

    uint32_t total_symbols_selected = encoding_symbols.size();

    int max_symbols_per_alc = std::floor((float)(_max_payload) / (float)file->fec_oti().encoding_symbol_length);

    while(!encoding_symbols.empty()) {
        std::vector<LibFlute::EncodingSymbol> selected_symbols;
        if (encoding_symbols.size() > max_symbols_per_alc) {
            selected_symbols = std::vector<LibFlute::EncodingSymbol>(encoding_symbols.begin(), encoding_symbols.begin() + max_symbols_per_alc);
        } else {
            selected_symbols = encoding_symbols;
        }

        if (selected_symbols.empty()) {
            break;
        }

        spdlog::trace("[RETRIEVE] Creating ALC packet with {} symbols for block {} starting at symbol {}", selected_symbols.size(), selected_symbols[0].source_block_number(), selected_symbols[0].id());

        auto packet = std::make_shared<AlcPacket>(_tsi, file->meta().toi, file->fec_oti(), selected_symbols, _max_payload, file->fdt_instance_id());
        
        string_stream << "ALC ";
        const char* packet_data = packet->data();
        string_stream.write(packet_data, packet->size());
        string_stream << "\r\n\r\n";

        encoding_symbols.erase(encoding_symbols.begin(), encoding_symbols.begin() + selected_symbols.size());
    }

    content_lock.unlock();

    // Calculate the percentage of symbols selected
    double percentage = (double)total_symbols_selected / (double)total_symbol_amount * 100;
    LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
    auto alc_percentage_retrieved = metricsInstance.getOrCreateGauge("alc_percentage_retrieved");
    alc_percentage_retrieved->Set(percentage);

    spdlog::debug("[RETRIEVE] ALC percentage retrieved: {}", percentage);


    return string_stream.str();
}

