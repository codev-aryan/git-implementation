#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <zlib.h>
#include <openssl/sha.h>
#include <ctime>

// --- Helper Functions ---

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

// Helper to write any object (blob, tree, commit) to .git/objects
// Returns the SHA hash of the written object
std::string save_object_to_disk(const std::string& content, const std::string& type) {
    std::string header = type + " " + std::to_string(content.size()) + '\0';
    std::string full_object = header + content;

    std::string sha1_hash = calculate_sha1(full_object);
    std::string compressed_data = compress_string(full_object);

    std::string dirName = sha1_hash.substr(0, 2);
    std::string fileName = sha1_hash.substr(2);
    std::filesystem::path objDir = std::filesystem::path(".git") / "objects" / dirName;
    
    if (!std::filesystem::exists(objDir / fileName)) {
        std::filesystem::create_directories(objDir);
        std::ofstream outFile(objDir / fileName, std::ios::binary);
        outFile.write(compressed_data.data(), compressed_data.size());
        outFile.close();
    }

    return sha1_hash;
}

// --- Specific Object Handlers ---

std::string create_blob(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return save_object_to_disk(content, "blob");
}

struct TreeEntry {
    std::string name;
    std::string sha_hex;
    std::string mode;
    
    bool operator<(const TreeEntry& other) const {
        return name < other.name;
    }
};

std::string write_tree_recursive(const std::filesystem::path& current_path) {
    std::vector<TreeEntry> entries;

    for (const auto& entry : std::filesystem::directory_iterator(current_path)) {
        std::string name = entry.path().filename().string();
        if (name == ".git") continue;

        TreeEntry tree_entry;
        tree_entry.name = name;

        if (entry.is_directory()) {
            tree_entry.mode = "40000";
            tree_entry.sha_hex = write_tree_recursive(entry.path());
        } else {
            auto perms = entry.status().permissions();
            bool is_exec = (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
            tree_entry.mode = is_exec ? "100755" : "100644";
            tree_entry.sha_hex = create_blob(entry.path());
        }
        entries.push_back(tree_entry);
    }

    std::sort(entries.begin(), entries.end());

    std::string tree_content;
    for (const auto& e : entries) {
        tree_content += e.mode + " " + e.name + '\0' + hex_to_raw(e.sha_hex);
    }

    return save_object_to_disk(tree_content, "tree");
}

// --- Main ---

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
                std::cout << full_content.substr(null_pos + 1);
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

        std::string sha = create_blob(inputPath);
        std::cout << sha << "\n";
    }
    else if (command == "ls-tree") {
        if (argc < 4 || std::string(argv[2]) != "--name-only") {
             std::cerr << "Usage: git ls-tree --name-only <tree_sha>\n";
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
            if (null_pos == std::string::npos) return EXIT_FAILURE;
            size_t cursor = null_pos + 1;

            while (cursor < full_content.size()) {
                size_t space_pos = full_content.find(' ', cursor);
                if (space_pos == std::string::npos) break;
                
                size_t name_end_pos = full_content.find('\0', space_pos + 1);
                if (name_end_pos == std::string::npos) break;

                std::string name = full_content.substr(space_pos + 1, name_end_pos - (space_pos + 1));
                std::cout << name << "\n";
                cursor = name_end_pos + 1 + 20; 
            }
        } catch (const std::exception& e) {
            std::cerr << "Error decompressing object: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }
    else if (command == "write-tree") {
        try {
            std::string sha = write_tree_recursive(std::filesystem::current_path());
            std::cout << sha << "\n";
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    else if (command == "commit-tree") {
        // Usage: git commit-tree <tree_sha> [-p <parent_sha>] -m <message>
        if (argc < 4) {
             std::cerr << "Usage: git commit-tree <tree_sha> [-p <parent_sha>] -m <message>\n";
             return EXIT_FAILURE;
        }

        std::string tree_sha = argv[2];
        std::string parent_sha;
        std::string message;

        // Parse arguments flexibly
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-p" && i + 1 < argc) {
                parent_sha = argv[++i];
            } else if (arg == "-m" && i + 1 < argc) {
                message = argv[++i];
            }
        }

        if (message.empty()) {
            std::cerr << "Error: Commit message required (-m)\n";
            return EXIT_FAILURE;
        }

        std::stringstream commit_content;
        commit_content << "tree " << tree_sha << "\n";
        if (!parent_sha.empty()) {
            commit_content << "parent " << parent_sha << "\n";
        }

        std::time_t now = std::time(nullptr);
        std::string author_info = "CodeCrafters <git@codecrafters.io> " + std::to_string(now) + " +0000";

        commit_content << "author " << author_info << "\n";
        commit_content << "committer " << author_info << "\n";
        commit_content << "\n";
        commit_content << message << "\n";

        std::string sha = save_object_to_disk(commit_content.str(), "commit");
        std::cout << sha << "\n";
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}