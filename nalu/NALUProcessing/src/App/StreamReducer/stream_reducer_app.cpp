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
	<< "Usage: StreamReducer <Source> <Inject> <Output> <TempId|File>" << std::endl
  << std::endl
	<< "    <Source>                       input .264/.265/.266 Annex B stream" << std::endl
	<< "    <Output>                       output .264/.265/.266 Annex B stream" << std::endl
	<< "    <TempId|File>                  Temporal layers to switch (<TempId from Source, >=TempId from Inject) or file with frame numbers" << std::endl
	<< std::endl;
  // clang-format on
}
enum Mode { TEMPORAL, FILEBASED };

int main(int argc, char *argv[]) {
  std::string inputfilename;
  std::string inputfilename_frame;
  std::string outputfilename;
  Mode mode = TEMPORAL;
  int temp_id = 0;

  CodecType codec = CODEC_AVC;
  // Check command line arguments
  int returnval = 0;
  if (argc != 4) {
    std::cout << "Incorrect number of arguments!" << std::endl;
    returnval = -1;
  } else {
    // Open IO files
    inputfilename = std::string(argv[1]);
    outputfilename = std::string(argv[2]);
    if (isdigit(argv[3][0])) {
      temp_id = atoi(argv[3]);
      mode = TEMPORAL;
    } else {
      inputfilename_frame = std::string(argv[3]);
      mode = FILEBASED;
    }
  }
  if (returnval < 0) {
    std::cout << std::endl;
    print_help();
    return returnval;
  }

  std::ifstream inputfile_frame(inputfilename_frame);
  std::istream_iterator<int> start(inputfile_frame), end;
  std::vector<int> retainFrames(start, end);
  
  // Store input in vectors for easy processing
  std::vector<uint8_t> bufferinputfile;

  read_file_to_vector(inputfilename, bufferinputfile);

  std::vector<Nalu> bufferinputnalu;

  vector_to_nalu_vector(bufferinputfile, bufferinputnalu, codec);

  std::vector<Nalu> bufferoutputnalu;

  int vcl_idx = 0;
  for (const auto& nalu : bufferinputnalu) {
    if (nalu.type == VCL) {
      // store VCL in output if id is within the retainFrames list
      if (std::find(retainFrames.begin(), retainFrames.end(), vcl_idx) !=
          retainFrames.end()) {
        // Push the actual VCL
        bufferoutputnalu.push_back(nalu);
      }
      ++vcl_idx;
    } else {
      bufferoutputnalu.push_back(nalu);
    }
  }

  write_nalu_vector_to_file(outputfilename, bufferoutputnalu);

  return returnval;
}
