#include "Utils.hpp"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <zlib.h>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

namespace Utils {

    std::string calculate_sha1(const std::string& input) {
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
            ss << std::setw(2) << (int)hash[i];
        }
        return ss.str();
    }

    std::string hex_to_raw(const std::string& hex) {
        std::string raw;
        raw.reserve(20);
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            char byte = (char)strtol(byteString.c_str(), nullptr, 16);
            raw.push_back(byte);
        }
        return raw;
    }

    std::string raw_to_hex(const std::string& raw) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (unsigned char c : raw) {
            ss << std::setw(2) << (int)c;
        }
        return ss.str();
    }

    std::string compress_string(const std::string& data) {
        z_stream zs;
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        
        if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
            throw std::runtime_error("Failed to initialize zlib compression");
        }

        zs.avail_in = data.size();
        zs.next_in = (Bytef*)data.data();

        int ret;
        char buffer[8192];
        std::string compressed_data;

        do {
            zs.avail_out = sizeof(buffer);
            zs.next_out = (Bytef*)buffer;
            ret = deflate(&zs, Z_FINISH);
            if (compressed_data.size() < zs.total_out) {
                compressed_data.append(buffer, zs.total_out - compressed_data.size());
            }
        } while (ret == Z_OK);

        deflateEnd(&zs);
        return compressed_data;
    }

    std::string decompress_object(const std::string& compressed_data) {
        z_stream zs;
        zs.zalloc = Z_NULL;
        zs.zfree = Z_NULL;
        zs.opaque = Z_NULL;
        zs.avail_in = compressed_data.size();
        zs.next_in = (Bytef*)compressed_data.data();

        if (inflateInit(&zs) != Z_OK) {
            throw std::runtime_error("Failed to initialize zlib");
        }

        std::string decompressed_data;
        char buffer[8192];
        int ret;

        do {
            zs.avail_out = sizeof(buffer);
            zs.next_out = (Bytef*)buffer;
            ret = inflate(&zs, Z_NO_FLUSH);
            if (decompressed_data.size() < zs.total_out) {
                decompressed_data.append(buffer, zs.total_out - decompressed_data.size());
            }
        } while (ret == Z_OK);

        inflateEnd(&zs);
        return decompressed_data;
    }
}