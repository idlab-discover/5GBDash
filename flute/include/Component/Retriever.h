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
#pragma once
#include <queue>
#include <string>
#include <map>
#include <mutex>
#include "Object/FileBase.h"
#include "Packet/AlcPacket.h"
#include "Object/FileDeliveryTable.h"
#include "Utils/flute_types.h"

namespace LibFlute {
  /**
   *  FLUTE transmitter class. Construct an instance of this to send data through a FLUTE/ALC session.
   */
  class Retriever {
    public:
     /**
      *  Default constructor.
      *
      *  @param mtu Path MTU to size FLUTE packets for 
      */
      Retriever(uint64_t tsi, unsigned short mtu, FecScheme _fec_scheme);

     /**
      *  Default destructor.
      */
      virtual ~Retriever();

     /**
      *  Transmit a file. 
      *  The caller must ensure the data buffer passed here remains valid until the completion callback 
      *  for this file is called.
      *
      *  @param content_location URI to set in the content location field of the generated FDT entry
      *  @param content_type MIME type to set in the content type field of the generated FDT entry
      *  @param expires Expiry timestamp (based on NTP epoch)
      *  @param data Pointer to the data buffer (managed by caller)
      *  @param length Length of the data buffer (in bytes)
      *
      *  @return TOI of the file
      */
      std::string get_alcs(const std::string& content_location,
          const std::string& content_type,
          uint32_t expires,
          char* data,
          size_t length,
          uint64_t toi,
          std::map<uint32_t,std::vector<uint32_t>> search_map);

      std::string get_alcs_from_file(
          std::shared_ptr<FileBase> file,
          std::map<uint32_t,std::vector<uint32_t>> search_map);

     /**
      *  Convenience function to get the current timestamp for expiry calculation
      *
      *  @return seconds since the NTP epoch
      */
      uint64_t seconds_since_epoch();

      /**
       * Returns the FEC Scheme used by the Retriever
      */
      FecScheme get_fec_scheme() { return _fec_oti.encoding_id; }

    private:
      uint64_t _tsi;
      uint16_t _mtu;

      std::unique_ptr<LibFlute::FileDeliveryTable> _fdt;

      unsigned _fdt_repeat_interval = 5;

      uint32_t _max_payload;
      FecOti _fec_oti{};
  };
};
