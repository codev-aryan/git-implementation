#include "Repository.hpp"
#include "Utils.hpp"
#include <fstream>
#include <vector>
#include <algorithm>
#include <sstream>
#include <ctime>

namespace Repository {

    void init() {
        if (!std::filesystem::exists(".git")) {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
            std::filesystem::create_directory(".git/refs/heads");
            std::ofstream headFile(".git/HEAD");
            headFile << "ref: refs/heads/main\n";
            headFile.close();
        }
    }

    std::string save_object_to_disk(const std::string& content, const std::string& type) {
        std::string header = type + " " + std::to_string(content.size()) + '\0';
        std::string full_object = header + content;

        std::string sha1_hash = Utils::calculate_sha1(full_object);
        std::string compressed_data = Utils::compress_string(full_object);

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

    std::string read_object_from_disk(const std::string& sha_hex) {
        std::string dirName = sha_hex.substr(0, 2);
        std::string fileName = sha_hex.substr(2);
        std::filesystem::path objectPath = std::filesystem::path(".git") / "objects" / dirName / fileName;

        std::ifstream file(objectPath, std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Object not found: " + sha_hex);

        std::string compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string full = Utils::decompress_object(compressed);
        
        size_t null_pos = full.find('\0');
        return full.substr(null_pos + 1);
    }

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
            tree_content += e.mode + " " + e.name + '\0' + Utils::hex_to_raw(e.sha_hex);
        }

        return save_object_to_disk(tree_content, "tree");
    }

    std::string commit_tree(const std::string& tree_sha, const std::string& parent_sha, const std::string& message) {
        std::stringstream content;
        content << "tree " << tree_sha << "\n";
        if (!parent_sha.empty()) content << "parent " << parent_sha << "\n";
        std::time_t now = std::time(nullptr);
        // Note: Using a fixed author for consistent output or dynamic if preferred. 
        // Keeping original user string:
        content << "author CodeCrafters <git@codecrafters.io> " << now << " +0000\n";
        content << "committer CodeCrafters <git@codecrafters.io> " << now << " +0000\n\n";
        content << message << "\n";
        return save_object_to_disk(content.str(), "commit");
    }
}