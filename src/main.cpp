#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <map>
#include <zlib.h>
#include <openssl/sha.h>
#include <ctime>
#include <cstdlib>
#include <cstring>

// --- Basic Helper Functions ---

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

std::string read_object_from_disk(const std::string& sha_hex) {
    std::string dirName = sha_hex.substr(0, 2);
    std::string fileName = sha_hex.substr(2);
    std::filesystem::path objectPath = std::filesystem::path(".git") / "objects" / dirName / fileName;

    std::ifstream file(objectPath, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Object not found: " + sha_hex);

    std::string compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string full = decompress_object(compressed);
    
    size_t null_pos = full.find('\0');
    return full.substr(null_pos + 1);
}

// --- Blob and Tree Handlers ---

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

// --- Clone & Packfile Logic ---

std::string apply_delta(const std::string& base, const std::string& delta) {
    size_t i = 0;
    size_t delta_len = delta.size();
    
    // Read source size
    size_t shift = 0;
    while (i < delta_len) {
        unsigned char byte = delta[i++];
        shift += 7;
        if (!(byte & 0x80)) break;
    }
    
    // Read target size
    shift = 0;
    size_t target_size = 0;
    while (i < delta_len) {
        unsigned char byte = delta[i++];
        target_size |= (byte & 0x7F) << shift;
        shift += 7;
        if (!(byte & 0x80)) break;
    }

    std::string result;
    result.reserve(target_size);

    while (i < delta_len) {
        unsigned char opcode = delta[i++];
        
        if (opcode & 0x80) { // Copy instruction
            size_t offset = 0;
            size_t size = 0;
            
            if (opcode & 0x01) { if (i < delta_len) offset |= (unsigned char)delta[i++]; }
            if (opcode & 0x02) { if (i < delta_len) offset |= (unsigned char)delta[i++] << 8; }
            if (opcode & 0x04) { if (i < delta_len) offset |= (unsigned char)delta[i++] << 16; }
            if (opcode & 0x08) { if (i < delta_len) offset |= (unsigned char)delta[i++] << 24; }
            
            if (opcode & 0x10) { if (i < delta_len) size |= (unsigned char)delta[i++]; }
            if (opcode & 0x20) { if (i < delta_len) size |= (unsigned char)delta[i++] << 8; }
            if (opcode & 0x40) { if (i < delta_len) size |= (unsigned char)delta[i++] << 16; }
            
            if (size == 0) size = 0x10000;
            
            if (offset + size <= base.size()) {
                 result.append(base.substr(offset, size));
            } else if (offset < base.size()) {
                 result.append(base.substr(offset));
            }
        } else { // Insert instruction
            size_t size = opcode & 0x7F;
            if (i + size <= delta_len) {
                result.append(delta.substr(i, size));
                i += size;
            } else {
                break; 
            }
        }
    }
    return result;
}

void checkout_tree(const std::string& sha, const std::filesystem::path& dir) {
    std::string tree_content = read_object_from_disk(sha);
    size_t cursor = 0;
    
    while (cursor < tree_content.size()) {
        size_t space = tree_content.find(' ', cursor);
        if (space == std::string::npos) break;

        size_t null = tree_content.find('\0', space);
        if (null == std::string::npos) break;
        
        std::string mode = tree_content.substr(cursor, space - cursor);
        std::string name = tree_content.substr(space + 1, null - (space + 1));
        
        if (null + 21 > tree_content.size()) break;
        
        std::string sha_raw = tree_content.substr(null + 1, 20);
        std::string entry_sha = raw_to_hex(sha_raw);
        
        cursor = null + 1 + 20;
        
        std::filesystem::path entry_path = dir / name;
        
        if (mode == "40000") { // Directory
            std::filesystem::create_directory(entry_path);
            checkout_tree(entry_sha, entry_path);
        } else { // Blob
            std::string content = read_object_from_disk(entry_sha);
            std::ofstream out(entry_path, std::ios::binary);
            out.write(content.data(), content.size());
            out.close();
            
            if (mode == "100755") {
                 std::filesystem::permissions(entry_path, 
                    std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::add);
            }
        }
    }
}

void handle_clone(const std::string& url, const std::string& dir) {
    // 1. Initialize Directory
    try {
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
        std::filesystem::current_path(dir);
        std::filesystem::create_directories(".git");
        std::filesystem::create_directories(".git/objects");
        std::filesystem::create_directories(".git/refs/heads");
        std::ofstream(".git/HEAD") << "ref: refs/heads/main\n";
    } catch(const std::exception& e) {
        std::cerr << "Init error: " << e.what() << "\n";
        return;
    }

    // 2. Fetch Refs (Discovery) with -L for redirects
    std::string refs_file = "refs_response";
    // Adding -L to follow redirects (e.g., http -> https or github redirections)
    std::string cmd_refs = "curl -s -L -f \"" + url + "/info/refs?service=git-upload-pack\" -o " + refs_file;
    if (std::system(cmd_refs.c_str()) != 0) {
        std::cerr << "Failed to fetch refs\n"; 
        return; 
    }

    // Parse Refs strictly (Packet Line Format)
    std::ifstream refs_in(refs_file, std::ios::binary);
    std::string refs_content((std::istreambuf_iterator<char>(refs_in)), std::istreambuf_iterator<char>());
    refs_in.close();
    std::filesystem::remove(refs_file);

    std::string head_hash;
    size_t ref_cursor = 0;
    while (ref_cursor + 4 <= refs_content.size()) {
        std::string len_hex = refs_content.substr(ref_cursor, 4);
        int len = 0;
        try { len = std::stoi(len_hex, nullptr, 16); } catch(...) { break; }
        
        if (len == 0) { // Flush packet "0000"
            ref_cursor += 4;
            continue; 
        }

        if (ref_cursor + len > refs_content.size()) break; // Truncated
        
        // Payload includes the length bytes in Git protocol logic? 
        // No, the length *includes* the 4 bytes. So payload is len-4.
        std::string line = refs_content.substr(ref_cursor + 4, len - 4);
        ref_cursor += len;
        
        // Line format: <hash> <ref>\0<capabilities>\n
        // Look for HEAD
        if (line.find("HEAD") != std::string::npos) {
             // Validate it looks like a hash (40 chars)
             if (line.size() >= 40) {
                 head_hash = line.substr(0, 40);
                 break; // Found it
             }
        }
    }
    
    if (head_hash.empty()) {
        std::cerr << "Head not found in refs\n";
        return;
    }

    // 3. Request Packfile
    std::string req_body_file = "req_body";
    std::ofstream req_out(req_body_file);
    req_out << "0032want " << head_hash << "\n00000009done\n";
    req_out.close();

    std::string pack_file = "pack_response";
    // Added -L here as well
    std::string cmd_pack = "curl -s -L -f -X POST --data-binary @" + req_body_file + 
                          " -H \"Content-Type: application/x-git-upload-pack-request\" " +
                          "\"" + url + "/git-upload-pack\" -o " + pack_file;
    if (std::system(cmd_pack.c_str()) != 0) {
        std::cerr << "Failed to fetch packfile\n";
        return;
    }

    std::ifstream pack_in(pack_file, std::ios::binary);
    std::vector<char> pack_data((std::istreambuf_iterator<char>(pack_in)), std::istreambuf_iterator<char>());
    pack_in.close();
    std::filesystem::remove(req_body_file);
    std::filesystem::remove(pack_file);

    // 4. Parse Packfile
    size_t cursor = 0;
    
    // Scan for "PACK" signature
    bool found_pack = false;
    for (size_t i = 0; i < pack_data.size(); ++i) {
        if (i + 4 <= pack_data.size() && 
            pack_data[i] == 'P' && pack_data[i+1] == 'A' && 
            pack_data[i+2] == 'C' && pack_data[i+3] == 'K') {
            cursor = i;
            found_pack = true;
            break;
        }
    }

    if (!found_pack) {
        std::cerr << "Invalid packfile: Signature not found\n";
        return;
    }

    cursor += 8; // Skip PACK(4) + Version(4)
    if (cursor + 4 > pack_data.size()) { std::cerr << "Packfile truncated\n"; return; }

    uint32_t num_objects = 0;
    for(int i=0; i<4; i++) num_objects = (num_objects << 8) | (unsigned char)pack_data[cursor++];

    std::map<size_t, std::string> offset_to_sha;

    for (uint32_t i = 0; i < num_objects; ++i) {
        if (cursor >= pack_data.size()) break;

        size_t obj_start_offset = cursor;
        unsigned char byte = pack_data[cursor++];
        int type = (byte >> 4) & 0x07;
        uint64_t size = byte & 0x0F;
        int shift = 4;
        
        while ((byte & 0x80) && cursor < pack_data.size()) {
            byte = pack_data[cursor++];
            size |= (uint64_t)(byte & 0x7F) << shift;
            shift += 7;
        }

        std::string base_sha;
        if (type == 6) { // OFS_DELTA
            uint64_t offset_delta = 0;
            if (cursor < pack_data.size()) {
                unsigned char c = pack_data[cursor++];
                offset_delta = c & 0x7F;
                while ((c & 0x80) && cursor < pack_data.size()) {
                    c = pack_data[cursor++];
                    offset_delta = ((offset_delta + 1) << 7) | (c & 0x7F);
                }
            }
            size_t base_offset = obj_start_offset - offset_delta;
            if (offset_to_sha.count(base_offset)) {
                base_sha = offset_to_sha[base_offset];
            }
        } else if (type == 7) { // REF_DELTA
            if (cursor + 20 <= pack_data.size()) {
                std::string sha_raw(pack_data.begin() + cursor, pack_data.begin() + cursor + 20);
                cursor += 20;
                base_sha = raw_to_hex(sha_raw);
            }
        }

        // Decompress
        z_stream zs;
        zs.zalloc = Z_NULL; zs.zfree = Z_NULL; zs.opaque = Z_NULL;
        inflateInit(&zs);
        
        zs.next_in = (Bytef*)&pack_data[cursor];
        zs.avail_in = pack_data.size() - cursor;
        
        std::vector<char> out_buffer;
        out_buffer.resize(size < 8192 ? 8192 : size + 512); 
        
        zs.next_out = (Bytef*)out_buffer.data();
        zs.avail_out = out_buffer.size();
        
        int ret;
        while ((ret = inflate(&zs, Z_FINISH)) == Z_OK) {
             size_t current_len = out_buffer.size();
             out_buffer.resize(current_len * 2);
             zs.next_out = (Bytef*)(out_buffer.data() + zs.total_out);
             zs.avail_out = out_buffer.size() - zs.total_out;
        }
        
        inflateEnd(&zs);
        cursor += zs.total_in; 
        
        std::string obj_data(out_buffer.data(), zs.total_out);
        std::string final_type;

        if (type == 6 || type == 7) {
            try {
                // To resolve delta, we need the base content. 
                // We rely on the base object being already on disk (since PACK usually sorts by type/topology)
                // or we peek the header.
                std::string dirName = base_sha.substr(0, 2);
                std::string fileName = base_sha.substr(2);
                std::filesystem::path p = std::filesystem::path(".git") / "objects" / dirName / fileName;
                
                if (std::filesystem::exists(p)) {
                    std::ifstream f(p, std::ios::binary);
                    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    std::string full = decompress_object(c);
                    // Full object is "type size\0content"
                    size_t sp = full.find(' ');
                    size_t nu = full.find('\0');
                    if (sp != std::string::npos && nu != std::string::npos) {
                        final_type = full.substr(0, sp);
                        std::string base_content = full.substr(nu + 1);
                        obj_data = apply_delta(base_content, obj_data);
                    }
                }
            } catch (...) {
                // Delta resolution failed, skip saving (or log error)
            }
        } else {
            if (type == 1) final_type = "commit";
            else if (type == 2) final_type = "tree";
            else if (type == 3) final_type = "blob";
            else if (type == 4) final_type = "tag";
        }

        if (!final_type.empty()) {
            std::string new_sha = save_object_to_disk(obj_data, final_type);
            offset_to_sha[obj_start_offset] = new_sha;
        }
    }

    // 5. Update HEAD and Checkout
    std::ofstream headFile(".git/refs/heads/main");
    headFile << head_hash << "\n";
    headFile.close();

    try {
        std::string commit_content = read_object_from_disk(head_hash);
        std::stringstream ss(commit_content);
        std::string line_commit;
        std::string tree_sha;
        while(std::getline(ss, line_commit)) {
            if (line_commit.rfind("tree ", 0) == 0) {
                tree_sha = line_commit.substr(5);
                break;
            }
        }
        if (!tree_sha.empty()) checkout_tree(tree_sha, ".");
    } catch (...) {
        std::cerr << "Failed to checkout\n";
    }
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
                return EXIT_FAILURE;
            }
            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } 
    else if (command == "cat-file") {
        if (argc < 4 || std::string(argv[2]) != "-p") return EXIT_FAILURE;
        try {
            std::cout << read_object_from_disk(argv[3]);
        } catch (...) { return EXIT_FAILURE; }
    }
    else if (command == "hash-object") {
        if (argc < 4 || std::string(argv[2]) != "-w") return EXIT_FAILURE;
        std::filesystem::path inputPath(argv[3]);
        if (!std::filesystem::exists(inputPath)) return EXIT_FAILURE;
        std::cout << create_blob(inputPath) << "\n";
    }
    else if (command == "ls-tree") {
        if (argc < 4 || std::string(argv[2]) != "--name-only") return EXIT_FAILURE;
        try {
            std::string content = read_object_from_disk(argv[3]);
            size_t cursor = 0;
            while (cursor < content.size()) {
                size_t space_pos = content.find(' ', cursor);
                if (space_pos == std::string::npos) break;
                size_t null_pos = content.find('\0', space_pos + 1);
                if (null_pos == std::string::npos) break;
                std::string name = content.substr(space_pos + 1, null_pos - (space_pos + 1));
                std::cout << name << "\n";
                cursor = null_pos + 1 + 20; 
            }
        } catch (...) { return EXIT_FAILURE; }
    }
    else if (command == "write-tree") {
        try {
            std::cout << write_tree_recursive(std::filesystem::current_path()) << "\n";
        } catch (...) { return EXIT_FAILURE; }
    }
    else if (command == "commit-tree") {
        if (argc < 4) return EXIT_FAILURE;
        std::string tree_sha = argv[2];
        std::string parent_sha, message;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-p" && i + 1 < argc) parent_sha = argv[++i];
            else if (arg == "-m" && i + 1 < argc) message = argv[++i];
        }
        std::stringstream content;
        content << "tree " << tree_sha << "\n";
        if (!parent_sha.empty()) content << "parent " << parent_sha << "\n";
        std::time_t now = std::time(nullptr);
        content << "author CodeCrafters <git@codecrafters.io> " << now << " +0000\n";
        content << "committer CodeCrafters <git@codecrafters.io> " << now << " +0000\n\n";
        content << message << "\n";
        std::cout << save_object_to_disk(content.str(), "commit") << "\n";
    }
    else if (command == "clone") {
        if (argc < 4) return EXIT_FAILURE;
        handle_clone(argv[2], argv[3]);
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}