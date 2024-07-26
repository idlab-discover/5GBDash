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
#include "Recovery/Fetcher.h"

#define BOOST_BIND_GLOBAL_PLACEHOLDERS

#include <iostream>
#include <string>
#include <map>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include "spdlog/spdlog.h"
#include "Metric/Metrics.h"
#include <Recovery/Client.h>

#include "public/tracy/Tracy.hpp"

using boost::asio::ip::tcp;

LibFlute::Fetcher::Fetcher( const std::string& url)
    : _url(url)
    , _url_regex(R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)")
    ,  metricsInstance(LibFlute::Metric::Metrics::getInstance())
{
    if (_url.length() == 0) {
        spdlog::debug("[FETCHER] Fetcher is disabled.");
        return;
    }

    _io_service.reset();

    // Start the io_service in a new thread
    _io_service_thread = std::jthread([this](){
        ZoneScopedN("Fetcher::_io_service_thread");
        spdlog::info("[FETCHER] IO thread started");
        metricsInstance.addThread(std::this_thread::get_id(), "Fetcher IO thread");

        while (_stop_thread.load() == false)
        { 
            // Create a work guard to keep the io_service running
            auto work_guard = boost::asio::make_work_guard(_io_service);
            _io_service.run();
            _io_service.reset();
        }
        spdlog::info("[FETCHER] IO thread stopped");
    });

    spdlog::info("[FETCHER] Fetcher created for URL: {}", _url);
}

LibFlute::Fetcher::~Fetcher() {
    spdlog::info("[FETCHER] Destroying Fetcher instance.");
    if (_url.length() == 0) {
        return;
    }
    // Stop the io_service
    _stop_thread.store(true);
    _io_service.stop();
    // Join the io_service thread
    if (_io_service_thread.joinable()) {
        _io_service_thread.join();
        // spdlog::debug("[FETCHER] Joined IO thread.");
    }
}

auto LibFlute::Fetcher::fetch_fdt() -> void
{
    ZoneScopedN("Fetcher::fetch_fdt");
    if (
        _url.length() == 0 // Do not retrieve if there is no known url.
        || ! _fdt_cb // Do not retrieve if there is no callback to handle the FDT.
    ) {
        spdlog::debug("[FETCHER] Not fetching the missing fdt.");
        return;
    }

    bool use_fake_network_socket = _fake_network_socket != nullptr && _url.compare("fake_network_socket") == 0;

    try {
        // _socket.set_option(boost::asio::socket_base::receive_buffer_size(16*1024*1024));

        std::smatch url_match;
        std::string scheme;
        std::string host;
        std::string port;
        std::string path = "/fdt";
        std::string objectJson;
        if (!use_fake_network_socket) {
            if (!std::regex_match(_url, url_match, _url_regex)) {
                spdlog::warn("[FETCHER] Invalid URL: {}", _url);
                return;
            }
            scheme = url_match[1].str();
            host = url_match[2].str();
            port = url_match[3].str().empty() ? "80" : url_match[3].str();
            bool isSecure = scheme == "https";
            if (isSecure) {
                port = "443";
            }
        } else {
            objectJson = "{\"toi\":0}";
        }
    

        boost::shared_ptr<LibFlute::Client> client(new LibFlute::Client(
            _io_service, host, port, path, objectJson,
            [this](char * buffer, size_t bytes_recvd) {
                size_t end_offset = 4; // length of "\r\n\r\n";
                this->handle_FDT(buffer, bytes_recvd - end_offset);                
            },
            [&](size_t bytes_recvd_total, size_t latency_us) {
                // This is the callback function that will be called when the client is done

                // Calculate the bandwidth used by this client.
                LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
                auto fetcher_bandwidth = metricsInstance.getOrCreateGauge("fetcher_bandwidth");

                // A request that takes longer than 60 seconds is probably not a valid FDT.
                if (bytes_recvd_total > 0 && latency_us > 0 && latency_us < 60000000) {
                    double latencySeconds = static_cast<double>(latency_us) / 1000000.0; // us to s
                    double bandwidth = static_cast<double>(bytes_recvd_total) / latencySeconds; // bytes per second
                    double bandwidthkbps = bandwidth * 8.0 / 1000.0; // kbits instead of bytes
                    double roundedBandwidth = std::round(bandwidthkbps * 1000.0) / 1000.0;
                    fetcher_bandwidth->Set(roundedBandwidth);
                    spdlog::debug("[FETCHER] Fetcher finished for TOI 0. Received {} bytes in {} us. Bandwidth: {} kbps", bytes_recvd_total, latency_us, fetcher_bandwidth->Value());
                } else {
                    // The request has failed, so there is no bandwidth.
                    fetcher_bandwidth->Set(0);
                }

                // Perform any cleanup or handling of completion here
                // Remove the client from the vector
                _activeClients.erase(std::remove(_activeClients.begin(), _activeClients.end(), client), _activeClients.end());
            }));
        _activeClients.push_back(client);

        if (use_fake_network_socket) {
            client->set_fake_network_socket(_fake_network_socket);
        }

        // Start the actual fetch request.
        client->start();

    } catch (std::exception &ex) {
      spdlog::warn("[FETCHER] Failed to fetch missing fdt: {}", ex.what());
    } catch (const char* errorMessage) {
      spdlog::warn("[FETCHER] Failed to fetch missing fdt: {}", errorMessage);
    } catch (...) {
      spdlog::warn("[FETCHER] Failed to fetch missing fdt: unknown error");
    } 
}


