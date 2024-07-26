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
#pragma once

#include <cstdint>
#include <map>

/** \mainpage LibFlute - ALC/FLUTE library
 *
 * The library contains two simple **example applications** as a starting point:
 * - examples/flute-transmitter.cpp for sending files
 * - examples/flute-receiver.cpp for receiving files
 *
 * The relevant public headers for using this library are
 * - LibFlute::Transmitter (in include/Component/Transmitter.h), and
 * - LibFlute::Receiver (in include/Component/Receiver.h)
 *
 */

namespace LibFlute {
    /**
    *  Content Encodings
    */
    enum class ContentEncoding {
        NONE,
        ZLIB,
        DEFLATE,
        GZIP
    };

    /**
    *  Error correction schemes
    */
    enum class FecScheme {
        CompactNoCode = 0,
        Raptor = 1,
        Reed_Solomon_GF_2_m = 2,                // Not yet implemented
        LDPC_Staircase_Codes = 3,               // Not yet implemented
        LDPC_Triangle_Codes = 4,                // Not yet implemented
        Reed_Solomon_GF_2_8 = 5,                // Not yet implemented
        RaptorQ = 6,                            // Not yet implemented
        SmallBlockLargeBlockExpandable = 128,   // Not yet implemented
        SmallBlockSystematic = 129,             // Not yet implemented
        Compact = 130                           // Not yet implemented
    };

    /**
    *  OTI values struct
    */
    struct FecOti {
        FecScheme encoding_id;
        uint64_t transfer_length;
        uint32_t encoding_symbol_length;
        uint32_t max_source_block_length;
    };

    struct SourceBlock {
        uint16_t id = 0; // The id of the source block
        bool complete = false;
        std::size_t length = 0; // Total sum of all symbol buffer sizes for this block
        struct Symbol {
            uint16_t id; // The id of the symbol
            char* data;
            std::size_t length; // Symbol size in bytes
            bool has_content = true; // This is only used by FileStream to recognise if the symbol has content or not. FileBase::get_next_symbols and retriever only select symbols where this is true. Ignore this everywhere else. (Leave it true except when you know what you are doing.)
            bool complete = false;
            bool queued = false;
        };
        std::map<uint16_t, Symbol> symbols;
    };
};
