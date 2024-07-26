#pragma once

#include "Utils/flute_types.h"
#include "spdlog/spdlog.h"
#include "tinyxml2.h"
#include <cstdlib>
#include "map"

namespace LibFlute {
/**
   *  abstract class for FEC Object En/De-coding
   */
    class FecTransformer {

        public:

            virtual ~FecTransformer() = default;
            /**
             * @brief Attempt to decode a source block
             *
             * @param srcblk the source block that should be decoded
             * @return whether or not the decoding was successful
             */
            virtual bool check_source_block_completion(LibFlute::SourceBlock& srcblk) = 0;

            /**
             * @brief Encode a file into multiple source blocks
             *
             * @param buffer a pointer to the buffer containing the data
             * @param bytes_read a pointer to an integer to store the number of bytes read out of buffer
             * @return a map of source blocks that the object has been encoded to
             */
            virtual std::map<uint16_t, LibFlute::SourceBlock> create_blocks(char *buffer, int *bytes_read) = 0;

            /**
             * @brief Process a received symbol
             *
             * @param srcblk the source block this symbols corresponds to
             * @param symb the received symbol
             * @param id the symbols id
             * @return success or failure
             */
            virtual bool process_symbol(LibFlute::SourceBlock& srcblk, LibFlute::SourceBlock::Symbol& symb, unsigned int id) = 0;

            virtual bool calculate_partitioning() = 0;

            /**
             * @brief Attempt to parse relevent information for decoding from the FDT
             *
             * @return success status
             */
            virtual bool parse_fdt_info(tinyxml2::XMLElement *file, LibFlute::FecOti global_fec_oti) = 0;

            /**
             * @brief Add relevant information about the FEC Scheme which the decoder may need, to the FDT
             *
             * @return success status
             */
            virtual bool add_fdt_info(tinyxml2::XMLElement *file, LibFlute::FecOti global_fec_oti) = 0;

            /**
             * @brief Allocate the size of the buffer needed for this encoding scheme (since it may be larger)
             *
             * @param min_length this should be the size of the file (transfer length). This determines the minimum size of the returned buffer
             * @return 0 on failure, otherwise return a pointer to the buffer
             */
            virtual void *allocate_file_buffer(int min_length) = 0;

            /**
             * @brief Called after the file is marked as complete, to finish extraction/decoding (if necessary)
             *
             * @param blocks the source blocks of the file, stored in the File object
             */
            virtual bool extract_file(std::map<uint16_t, LibFlute::SourceBlock> blocks) = 0;

            /**
             * @brief Set the maximum source block length (number of symbols per source block)
             *
             * @param max_source_block_length the maximum source block length
             */
            virtual void set_max_source_block_length(uint32_t max_source_block_length) = 0;

            /**
             * @brief Get the source block length (number of symbols per source block)
             *
             * @param block_id the source block id
             * @return the source block length
             */
            virtual uint32_t get_source_block_length(uint16_t block_id) = 0;

            virtual void discard_decoder(uint16_t block_id) = 0;

            uint32_t nof_source_symbols = 0;
            uint32_t nof_source_blocks = 0;
            uint32_t large_source_block_length = 0;
            uint32_t small_source_block_length = 0;
            uint32_t nof_large_source_blocks = 0;

    };
};
