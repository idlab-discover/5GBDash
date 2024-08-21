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
	<< "Usage: StreamConcat <Source> <Inject> <Output> <TempId|File> [<Codec>]" << std::endl
  << std::endl
	<< "    <Source>                       input .264/.265/.266 Annex B stream" << std::endl
	<< "    <Inject>                       input .264/.265/.266 Annex B stream" << std::endl
	<< "    <Output>                       output .264/.265/.266 Annex B stream" << std::endl
	<< "    <TempId|File>                  Temporal layers to switch (<TempId from Source, >=TempId from Inject) or file with frame numbers" << std::endl
	<< "    <Codec = 0>                    0: H.264/AVC; 1: H.265/HEVC; 2:H.266/VVC" << std::endl
	<< std::endl;
  // clang-format on
}
enum Mode { TEMPORAL, FILEBASED };

int main(int argc, char *argv[]) {
  std::string inputfilename1;
  std::string inputfilename2;
  std::string inputfilename_frame;
  std::string outputfilename;
  Mode mode = TEMPORAL;
  int temp_id = 0;

  CodecType codec = CODEC_AVC;
  // Check command line arguments
  int returnval = 0;
  if (argc != 5 && argc != 6) {
    std::cout << "Incorrect number of arguments!" << std::endl;
    returnval = -1;
  } else {
    // Open IO files
    inputfilename1 = std::string(argv[1]);
    inputfilename2 = std::string(argv[2]);
    outputfilename = std::string(argv[3]);
    if (isdigit(argv[4][0])) {
      temp_id = atoi(argv[4]);
      mode = TEMPORAL;
    } else {
      inputfilename_frame = std::string(argv[4]);
      mode = FILEBASED;
    }

    codec = CODEC_AVC;
    if (argc >= 6) {
      if (isdigit(argv[5][0])) {
        codec = (CodecType)atoi(argv[5]);
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

  std::ifstream inputfile_frame(inputfilename_frame);
  std::istream_iterator<int> start(inputfile_frame), end;
  std::vector<int> replaceframes(start, end);
  // std::sort(replaceframes.begin(), replaceframes.end());
  
  // Store input in vectors for easy processing
  std::vector<uint8_t> bufferinputfile1;
  std::vector<uint8_t> bufferinputfile2;

  read_file_to_vector(inputfilename1, bufferinputfile1);
  read_file_to_vector(inputfilename2, bufferinputfile2);

  std::vector<Nalu> bufferinputnalu1;
  std::vector<Nalu> bufferinputnalu2;

  vector_to_nalu_vector(bufferinputfile1, bufferinputnalu1, codec);
  vector_to_nalu_vector(bufferinputfile2, bufferinputnalu2, codec);

  

  // bufferAPS contains APSs with their type and id as int identifier
  std::map<int, Nalu> bufferapssource;
  std::map<int, Nalu> bufferapsvclsource;
  std::map<int, Nalu> bufferapsinject;
  std::map<int, Nalu> bufferapsvclinject;

  std::vector<Nalu> bufferoutputnalu;



  int nalu1_idx = 0;
  int nalu2_idx = 0;

  int vcl_idx = 0;

  int common_counter = 0;

  for (int i = 0; i < bufferinputnalu1.size() + bufferinputnalu2.size() - common_counter; ++i) {

    if (bufferinputnalu1[nalu1_idx].type == VCL) {
      if (std::find(replaceframes.begin(), replaceframes.end(), vcl_idx) ==
          replaceframes.end()) { // If not in list, then push
        // Push the actual VCL from source 1
        bufferoutputnalu.push_back(bufferinputnalu1[nalu1_idx]);
        ++nalu1_idx;
      } else {
        // Push the actual VCL from source 2
        bufferoutputnalu.push_back(bufferinputnalu2[nalu2_idx]);
        ++nalu2_idx;
      }
      
      ++vcl_idx;
    } else {
      bufferoutputnalu.push_back(bufferinputnalu1[nalu1_idx]);
      ++nalu1_idx;
      ++nalu2_idx;
      ++common_counter;
    }

  }

  write_nalu_vector_to_file(outputfilename, bufferoutputnalu);

  return returnval;
}
