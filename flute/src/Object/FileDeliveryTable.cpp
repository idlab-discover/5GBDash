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
#include "Object/FileDeliveryTable.h"
#include "tinyxml2.h" 
#include <iostream>
#include <string>
#include "spdlog/spdlog.h"

#ifdef RAPTOR_ENABLED
#include "Fec/RaptorFEC.h"
#endif


LibFlute::FileDeliveryTable::FileDeliveryTable(uint32_t instance_id, FecOti fec_oti)
  : _instance_id( instance_id )
  , _global_fec_oti( fec_oti )
{}

LibFlute::FileDeliveryTable::~FileDeliveryTable() {
}

LibFlute::FileDeliveryTable::FileDeliveryTable(uint32_t instance_id, char* buffer, size_t len) 
  : _instance_id( instance_id )
{
  tinyxml2::XMLDocument doc(true, tinyxml2::COLLAPSE_WHITESPACE);
  doc.Parse(buffer, len);
  auto fdt_instance = doc.FirstChildElement("FDT-Instance");
  if (fdt_instance == nullptr) {
    spdlog::info("[RECEIVE] ERROR: {}", buffer);
    throw "Missing FDT-Instance element";
  }
  _expires = std::stoull(fdt_instance->Attribute("Expires"));

  spdlog::debug("[RECEIVE] Received new FDT with instance ID {}", instance_id);
  // spdlog::debug("[RECEIVE] FDT content:\n{}", std::string(buffer, len));

  _global_fec_oti.encoding_id = FecScheme::CompactNoCode;
  auto val = fdt_instance->Attribute("FEC-OTI-FEC-Encoding-ID");
  if (val != nullptr) {
    _global_fec_oti.encoding_id = static_cast<FecScheme>(strtoul(val, nullptr, 0));
  }

  _global_fec_oti.max_source_block_length = 0;
  val = fdt_instance->Attribute("FEC-OTI-Maximum-Source-Block-Length");
  if (val != nullptr) {
    _global_fec_oti.max_source_block_length = strtoul(val, nullptr, 0);
  }

  _global_fec_oti.encoding_symbol_length = 0;
  val = fdt_instance->Attribute("FEC-OTI-Encoding-Symbol-Length");
  if (val != nullptr) {
    _global_fec_oti.encoding_symbol_length = strtoul(val, nullptr, 0);
  }

  for (auto file = fdt_instance->FirstChildElement("File"); 
      file != nullptr; file = file->NextSiblingElement("File")) {
    try {
      // required attributes
      auto toi_str = file->Attribute("TOI");
      if (toi_str == nullptr) {
        throw "Missing TOI attribute on File element";
      }
      uint32_t toi = strtoull(toi_str, nullptr, 0);

      auto content_location = file->Attribute("Content-Location");
      if (content_location == nullptr) {
        throw "Missing Content-Location attribute on File element";
      }

      uint32_t content_length = 0;
      val = file->Attribute("Content-Length");
      if (val != nullptr) {
        content_length = strtoull(val, nullptr, 0);
      }

      uint32_t transfer_length = 0;
      val = file->Attribute("Transfer-Length");
      if (val != nullptr) {
        transfer_length = strtoull(val, nullptr, 0);
      } else {
        transfer_length = content_length;
      }

      auto content_md5 = file->Attribute("Content-MD5");
      if (!content_md5) {
        content_md5 = "";
      }

      auto content_type = file->Attribute("Content-Type");
      if (!content_type) {
        content_type = "";
      }

      auto encoding_id = _global_fec_oti.encoding_id;
      val = file->Attribute("FEC-OTI-FEC-Encoding-ID");
      if (val != nullptr) {
        encoding_id = static_cast<FecScheme>(strtoul(val, nullptr, 0));
      }

      auto max_source_block_length = _global_fec_oti.max_source_block_length;
      val = file->Attribute("FEC-OTI-Maximum-Source-Block-Length");
      if (val != nullptr) {
        max_source_block_length = strtoul(val, nullptr, 0);
      }

      auto encoding_symbol_length = _global_fec_oti.encoding_symbol_length;
      val = file->Attribute("FEC-OTI-Encoding-Symbol-Length");
      if (val != nullptr) {
        encoding_symbol_length = strtoul(val, nullptr, 0);
      }

      LibFlute::FecTransformer *fec_transformer = 0;

      switch (encoding_id){
#ifdef RAPTOR_ENABLED
        case FecScheme::Raptor:
          fec_transformer = new RaptorFEC(); // corresponding delete calls in Receiver.cpp and destuctor function
          fec_transformer->set_max_source_block_length(max_source_block_length);
          // spdlog::debug("[RECEIVE] Received FDT entry for a raptor encoded file");
          break;
#endif
        default:
          break;
      }

      if (fec_transformer && !fec_transformer->parse_fdt_info(file, _global_fec_oti)) {
        throw "Failed to parse fdt info for specific FEC data";
      }

      uint32_t expires = 0;
      auto cc = file->FirstChildElement("mbms2007:Cache-Control");
      if (cc) {
        auto expires_elem = cc->FirstChildElement("mbms2007:Expires");
        if (expires_elem) {
          expires = strtoul(expires_elem->GetText(), nullptr, 0);
        }
      }
      uint64_t deadline = 0;
      auto r = file->FirstChildElement("mbms2007:Recover");
      if (r) {
        auto d = r->FirstChildElement("mbms2007:Deadline");
        if (d) {
          deadline = strtoul(d->GetText(), nullptr, 0);
        }
      }

      uint32_t stream_id = 0;
      auto si = file->FirstChildElement("mbms2007:Stream");
      if (si) {
        auto si_id = si->FirstChildElement("mbms2007:Id");
        if (si_id) {
          stream_id = strtoul(si_id->GetText(), nullptr, 0);
        }
      }

      FecOti fec_oti{
        .encoding_id = (FecScheme)encoding_id,
        .transfer_length =  transfer_length,
        .encoding_symbol_length = encoding_symbol_length,
        .max_source_block_length = max_source_block_length
      };

      FileEntry fe{
        .toi = toi,
        .stream_id = stream_id,
        .content_location = std::string(content_location),
        .content_length = content_length,
        .content_md5 = std::string(content_md5),
        .content_type = std::string(content_type),
        .expires = expires,
        .should_be_complete_at = deadline,
        .fec_oti = fec_oti,
        .fec_transformer = fec_transformer
      };
      _file_entries.push_back(fe);
    } catch (std::exception &ex) {
      spdlog::warn("[RECEIVE] Failed to parse FDT file entry: {}", ex.what());
    } catch (const char *errorMessage) {
      spdlog::warn("[RECEIVE] Failed to parse FDT file entry: {}", errorMessage);
    } catch (...)
    {
      spdlog::warn("[RECEIVE] Failed to parse FDT file entry: unknown error");
    }
  }
}

