#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

std::string decompress_object(const std::string& compressed_data) {
    z_stream zs; // z_stream is zlib's control structure
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = compressed_data.size(); // Input size
    zs.next_in = (Bytef*)compressed_data.data(); // Input pointer

    if (inflateInit(&zs) != Z_OK) {
        throw std::runtime_error("Failed to initialize zlib");
    }

    std::string decompressed_data;
    char buffer[8192];
    int ret;

    // Loop to decompress the data in chunks
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

int main(int argc, char *argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::cerr << "Logs from your program will appear here!\n";

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
        
        // 1. Construct the path: .git/objects/HH/HASH_REMAINDER
        std::string dirName = hash.substr(0, 2);
        std::string fileName = hash.substr(2);
        std::filesystem::path objectPath = std::filesystem::path(".git") / "objects" / dirName / fileName;

        // 2. Read the compressed file
        std::ifstream file(objectPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Object not found at " << objectPath << "\n";
            return EXIT_FAILURE;
        }
        std::string compressed_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // 3. Decompress the content
        try {
            std::string full_content = decompress_object(compressed_content);
            
            // 4. Extract actual content (skip header: "blob <size>\0")
            size_t null_pos = full_content.find('\0');
            if (null_pos != std::string::npos) {
                std::string blob_content = full_content.substr(null_pos + 1);
                std::cout << blob_content; // Print without extra newline
            }
        } catch (const std::exception& e) {
            std::cerr << "Error decompressing object: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}