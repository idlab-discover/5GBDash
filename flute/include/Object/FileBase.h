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
#include <stddef.h>
#include <stdint.h>
#include <map>
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <semaphore>
#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"
#include "Object/FileDeliveryTable.h"
#include "Packet/AlcPacket.h"
#include "Packet/EncodingSymbol.h"

#include "public/tracy/Tracy.hpp"

namespace LibFlute {
    /**
    *  Represents a file/stream being transmitted or received
    */
    class FileBase {
    public:   
        typedef std::function<void(LibFlute::FileBase&, std::shared_ptr<std::map<uint16_t, std::vector<uint16_t>>>)> missing_callback_t;

        typedef std::function<void(std::shared_ptr<LibFlute::AlcPacket>)> receiver_callback_t;   
        /**
        *  Create a file from an FDT entry (used for reception)
        *
        *  @param entry FDT entry
        */
        FileBase(LibFlute::FileDeliveryTable::FileEntry entry);

        /**
        *  Create a file from the given parameters (used for transmission)
        *
        *  @param toi TOI of the file
        *  @param content_location Content location URI to use
        *  @param content_type MIME type
        *  @param expires Expiry value (in seconds since the NTP epoch)
        *  @param should_be_complete_at Expiry value (in seconds since the NTP epoch)
        *  @param data Pointer to the data buffer
        *  @param length Length of the buffer
        *  @param copy_data Copy the buffer. If false (the default), the caller must ensure the buffer remains valid 
        *                   while the file is being transmitted.
        */
        FileBase(uint32_t toi, 
            FecOti fec_oti,
            std::string content_location,
            std::string content_type,
            uint64_t expires,
            uint64_t should_be_complete_at,
            char* data,
            size_t length,
            bool copy_data = false,
            bool calculate_hash = true);

        /**
        *  Default destructor.
        */
        virtual ~FileBase();

        /**
        *  Check if the file is complete
        */
        bool complete() const;

        /**
        *  Get the data buffer length
        */
        size_t length() const;

        /**
        *  Get the FEC OTI values
        */
        FecOti& fec_oti();

        /**
        *  Get the file metadata from its FDT entry
        */
        LibFlute::FileDeliveryTable::FileEntry& meta();

        /**
        *  Timestamp of file reception
        */
        unsigned long received_at() const;

        void mark_complete();

        /**
        *  Set the FDT instance ID
        */
        void set_fdt_instance_id( uint16_t id);

        /**
        *  Get the FDT instance ID
        */
        uint16_t fdt_instance_id();

        void register_missing_callback(missing_callback_t cb);

        void register_receiver_callback(receiver_callback_t cb);

        std::map<uint16_t, LibFlute::SourceBlock> get_source_blocks();

        void retrieve_missing_parts();

        void push_alc_to_receive_buffer(const std::shared_ptr<AlcPacket>& alc);

        void process_receive_buffer();

        void start_receive_thread();

        void stop_receive_thread(bool should_join = true);

        void get_buffered_symbols(std::vector<EncodingSymbol>& symbols);

        void ignore_reception();

        uint64_t time_after_deadline();

        uint64_t time_before_deadline();

        virtual char* buffer() const;

        /**
        * Free the data buffer
        */
        virtual void free_buffer();

        /**
        *  Write the data from an encoding symbol into the appropriate place in the buffer
        */
        virtual void put_symbol(const EncodingSymbol& symbol);
        
        /**
        *  Get the next encoding symbols that fit in max_size bytes
        */
        std::vector<EncodingSymbol> get_next_symbols(size_t max_size);

        /**
        *  Mark encoding symbols as completed
        */
        void mark_completed(const std::vector<EncodingSymbol>& symbols, bool success);

        const std::unique_lock<LockableBase(std::mutex)> get_content_buffer_lock();

    protected:
        // More than one semaphore seems to drastically slow down the time it takes to create the blocks of one file.
        // Even though, this allows to create the blocks of multiple files in parallel, it is not worth it.
        static constexpr std::ptrdiff_t _max_create_block_threads{1}; // {1} for binary semaphore
        static std::counting_semaphore<_max_create_block_threads> _create_blocks_semaphore;

        static constexpr std::ptrdiff_t _max_process_symbol_threads{8}; // {1} for binary semaphore
        static std::counting_semaphore<_max_process_symbol_threads> _process_symbol_semaphore;

        void check_source_block_completion(LibFlute::SourceBlock& block);
        virtual void check_file_completion(bool check_hash = true, bool extract_data = true);

        void emit_missing_symbols();

        std::map<uint16_t, LibFlute::SourceBlock> _source_blocks;

        bool _complete = false;

        LibFlute::FileDeliveryTable::FileEntry _meta;
        unsigned long _received_at = 0;
        const uint64_t _retrieval_deadline;

        uint16_t _fdt_instance_id = 0;

        std::string _purpose = "unknown";

        missing_callback_t _missing_cb = nullptr;
        receiver_callback_t _receiver_cb = nullptr;

        TracyLockable(std::mutex, _receive_buffer_mutex);
        TracyLockable(std::mutex, _content_buffer_mutex);
        std::vector<std::shared_ptr<AlcPacket>> _alc_buffer;
        std::atomic<bool> _stop_receive_thread{true};
        std::jthread _receive_thread;

        // A bool wether or not this file should be ignored by the receiver.
        bool _ignore_reception = false;
    };
};