auto LibFlute::FileDeliveryTable::add(FileEntry& fe) -> void
{
  const std::lock_guard<LockableBase(std::mutex)> lock(_fdt_mutex);
  // Increment id and wrap around when greater than 0xFFFFF
  _instance_id = (_instance_id + 1) & ((1 << 20) - 1);
  _file_entries.push_back(fe);
}

auto LibFlute::FileDeliveryTable::remove(uint32_t toi) -> void
{
  const std::lock_guard<LockableBase(std::mutex)> lock(_fdt_mutex);

  for (auto it = _file_entries.begin(); it != _file_entries.end();) {
    if (it->toi == toi) {
      it = _file_entries.erase(it);
    } else {
      ++it;
    }
  }
  // Increment id and wrap around when greater than 0xFFFFF
  _instance_id = (_instance_id + 1) & ((1 << 20) - 1);
}

auto LibFlute::FileDeliveryTable::to_string() const -> std::string {
  const std::lock_guard<LockableBase(std::mutex)> lock(_fdt_mutex);

  // Create a local copy of the global fec oti
  // If there is only one file entry, use its fec_oti as the global one
  auto current_global_fec_oti = _file_entries.size() != 1 ? _global_fec_oti : _file_entries[0].fec_oti;

  tinyxml2::XMLDocument doc;
  doc.InsertFirstChild( doc.NewDeclaration() );
  auto root = doc.NewElement("FDT-Instance");
  root->SetAttribute("Expires", std::to_string(_expires).c_str());
  root->SetAttribute("FEC-OTI-FEC-Encoding-ID", (unsigned)current_global_fec_oti.encoding_id);
  root->SetAttribute("FEC-OTI-Maximum-Source-Block-Length", (unsigned)current_global_fec_oti.max_source_block_length);
  root->SetAttribute("FEC-OTI-Encoding-Symbol-Length", (unsigned)current_global_fec_oti.encoding_symbol_length);
  root->SetAttribute("xmlns:mbms2007", "urn:3GPP:metadata:2007:MBMS:FLUTE:FDT");
  doc.InsertEndChild(root);

  for (const auto& file : _file_entries) {
    auto f = doc.NewElement("File");
    f->SetAttribute("TOI", file.toi);
    f->SetAttribute("Content-Location", file.content_location.c_str());
    f->SetAttribute("Content-Length", file.content_length);
    if (file.fec_oti.transfer_length != file.content_length) {
      f->SetAttribute("Transfer-Length", (unsigned)file.fec_oti.transfer_length);
    }
    if (file.content_md5.length() > 0) {
      f->SetAttribute("Content-MD5", file.content_md5.c_str());
    }
    if (file.content_type.length() > 0) {
        f->SetAttribute("Content-Type", file.content_type.c_str());
    }
    if(file.fec_transformer) {
      file.fec_transformer->add_fdt_info(f, current_global_fec_oti);
    } else {
      // Check if the encoding id differs from the global one
      if (file.fec_oti.encoding_id != current_global_fec_oti.encoding_id) {
        f->SetAttribute("FEC-OTI-FEC-Encoding-ID", (unsigned)file.fec_oti.encoding_id);
      }
      // Check if the max source block length differs from the global one
      if (file.fec_oti.max_source_block_length != current_global_fec_oti.max_source_block_length) {
        f->SetAttribute("FEC-OTI-Maximum-Source-Block-Length", (unsigned)file.fec_oti.max_source_block_length);
      }
      // Check if the encoding symbol length differs from the global one
      if (file.fec_oti.encoding_symbol_length != current_global_fec_oti.encoding_symbol_length) {
        f->SetAttribute("FEC-OTI-Encoding-Symbol-Length", (unsigned)file.fec_oti.encoding_symbol_length);
      }
    }
    auto cc = doc.NewElement("mbms2007:Cache-Control");
    auto exp = doc.NewElement("mbms2007:Expires");
    exp->SetText(std::to_string(file.expires).c_str());
    cc->InsertEndChild(exp);
    f->InsertEndChild(cc);
    if (file.should_be_complete_at > 0) {
      auto r = doc.NewElement("mbms2007:Recover");
      auto d = doc.NewElement("mbms2007:Deadline");
      d->SetText(std::to_string(file.should_be_complete_at).c_str());
      r->InsertEndChild(d);
      f->InsertEndChild(r);
    }
    if (file.stream_id > 0) {
      auto si = doc.NewElement("mbms2007:Stream");
      auto si_id = doc.NewElement("mbms2007:Id");
      si_id->SetText(std::to_string(file.stream_id).c_str());
      si->InsertEndChild(si_id);
      f->InsertEndChild(si);
    }
    root->InsertEndChild(f);
  }


  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  return std::string(printer.CStr());
}