auto LibFlute::Fetcher::fetch_alcs(
    const uint32_t toi,
    LibFlute::FecScheme fec,
    const std::string &content_location,
    std::shared_ptr<std::map<uint16_t, std::vector<uint16_t>>> missing_symbols) -> void
{
    ZoneScopedN("Fetcher::fetch_alcs");
    if (
        _url.length() == 0 // Do not retrieve if there is no known url.
        || missing_symbols->size() == 0 // Do not retrieve if there are no missing symbols.
        || !_alc_cb // Do not retrieve if there is no callback to handle the ALC.
    ) {
        spdlog::info("[FETCHER] Not fetching the missing symbols.");
        return;
    }

    bool use_fake_network_socket = _fake_network_socket != nullptr && _url.compare("fake_network_socket") == 0;

    spdlog::trace("[FETCHER] Fetching missing symbols for TOI {}", toi);

    try {
        // _socket.set_option(boost::asio::socket_base::receive_buffer_size(16*1024*1024));

        std::smatch url_match;
        std::string scheme;
        std::string host;
        std::string port;
        std::string path = "/alc";
        if (!use_fake_network_socket) {
            if (!std::regex_match(_url, url_match, _url_regex)) {
                spdlog::warn("[FETCHER] Invalid URL: {}", _url);
                return;
            }
            scheme = url_match[1].str();
            host = url_match[2].str();
            port = url_match[3].str().empty() ? "80" : url_match[3].str();
            bool isSecure = scheme == "https";
            if (isSecure) {
                port = "443";
            }
            path = url_match[4].str();
        }
        
        // Convert the map to a property tree
        boost::property_tree::ptree symbol_tree;
        for (const auto& entry : *missing_symbols) {
            // Skip empty entries
            if (entry.second.size() == 0) {
                continue;
            }
            std::string key = std::to_string(entry.first);
            boost::property_tree::ptree values;
            for (const auto& value : entry.second) {
                values.push_back(std::make_pair("", boost::property_tree::ptree(std::to_string(value))));
            }
            symbol_tree.add_child(key, values);
        }

        // Check the size of the tree
        if (symbol_tree.size() == 0) {
            spdlog::debug("[FETCHER] Not fetching the missing symbols. No symbols to fetch for TOI {}.", toi);
            return;
        }

        boost::property_tree::ptree tree;
        tree.put("toi",std::to_string(toi));
        tree.put("file",content_location);
        tree.put("fec",std::to_string(static_cast<int>(fec)));
        tree.add_child("missing",symbol_tree);

        // Convert the property tree to a JSON string
        std::ostringstream body_stream;
        boost::property_tree::json_parser::write_json(body_stream, tree, false);
        std::string objectJson = body_stream.str();

        // spdlog::debug("[FETCHER] Fetching missing symbols for TOI {} with JSON: {}", toi, objectJson);

        boost::shared_ptr<LibFlute::Client> client(new LibFlute::Client(
            _io_service, host, port, path, objectJson,
            [this](char * buffer, size_t bytes_recvd) {
                size_t end_offset = 4; // length of "\r\n\r\n";
                this->handle_ALC(buffer, bytes_recvd - end_offset);
            },
            [&, toi](size_t bytes_recvd_total, size_t latency_us) {
                // This is the callback function that will be called when the client is done

                // Calculate the bandwidth used by this client.
                LibFlute::Metric::Metrics& metricsInstance = LibFlute::Metric::Metrics::getInstance();
                auto fetcher_bandwidth = metricsInstance.getOrCreateGauge("fetcher_bandwidth");

                // A request that takes longer than 60 seconds is probably not a valid ALC.
                if (bytes_recvd_total > 0 && latency_us > 0 && latency_us < 60000000) {
                    double latencySeconds = static_cast<double>(latency_us) / 1000000.0; // us to s
                    double bandwidth = static_cast<double>(bytes_recvd_total) / latencySeconds; // bytes per second
                    double bandwidthkbps = bandwidth * 8.0 / 1000.0; // kbits instead of bytes
                    double roundedBandwidth = std::round(bandwidthkbps * 1000.0) / 1000.0;
                    fetcher_bandwidth->Set(roundedBandwidth);
                    spdlog::debug("[FETCHER] Fetcher finished for TOI {}. Received {} bytes in {} us. Bandwidth: {} kbps", toi, bytes_recvd_total, latency_us, fetcher_bandwidth->Value());
                } else {
                    // The request has failed, so there is no bandwidth.
                    fetcher_bandwidth->Set(0);
                }

                // Perform any cleanup or handling of completion here
                // Remove the client from the vector
                _activeClients.erase(std::remove(_activeClients.begin(), _activeClients.end(), client), _activeClients.end());
            }));
        _activeClients.push_back(client);

        if (use_fake_network_socket) {
            client->set_fake_network_socket(_fake_network_socket);
        }

        // Start the actual fetch request.
        client->start();

    } catch (std::exception &ex) {
      spdlog::warn("[FETCHER] Failed to fetch missing symbols: {}", ex.what());
    } catch (const char* errorMessage) {
      spdlog::warn("[FETCHER] Failed to fetch missing symbols: {}", errorMessage);
    } catch (...) {
      spdlog::warn("[FETCHER] Failed to fetch missing symbols: unknown error");
    }  
}

