#include "Cloner.hpp"
#include "Repository.hpp"
#include "Utils.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <map>
#include <cstdlib>
#include <zlib.h>
#include <sstream>

namespace Cloner {

    // --- Private Helper Functions ---

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
        std::string tree_content = Repository::read_object_from_disk(sha);
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
            std::string entry_sha = Utils::raw_to_hex(sha_raw);
            
            cursor = null + 1 + 20;
            
            std::filesystem::path entry_path = dir / name;
            
            if (mode == "40000") { // Directory
                std::filesystem::create_directory(entry_path);
                checkout_tree(entry_sha, entry_path);
            } else { // Blob
                std::string content = Repository::read_object_from_disk(entry_sha);
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

    // --- Main Clone Logic ---

    void clone_repo(const std::string& url, const std::string& dir) {
        // 1. Initialize Directory
        try {
            if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
            std::filesystem::current_path(dir);
            Repository::init();
        } catch(const std::exception& e) {
            std::cerr << "Init error: " << e.what() << "\n";
            return;
        }

        // 2. Fetch Refs (Discovery)
        std::string refs_file = "refs_response";
        std::string cmd_refs = "curl -s -L -f \"" + url + "/info/refs?service=git-upload-pack\" -o " + refs_file;
        if (std::system(cmd_refs.c_str()) != 0) {
            std::cerr << "Failed to fetch refs\n"; 
            return; 
        }

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
            
            if (len == 0) { ref_cursor += 4; continue; }
            if (ref_cursor + len > refs_content.size()) break;
            
            std::string line = refs_content.substr(ref_cursor + 4, len - 4);
            ref_cursor += len;
            
            if (line.find("HEAD") != std::string::npos) {
                 if (line.size() >= 40) {
                     head_hash = line.substr(0, 40);
                     break;
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

        cursor += 8; // Skip PACK + Version
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
                    base_sha = Utils::raw_to_hex(sha_raw);
                }
            }

            // Decompress stream manually because it's embedded in pack data
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
                    // Resolve delta against base object
                    std::string dirName = base_sha.substr(0, 2);
                    std::string fileName = base_sha.substr(2);
                    std::filesystem::path p = std::filesystem::path(".git") / "objects" / dirName / fileName;
                    
                    if (std::filesystem::exists(p)) {
                        // We must read base manually or use Repository::read_object_from_disk
                        // However, read_object returns just content, we need the header for logic sometimes?
                        // Actually, Repository::read_object_from_disk strips header.
                        // But apply_delta needs raw content.
                        
                        // Let's use Repository helper but keep in mind we need the content
                        std::string base_content = Repository::read_object_from_disk(base_sha);
                        obj_data = apply_delta(base_content, obj_data);
                        
                        // We need to know the type of the base object to save the new object correctly.
                        // A cheap way is to guess or store it, but for simplicity we rely on the fact 
                        // that the base object is usually a blob for file deltas.
                        // *Correction*: We can cheat. The header of the base object on disk has the type.
                        
                        std::ifstream f(p, std::ios::binary);
                        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        std::string full = Utils::decompress_object(c);
                        size_t sp = full.find(' ');
                        if (sp != std::string::npos) {
                            final_type = full.substr(0, sp);
                        }
                    }
                } catch (...) {}
            } else {
                if (type == 1) final_type = "commit";
                else if (type == 2) final_type = "tree";
                else if (type == 3) final_type = "blob";
                else if (type == 4) final_type = "tag";
            }

            if (!final_type.empty()) {
                std::string new_sha = Repository::save_object_to_disk(obj_data, final_type);
                offset_to_sha[obj_start_offset] = new_sha;
            }
        }

        // 5. Update HEAD and Checkout
        std::ofstream headFile(".git/refs/heads/main");
        headFile << head_hash << "\n";
        headFile.close();

        try {
            std::string commit_content = Repository::read_object_from_disk(head_hash);
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
}