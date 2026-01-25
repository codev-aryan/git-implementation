#pragma once
#include <string>
#include <vector>

namespace Utils {
    std::string calculate_sha1(const std::string& input);
    std::string hex_to_raw(const std::string& hex);
    std::string raw_to_hex(const std::string& raw);
    
    // Zlib wrappers
    std::string compress_string(const std::string& data);
    std::string decompress_object(const std::string& compressed_data);
}