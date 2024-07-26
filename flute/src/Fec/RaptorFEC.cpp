#ifdef RAPTOR_ENABLED

#include "Fec/RaptorFEC.h"

#include "public/tracy/Tracy.hpp"

LibFlute::RaptorFEC::RaptorFEC(unsigned int transfer_length, unsigned int max_payload, uint32_t max_source_block_length) 
    : F(transfer_length)
    , P(max_payload)
{
  // rfc5581: 6.2.1.2: MSBL: A non-netative integer less then 8192 for FEC Scheme 1 in units of symbols.
  _max_source_block_length = max_source_block_length > 8192 ? 8191 : max_source_block_length;
  //_max_source_block_length = 8192;

  uint32_t Kmin = 148; // minimum number of source symbols

  double g = fmin( fmin(ceil((double)P*Kmin/(double)F), (double)P/(double)Al), 10.0f);
  G = (unsigned int) g;

  T = (unsigned int) floor((double)P/(double)(Al*g)) * Al;

  if (T % Al){
    spdlog::error(" Symbol size T should be a multiple of symbol alignment parameter Al");
    throw "Symbol size does not align";
  }

  Kt = ceil((double)F/(double)T); // total symbols

  if (Kt < 4){
    spdlog::error("Input file is too small, it must be a minimum of 4 Symbols");
    throw "Input is less than 4 symbols";
  }

  Z = (unsigned int) ceil((double)Kt/(double)_max_source_block_length);

  K = (Kt > _max_source_block_length) ? _max_source_block_length : (unsigned int) Kt; // symbols per source block

  _max_source_block_length = K; // Overwrite the max source block length to be the actual value, since it may be smaller than the value passed in

  N = fmin( ceil( ceil((double)Kt/(double)Z) * (double)T/(double)W ) , (double)T/(double)Al );

  // Set the values that the File class may need:
  nof_source_symbols = (unsigned int) Kt;
  nof_source_blocks = Z;

  small_source_block_length = (Z * K - nof_source_symbols) * T; // = (number of symbols in the final (small) source block, if nof_source_symbols isnt cleanly divisible by Z * K ) * symbol size

  // open question as to how we define "large source blocks" because either none of the remaining "regular" blocks are large, or all of them are, since raptor has a fixed block size

  /*
  nof_large_source_blocks = K - (small_source_block_length != 0); // if we define a "large" source block as a normal one then its just the nof "regular" source blocks minus the nof small ones (which is either one or zero)
  large_source_block_length = K * T;
  */

  nof_large_source_blocks = 0; //for now argue that there are no "large" blocks, only regular and small ones
  large_source_block_length = 0;
}

LibFlute::RaptorFEC::~RaptorFEC() {
  for(auto iter = decoders.begin(); iter != decoders.end(); iter++){
    free_decoder_context(iter->second);
  }
}

void LibFlute::RaptorFEC::set_max_source_block_length(uint32_t max_source_block_length) {
  // rfc5581: 6.2.1.2: MSBL: A non-netative integer less then 8192 for FEC Scheme 1 in units of symbols.
  _max_source_block_length = max_source_block_length >= 8192 ? 8191 : max_source_block_length;
  //_max_source_block_length = 8192;
}

bool LibFlute::RaptorFEC::calculate_partitioning() {
  return true;
}

void LibFlute::RaptorFEC::extract_finished_block(LibFlute::SourceBlock& srcblk, struct dec_context *dc) {
    if(!dc || dc->pp == NULL) {
        return;
    }
    for(auto iter = srcblk.symbols.begin(); iter != srcblk.symbols.end(); iter++) {
      // Check if the symbol index is valid in dc->pp
      if (dc->pp[iter->first] != NULL)
      {
        // Verify data sizes and copy source data to the destination buffer
        memcpy(iter->second.data, dc->pp[iter->first], T);
      } else {
        // Handle the case where the symbol index is not found in dc->pp
        spdlog::warn("[DECODER] Symbol index {} not found in dec_context", iter->first);
        throw "Symbol index not found in dec_context";
      }
    }
   spdlog::debug("[DECODER] Raptor: finished decoding source block {}",srcblk.id);
}

