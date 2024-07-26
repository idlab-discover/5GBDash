#pragma once
#ifdef RAPTOR_ENABLED


#include "raptor.h"    // raptor lib
#include "Utils/flute_types.h"
#include "spdlog/spdlog.h"
#include "tinyxml2.h"
#include "Fec/FecTransformer.h"
#include <cstdlib>

namespace LibFlute {

    class RaptorFEC : public LibFlute::FecTransformer {

    private:

        bool is_encoder = true;

        unsigned int target_K(int blockno);

        LibFlute::SourceBlock::Symbol translate_symbol(uint16_t symbol_id, struct enc_context *encoder_ctx);

        LibFlute::SourceBlock create_block(char *buffer, int *bytes_read, uint16_t blockid);

        const float surplus_packet_ratio = 1.15; // adds 15% transmission overhead in exchange for protection against up to 15% packet loss. Assuming 1 symbol per packet, for smaller files packets may contain up to 10 symbols per packet but small files are much less vulnerable to packet loss anyways

        void extract_finished_block(LibFlute::SourceBlock& srcblk, struct dec_context *dc);

        uint32_t _max_source_block_length = 8191; // Maximum symbols per source block, should always be less then 8192 for raptor FEC Scheme 1

    public:

        RaptorFEC(unsigned int transfer_length, unsigned int max_payload, uint32_t max_source_block_length);

        RaptorFEC() {};

        ~RaptorFEC();

        bool check_source_block_completion(LibFlute::SourceBlock& srcblk);

        std::map<uint16_t, LibFlute::SourceBlock> create_blocks(char *buffer, int *bytes_read);

        bool process_symbol(LibFlute::SourceBlock& srcblk, LibFlute::SourceBlock::Symbol& symb, unsigned int id);

        bool calculate_partitioning();

        bool parse_fdt_info(tinyxml2::XMLElement *file, LibFlute::FecOti global_fec_oti);

        bool add_fdt_info(tinyxml2::XMLElement *file, LibFlute::FecOti global_fec_oti);

        void *allocate_file_buffer(int min_length);

        bool extract_file(std::map<uint16_t, LibFlute::SourceBlock> blocks);

        void set_max_source_block_length(uint32_t max_source_block_length);

        uint32_t get_source_block_length(uint16_t block_id);

        void discard_decoder(uint16_t block_id);

        std::map<uint16_t, struct dec_context* > decoders; // map of source block number to decoders

        uint32_t nof_source_symbols = 0;
        uint32_t nof_source_blocks = 0;
        uint32_t large_source_block_length = 0;
        uint32_t small_source_block_length = 0;
        uint32_t nof_large_source_blocks = 0;

        unsigned int F; // object size in bytes
        unsigned int Al = 4; // symbol alignment: 4 => from RFC 5053: 4.2 Example Parameter Derivation Algorithm (change this in Transmitter.cpp and Retriever.cpp as well)
        unsigned int T; // symbol size in bytes
        unsigned long W = 16*1024*1024; // target on sub block size - set default to 16 MB, to keep the number of sub-blocks, N, = 1 (you probably only need W >= 11MB to achieve this, assuming an ethernet mtu of ~1500 bytes, but we round to the nearest power of 2)
        unsigned int G; // number of symbols per packet
        unsigned int Z; // number of source blocks
        unsigned int N; // number of sub-blocks per source block
        unsigned int K; // number of symbols in a source block
        unsigned int Kt; // total number of symbols
        unsigned int P; // maximum payload size: e.g. 1436 for ipv4 over 802.3

    };

}

#endif

