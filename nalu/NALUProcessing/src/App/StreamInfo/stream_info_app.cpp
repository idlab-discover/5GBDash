#include "nalu_processing_lib.h"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <iostream>
#include <map>
#include <vector>

void print_help() {
  // clang-format off
  std::cout
	<< "Usage: StreamInfo <Source> [<Codec>]" << std::endl
  << std::endl
	<< "    <Source>                       input .264/.265/.266 Annex B stream" << std::endl
	<< "    <Codec = 0>                    0: H.264/AVC; 1: H.265/HEVC; 2:H.266/VVC" << std::endl
	<< std::endl;
  // clang-format on
}


const char* pict_type_to_string(PictType pict_type) {
    switch (pict_type) {
        case PICT_P: return "P";
        case PICT_B: return "B";
        case PICT_I: return "I";
        case PICT_SP: return "SP";
        case PICT_SI: return "SI";
        case PICT_UNKNOWN: // fall-through
        default: return "Unknown";
    }
}

int main(int argc, char *argv[]) {
  std::string inputfilename1;
  CodecType codec = CODEC_AVC;
  // Check command line arguments
  int returnval = 0;
  if (argc != 2 && argc != 3) {
    std::cout << "Incorrect number of arguments!" << std::endl;
    returnval = -1;
  } else {
    // Open IO files
    inputfilename1 = std::string(argv[1]);

    codec = CODEC_AVC;
    if (argc >= 3) {
      if (isdigit(argv[2][0])) {
        codec = (CodecType)atoi(argv[2]);
      } else {
        std::cout << "Argument Codec is not integer!" << std::endl;
        returnval = -1;
      }
    }
  }
  if (returnval < 0) {
    std::cout << std::endl;
    print_help();
    return returnval;
  }
  
  // Store input in vectors for easy processing
  std::vector<uint8_t> bufferinputfile1;

  read_file_to_vector(inputfilename1, bufferinputfile1);

  std::vector<Nalu> bufferinputnalu1;

  vector_to_nalu_vector(bufferinputfile1, bufferinputnalu1, codec);


  int vcl_idx = 0;
  bool first = true;

  std::cout << "{\n    \"frames\": [\n";

  for (int i = 0; i < bufferinputnalu1.size(); ++i) {

    if (bufferinputnalu1[i].type != VCL) { 
      continue;
    }

    if (!first) {
        std::cout << ",\n";
    }
    first = false;

    std::cout << "        {\n";
    std::cout << "            \"coded_picture_number\": " << vcl_idx << ",\n";
    std::cout << "            \"pict_type\": \"" << pict_type_to_string(bufferinputnalu1[i].pict_type) << "\"\n";
    std::cout << "        }";

    ++vcl_idx;
  }

  std::cout << "\n    ]\n";
  std::cout << "}\n";

  return returnval;
}