auto LibFlute::Fetcher::handle_ALC(char * buffer, size_t bytes_recvd) -> void
{
    ZoneScopedN("Fetcher::handle_ALC");   
    if (bytes_recvd == 0){
        return;
    }


    // We are only interested in lines that start with "ALC ".
    size_t start_offset = 4; // length of "ALC "
    size_t alc_length =  bytes_recvd - start_offset;
    spdlog::trace("[FETCHER] Received {} ALC bytes from Fetcher", alc_length);
    if (strncmp(buffer, "ALC ", start_offset) == 0) {
        try{
            if (_alc_cb){
                // Calculate the starting point for the buffer
                const char *alc_data = buffer + start_offset;

                /*
                auto len_to_display = alc_length < 50 ? alc_length : 50;
                std::stringstream hex_stream;
                std::stringstream ascii_stream;
                std::stringstream binary_stream;
                hex_stream << std::hex << std::setfill('0');
                for (int i = 0; i < len_to_display; ++i) {
                    auto c = alc_data[i];
                    hex_stream << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
                    if (i < len_to_display / 4) {
                        binary_stream << std::bitset<8>(static_cast<unsigned char>(c)); // Add binary stream
                    }
                    if (c >= 32 && c <= 126) {
                        ascii_stream << c;
                    } else {
                        ascii_stream << '?';
                    }
                    if (i % 2 == 1) {
                        hex_stream << ' ';  // Add a space every two characters
                        ascii_stream << ' ' << ' ' << ' ';
                        binary_stream << ' ';
                    } else if (i < len_to_display / 4) {
                        binary_stream << '.';
                    }
                }

                spdlog::trace("[FETCHER] First {} / {} bytes in hex:   {}", len_to_display, alc_length, hex_stream.str());
                spdlog::trace("[FETCHER] First {} / {} bytes in ascii: {}", len_to_display, alc_length, ascii_stream.str());
                spdlog::trace("[FETCHER] First {}  / {} bytes in bin:   {}", len_to_display / 8, alc_length, binary_stream.str());
                */

                // Call the callback responsible for handling the received ALC.
                _alc_cb(alc_data, alc_length);
                 
            }
        } catch (std::exception &ex) {
            spdlog::warn("[FETCHER] Failed to handle fetched ALC: {}", ex.what());
        } catch (const char* errorMessage) {
            spdlog::warn("[FETCHER] Failed to handle fetched ALC: {}", errorMessage);
        } catch (...) {
            spdlog::warn("[FETCHER] Failed to handle fetched ALC: unknown error");
        }
    } else if (bytes_recvd > 0) {
        spdlog::warn("[FETCHER] Received ALC data that does not start with 'ALC '.");
    }
}

auto LibFlute::Fetcher::handle_FDT(char * buffer, size_t bytes_recvd) -> void
{
    ZoneScopedN("Fetcher::handle_FDT");    
    if (bytes_recvd == 0){
        return;
    }

    spdlog::trace("[FETCHER] Received {} FDT bytes from Fetcher", bytes_recvd);

    try{
        if (_fdt_cb) {
            // Call the callback responsible for handling the received FDT.
            _fdt_cb(buffer, bytes_recvd);
        }
    } catch (std::exception &ex) {
        spdlog::warn("[FETCHER] Failed to handle fetched FDT: {}", ex.what());
    } catch (const char* errorMessage) {
        spdlog::warn("[FETCHER] Failed to handle fetched FDT: {}", errorMessage);
    } catch (...) {
        spdlog::warn("[FETCHER] Failed to handle fetched FDT: unknown error");
    }   
}