void *LibFlute::RaptorFEC::allocate_file_buffer(int min_length){
  // min length should be exactly Z*K*T, so including repair symbols we should have a larger value
  auto length = Z*target_K(0)*T;
  if (min_length > length){
    spdlog::error("[DECODER] Raptor FEC: min_length is larger than the maximum possible file size");
    throw "Raptor FEC: min_length is larger than the maximum possible file size";
  }
  auto buffer = malloc(length);
  //TracyAlloc(buffer, length);
  return buffer;
}

uint32_t LibFlute::RaptorFEC::get_source_block_length(uint16_t block_id) {
  if (block_id < Z - 1) {
    return K;
  }
  // The last block will usually be smaller than the normal block size, unless the file size is an exact multiple
  return Kt - K*(Z-1);
}

bool LibFlute::RaptorFEC::process_symbol(LibFlute::SourceBlock& srcblk, LibFlute::SourceBlock::Symbol& symbol, unsigned int id) {
  ZoneScopedN("RaptorFEC::process_symbol");
  if (symbol.length != T){ // symbol.length should always be the symbol size, T
    spdlog::info("[DECODER] SBN {}, ESI {}, T {}, symbol_length {}",srcblk.id,id, T, symbol.length);
    spdlog::error("[DECODER] Symbol length is not equal to T");
    throw "Symbol length is not equal to T";
  }
  struct dec_context *dc = decoders[srcblk.id];
  int nsymbs = get_source_block_length(srcblk.id);
  /*
  if (id >= nsymbs){
    spdlog::debug("[DECODER] Symbol index {} is out of range for {} source symbols, this is a repair symbol for block {}", id, nsymbs, srcblk.id);
  }
  */
  if (!dc) {
    int blocksize = (srcblk.id < Z - 1) ? K*T : F - K*T*(Z-1);
    // spdlog::debug("[DECODER] Preparing decoder context for block {} with {} symbols and blocksize {}", srcblk.id, nsymbs, blocksize);
    struct enc_context *sc = create_encoder_context(NULL, nsymbs, T, blocksize, srcblk.id);
    //struct enc_context *sc = create_encoder_context(NULL, K, T, K*T, srcblk.id); // the "length" will always be K*T from the decoders perspective
    dc = create_decoder_context(sc);
    decoders[srcblk.id] = dc;
  }
  if (dc->finished){
    spdlog::debug("[DECODER] Skipped processing of symbol for finished block : SBN {}, ESI {}",srcblk.id,id);
    return true;
  }
  auto * pkt = (struct LT_packet *) calloc(1, sizeof(struct LT_packet));
  //TracyAlloc(pkt, sizeof(struct LT_packet));
  pkt->id = id;
  pkt->syms = (GF_ELEMENT *) malloc(symbol.length * sizeof(char)); // NOLINT
  //TracyAlloc(pkt->syms, symbol.length * sizeof(char));
  memcpy(pkt->syms, symbol.data, symbol.length * sizeof(char));

  process_LT_packet(dc, pkt);
  //TracyFree(pkt->syms);
  //TracyFree(pkt);
  free_LT_packet(pkt);
  return true;
}

void LibFlute::RaptorFEC::discard_decoder(uint16_t block_id) {
  if (decoders[block_id]) {
    free_decoder_context(decoders[block_id]);
    decoders.erase(block_id);
  }
}

bool LibFlute::RaptorFEC::extract_file(std::map<uint16_t, LibFlute::SourceBlock> blocks) {
    for(auto iter = blocks.begin(); iter != blocks.end(); iter++) {
      try {
        extract_finished_block(iter->second,decoders[iter->second.id]);
      } catch (const char* msg) {
        spdlog::error("[DECODER] Error extracting file block: {}", msg);
        return false;
      }
    }
    return true;
}

