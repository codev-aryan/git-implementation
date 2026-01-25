#pragma once
#include <string>
#include <filesystem>

namespace Repository {
    // Initialization
    void init();

    // Core Object I/O
    std::string save_object_to_disk(const std::string& content, const std::string& type);
    std::string read_object_from_disk(const std::string& sha_hex);
    
    // Higher Level Operations
    std::string create_blob(const std::filesystem::path& filepath);
    std::string write_tree_recursive(const std::filesystem::path& current_path);
    std::string commit_tree(const std::string& tree_sha, const std::string& parent_sha, const std::string& message);
}