#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <zlib.h>
#include <openssl/sha.h>

std::string compress_string(const std::string& data) {
    z_stream zs;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    
    // Z_DEFAULT_COMPRESSION is standard for Git
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

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("Zlib compression failed");
    }

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

    if (ret != Z_STREAM_END) {
        throw std::runtime_error("Zlib decompression incomplete");
    }

    return decompressed_data;
}

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

int main(int argc, char *argv[])
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }

    std::string command = argv[1];

    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");

            std::ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }

            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } 
    else if (command == "cat-file") {
        if (argc < 4 || std::string(argv[2]) != "-p") {
             std::cerr << "Usage: git cat-file -p <blob_sha>\n";
             return EXIT_FAILURE;
        }

        std::string hash = argv[3];
        std::string dirName = hash.substr(0, 2);
        std::string fileName = hash.substr(2);
        std::filesystem::path objectPath = std::filesystem::path(".git") / "objects" / dirName / fileName;

        std::ifstream file(objectPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Object not found at " << objectPath << "\n";
            return EXIT_FAILURE;
        }
        std::string compressed_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        try {
            std::string full_content = decompress_object(compressed_content);
            size_t null_pos = full_content.find('\0');
            if (null_pos != std::string::npos) {
                std::string blob_content = full_content.substr(null_pos + 1);
                std::cout << blob_content;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error decompressing object: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }
    else if (command == "hash-object") {
        if (argc < 4 || std::string(argv[2]) != "-w") {
            std::cerr << "Usage: git hash-object -w <file>\n";
            return EXIT_FAILURE;
        }

        std::filesystem::path inputPath(argv[3]);
        if (!std::filesystem::exists(inputPath)) {
            std::cerr << "Error: File not found: " << inputPath << "\n";
            return EXIT_FAILURE;
        }

        // 1. Read file content
        std::ifstream file(inputPath, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // 2. Prepare the object (header + content)
        std::string header = "blob " + std::to_string(content.size()) + '\0';
        std::string full_object = header + content;

        // 3. Calculate SHA-1 Hash
        std::string sha1_hash = calculate_sha1(full_object);

        // 4. Compress the object
        std::string compressed_data = compress_string(full_object);

        // 5. Write to .git/objects
        std::string dirName = sha1_hash.substr(0, 2);
        std::string fileName = sha1_hash.substr(2);
        std::filesystem::path objDir = std::filesystem::path(".git") / "objects" / dirName;
        std::filesystem::create_directories(objDir);

        std::ofstream outFile(objDir / fileName, std::ios::binary);
        outFile.write(compressed_data.data(), compressed_data.size());
        outFile.close();

        // 6. Print Hash
        std::cout << sha1_hash << "\n";
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}