bool LibFlute::RaptorFEC::check_source_block_completion(LibFlute::SourceBlock& srcblk) {
  if (is_encoder) {
    // check source block completion for the Encoder
    bool complete = std::all_of(srcblk.symbols.begin(), srcblk.symbols.end(), [](const auto& symbol){ return symbol.second.complete; });

    if(complete)
        std::for_each(srcblk.symbols.begin(), srcblk.symbols.end(), [](const auto& symbol){ delete[] symbol.second.data; });

    return complete;
  }
  // else case- we are the Decoder

  if(!srcblk.symbols.size()){
    spdlog::warn("[DECODER] Empty source block (size 0) SBN {}",srcblk.id);
    return false;
  }

  struct dec_context *dc = decoders[srcblk.id];
  if (!dc) {
    spdlog::error("[DECODER] Couldn't find raptor decoder for source block {}",srcblk.id);
    return false;
  }
  return dc->finished;
}

unsigned int LibFlute::RaptorFEC::target_K(int blockno) {
    // always send at least one repair symbol
  if (blockno < Z-1) {
      int target = K * surplus_packet_ratio;
      return (target > K) ? target : K + 1;
  }
  // last block gets special treatment
  int remaining_symbs = Kt - K*(Z-1);
  return (remaining_symbs + 1 > remaining_symbs*surplus_packet_ratio) ? remaining_symbs + 1 : remaining_symbs * surplus_packet_ratio;
}

LibFlute::SourceBlock::Symbol LibFlute::RaptorFEC::translate_symbol(uint16_t symbol_id, struct enc_context *encoder_ctx){
    ZoneScopedN("RaptorFEC::translate_symbol");
    struct LT_packet *lt_packet = encode_LT_packet(encoder_ctx);
    LibFlute::SourceBlock::Symbol symbol { .id = symbol_id, .data = new char[T], .length = T, .complete = false};

    memcpy(symbol.data, lt_packet->syms, T);

    free_LT_packet(lt_packet);
    return symbol;
}

LibFlute::SourceBlock LibFlute::RaptorFEC::create_block(char *buffer, int *bytes_read, uint16_t blockid) {
    ZoneScopedN("RaptorFEC::create_block");
    int seed = blockid;
    int nsymbs = get_source_block_length(blockid);
    int blocksize = (blockid < Z - 1) ? K*T : F - K*T*(Z-1); // the last block will usually be smaller than the normal block size, unless the file size is an exact multiple
    struct enc_context *encoder_ctx = create_encoder_context((unsigned char *)buffer, nsymbs , T, blocksize, seed);
    //    struct enc_context *encoder_ctx = create_encoder_context((unsigned char *)buffer, K, T, blocksize, seed);
    if (!encoder_ctx) {
        spdlog::error("[ENCODER] Error creating encoder context");
        throw "Error creating encoder context";
    }
    unsigned int symbols_to_read = target_K(blockid);

    LibFlute::SourceBlock source_block{
      .id = blockid,
      .complete = false,
      .length = T * symbols_to_read,
      .symbols = {}};

    for(uint16_t symbol_id = 0; symbol_id < symbols_to_read; symbol_id++) {
        source_block.symbols[symbol_id] = translate_symbol(symbol_id, encoder_ctx);
    }
    *bytes_read += blocksize;
    // spdlog::debug("[ENCODER] Created block {} with {} symbols for total blocksize {}", blockid, nsymbs, blocksize);

    free_encoder_context(encoder_ctx);

    return source_block;
}


std::map<uint16_t, LibFlute::SourceBlock> LibFlute::RaptorFEC::create_blocks(char *buffer, int *bytes_read) {
  if(!bytes_read) {
    throw std::invalid_argument("bytes_read pointer shouldn't be null");
  } else if(N != 1) {
    throw std::invalid_argument("Currently the encoding only supports 1 sub-block per block");
  }

  ZoneScopedN("RaptorFEC::create_blocks");

  std::map<uint16_t, LibFlute::SourceBlock> block_map;
  *bytes_read = 0;

  for(uint16_t src_blocks = 0; src_blocks < Z; src_blocks++) {
    if(!is_encoder) {
      unsigned int symbols_to_read = target_K(src_blocks);
      LibFlute::SourceBlock block{
      .id = src_blocks,
      .complete = false,
      .length = T * symbols_to_read,
      .symbols = {}};
      for (uint16_t i = 0; i < symbols_to_read; i++) {
        block.symbols[i] = LibFlute::SourceBlock::Symbol {.id = i, .data = buffer + src_blocks*K*T + T*i, .length = T, .complete = false};
      }
      block.id = src_blocks;
      block_map[src_blocks] = block;
    } else {
      block_map[src_blocks] = create_block(&buffer[*bytes_read], bytes_read, src_blocks);
    }
  }
  return block_map;
}


bool LibFlute::RaptorFEC::parse_fdt_info(tinyxml2::XMLElement *file, LibFlute::FecOti global_fec_oti) {
  is_encoder = false;

  const char* val = 0;
  val = file->Attribute("Transfer-Length");
  if (val != nullptr) {
    F = strtoul(val, nullptr, 0);
  } else {
    val = file->Attribute("Content-Length");
    if (val != nullptr) {
      F = strtoul(val, nullptr, 0);
    } else {
      throw "Required field \"Transfer-Length\" is missing for an object in the FDT";
    }
  }

  val = file->Attribute("FEC-OTI-Number-Of-Source-Blocks");
  if (val != nullptr) {
    Z = strtoul(val, nullptr, 0);
  } else {
    throw "Required field \"FEC-OTI-Number-Of-Source-Blocks\" is missing for an object in the FDT";
  }

  val = file->Attribute("FEC-OTI-Number-Of-Sub-Blocks");
  if (val != nullptr) {
    N = strtoul(val, nullptr, 0);
  } else {
    throw "Required field \"FEC-OTI-Number-Of-Sub-Blocks\" is missing for an object in the FDT";
  }

  val = file->Attribute("FEC-OTI-Encoding-Symbol-Length");
  if (val != nullptr) {
    T = strtoul(val, nullptr, 0);
  } else if (global_fec_oti.encoding_symbol_length != 0) {
    T = global_fec_oti.encoding_symbol_length;
  } else {
    throw "Required field \"FEC-OTI-Encoding-Symbol-Length\" is missing for an object in the FDT";
  }

  val = file->Attribute("FEC-OTI-Symbol-Alignment-Parameter");
  if (val != nullptr) {
    Al = strtoul(val, nullptr, 0);
  } else {
    throw "Required field \"FEC-OTI-Symbol-Alignment-Parameter\" is missing for an object in the FDT";
  }

  if (T % Al) {
    throw "Symbol size T is not a multiple of Al. Invalid configuration from sender";
  }

  val = file->Attribute("FEC-OTI-Maximum-Source-Block-Length");
  if (val != nullptr) {
    // Note that it could also be set from an attribute of fdt-instance, this is done where this function is called.
    set_max_source_block_length(strtoul(val, nullptr, 0));
  } else if (global_fec_oti.max_source_block_length != 0) {
    set_max_source_block_length(global_fec_oti.max_source_block_length);
  }

  // Set the values that are missing that we or the File class may need, follows the same logic as in calculate_partitioning()
  nof_source_symbols = ceil((double)F / (double)T);
  K = (nof_source_symbols > _max_source_block_length) ? _max_source_block_length : nof_source_symbols;
  Kt = ceil((double)F/(double)T); // total symbols

  nof_source_blocks = Z;
  small_source_block_length = (Z * K - nof_source_symbols) * T;
  nof_large_source_blocks = 0;
  large_source_block_length = 0;

  return true;
}

bool LibFlute::RaptorFEC::add_fdt_info(tinyxml2::XMLElement *file, LibFlute::FecOti global_fec_oti) {
  if (global_fec_oti.encoding_id != FecScheme::Raptor) {
    file->SetAttribute("FEC-OTI-FEC-Encoding-ID", (unsigned) FecScheme::Raptor);
  }
  if (global_fec_oti.max_source_block_length != _max_source_block_length) {
    file->SetAttribute("FEC-OTI-Maximum-Source-Block-Length", _max_source_block_length);
  }
  if (global_fec_oti.encoding_symbol_length != T) {
    file->SetAttribute("FEC-OTI-Encoding-Symbol-Length", T);
  }
  file->SetAttribute("FEC-OTI-Symbol-Alignment-Parameter", Al);
  file->SetAttribute("FEC-OTI-Number-Of-Source-Blocks", Z);
  file->SetAttribute("FEC-OTI-Number-Of-Sub-Blocks", N);
  file->SetAttribute("FEC-OTI-Symbol-Alignment-Parameter", Al);

  is_encoder = true;

  return true;
}

#endif